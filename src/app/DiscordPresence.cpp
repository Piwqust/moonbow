#include "app/DiscordPresence.h"

#include "Player.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/player/Episodes.h"
#include "app/player/NowPlaying.h"
#include "core/Config.h"
#include "core/Json.h"
#include "core/Log.h"
#include "library/MediaTypes.h"
#include "net/ServiceKeys.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace app {

namespace {

using Clock = std::chrono::steady_clock;

// --- the pipe, on its own thread --------------------------------------------------------------

class Pipe {
public:
    ~Pipe() { Stop(); }

    // UI thread: the activity to show (a SET_ACTIVITY "activity" object), or nothing to clear it.
    void Want(std::optional<std::string> activity) {
        {
            std::lock_guard lock(mutex_);
            want_ = std::move(activity);
            pending_ = true;
            if (!thread_.joinable() && want_) thread_ = std::thread([this] { Run(); });
        }
        wake_.notify_all();
    }

    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (!thread_.joinable()) return;
        // Every pipe call gives up after a second, so this is quick; a Discord that hangs still
        // never holds up Moonbow's exit: after 2 s the worker is left to end with the process.
        if (WaitForSingleObject(thread_.native_handle(), 2000) == WAIT_OBJECT_0)
            thread_.join();
        else
            thread_.detach();
    }

private:
    enum Op : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

    void Run() {
        Clock::time_point lastSent = Clock::now() - std::chrono::seconds(10);
        Clock::time_point retryAt = Clock::now();
        std::unique_lock lock(mutex_);
        while (!stop_) {
            const Clock::time_point due = std::max(lastSent + std::chrono::seconds(4), retryAt);
            if (!pending_)
                wake_.wait_for(lock, std::chrono::seconds(15), [this] { return stop_ || pending_; });
            else if (Clock::now() < due)
                wake_.wait_until(lock, due, [this] { return stop_; });
            if (stop_) break;
            if (!pending_) {
                lock.unlock();
                Drain();  // answers Discord's pings while idle
                lock.lock();
                continue;
            }
            if (Clock::now() < std::max(lastSent + std::chrono::seconds(4), retryAt)) continue;
            const std::optional<std::string> activity = want_;
            pending_ = false;
            lock.unlock();
            bool ok = true;
            if (pipe_ == INVALID_HANDLE_VALUE && activity) ok = Connect();
            if (ok && pipe_ != INVALID_HANDLE_VALUE) {
                ok = Send(Frame, Command(activity));
                Drain();
            }
            lastSent = Clock::now();
            lock.lock();
            if (!ok) {
                Disconnect();
                if (activity && !pending_) pending_ = true;  // Discord not running: try again later
                retryAt = Clock::now() + std::chrono::seconds(15);
            }
        }
        lock.unlock();
        if (pipe_ != INVALID_HANDLE_VALUE) Send(Frame, Command(std::nullopt));  // leave no stale status
        Disconnect();
        if (event_) CloseHandle(event_);
        event_ = nullptr;
    }

    std::string Command(const std::optional<std::string>& activity) {
        return R"({"cmd":"SET_ACTIVITY","args":{"pid":)" + std::to_string(GetCurrentProcessId()) +
               R"(,"activity":)" + (activity ? *activity : std::string("null")) + R"(},"nonce":")" +
               std::to_string(++nonce_) + R"("})";
    }

    bool Connect() {
        const std::string id = net::keys::DiscordApp();
        if (id.empty()) return false;
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            pipe_ = h;
            if (Send(Handshake, R"({"v":1,"client_id":")" + id + R"("})") && AwaitReady()) {
                Log("discord: connected");
                return true;
            }
            Disconnect();
        }
        return false;
    }

    // The handshake's answer: READY, or CLOSE with an error (an unknown application id).
    bool AwaitReady() {
        for (int waited = 0; waited < 3000; waited += 50) {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (avail >= 8) {
                uint32_t op = 0;
                std::string body;
                if (!Read(&op, &body)) return false;
                return op == Frame && body.find("\"READY\"") != std::string::npos;
            }
            Sleep(50);
        }
        return false;
    }

    // Reads what Discord sent (answers to SET_ACTIVITY, pings) without blocking.
    void Drain() {
        while (pipe_ != INVALID_HANDLE_VALUE) {
            DWORD avail = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
                Disconnect();
                return;
            }
            if (avail < 8) return;
            uint32_t op = 0;
            std::string body;
            if (!Read(&op, &body)) {
                Disconnect();
                return;
            }
            if (op == Ping) Send(Pong, body);
            if (op == Close) {
                Log("discord: closed by Discord: " + body.substr(0, 200));
                Disconnect();
                return;
            }
        }
    }

    bool Read(uint32_t* op, std::string* body) {
        uint32_t header[2] = {};
        if (!ReadExactly(header, sizeof(header))) return false;
        if (header[1] > (1u << 20)) return false;
        body->resize(header[1]);
        if (header[1] && !ReadExactly(body->data(), header[1])) return false;
        *op = header[0];
        return true;
    }

    // One read or write on the overlapped pipe, given up after a second: a Discord that stops
    // reading can't block the worker (and so Moonbow's exit).
    bool Transfer(bool write, void* data, DWORD size, DWORD* done) {
        *done = 0;
        if (!event_) event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event_) return false;
        OVERLAPPED ov{};
        ov.hEvent = event_;
        ResetEvent(event_);
        const BOOL started = write ? WriteFile(pipe_, data, size, nullptr, &ov) : ReadFile(pipe_, data, size, nullptr, &ov);
        if (!started && GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(event_, 1000) != WAIT_OBJECT_0) {
            CancelIoEx(pipe_, &ov);
            GetOverlappedResult(pipe_, &ov, done, TRUE);  // `ov` is on this stack: wait for the cancel
            return false;
        }
        return GetOverlappedResult(pipe_, &ov, done, FALSE) != FALSE;
    }

    bool ReadExactly(void* data, DWORD size) {
        auto* p = static_cast<char*>(data);
        while (size) {
            DWORD got = 0;
            if (!Transfer(false, p, size, &got) || !got) return false;
            p += got;
            size -= got;
        }
        return true;
    }

    bool Send(uint32_t op, const std::string& json) {
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        std::string frame(8, '\0');
        const uint32_t header[2] = {op, static_cast<uint32_t>(json.size())};
        std::memcpy(frame.data(), header, sizeof(header));
        frame += json;
        DWORD wrote = 0;
        return Transfer(true, frame.data(), static_cast<DWORD>(frame.size()), &wrote) && wrote == frame.size();
    }

    void Disconnect() {
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::string> want_;
    bool pending_ = false, stop_ = false;
    std::thread thread_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;  // the worker's only, as is event_
    HANDLE event_ = nullptr;
    uint64_t nonce_ = 0;
};

Pipe& ThePipe() {
    static Pipe p;
    return p;
}

// --- what to show ---------------------------------------------------------------------------

struct Shown {
    bool any = false;
    std::string key;        // everything but the clock
    int64_t startMs = 0;    // when the playing file "started", for Discord's elapsed time
};

Shown& Last() {
    static Shown s;
    return s;
}

std::string Clip(const std::wstring& text) {
    std::wstring t = text.size() > 120 ? text.substr(0, 119) + L"…" : text;
    if (t.size() < 2) t += L"  ";  // Discord wants at least 2 characters
    return json::ToUtf8(t);
}

void Update() {
    Config& c = TheApp().config();
    const std::string mode = c.GetString("privacy.discordStatus");
    const PlayerState& s = TheApp().player().State();
    const bool music = library::KindOf(s.path) == library::MediaKind::Audio;
    const bool show = mode != "off" && !net::keys::DiscordApp().empty() && s.fileLoaded && !s.path.empty() &&
                      (music || mode == "all");
    Shown& last = Last();
    if (!show) {
        if (last.any) {
            last = Shown{};
            ThePipe().Want(std::nullopt);
        }
        return;
    }
    std::wstring details, state, large;
    if (music) {
        const PlayingTags& t = CurrentTags();
        details = t.title.empty() ? library::StemOf(s.path) : t.title;
        state = t.artist;
        large = t.album;
    } else {
        const episodes::Episode e = episodes::Parse(s.path);
        details = !s.title.empty() ? s.title : library::StemOf(s.path);
        if (e.Valid()) state = episodes::Label(e);
    }
    if (s.paused) state = state.empty() ? std::wstring(Tr(L"Paused")) : state + L" · " + Tr(L"Paused");
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    const int64_t startMs = nowMs - static_cast<int64_t>(s.position * 1000);
    const std::string key = std::string(music ? "m" : "v") + (s.paused ? "p" : "") + Clip(details) + "\n" +
                            json::ToUtf8(state) + "\n" + json::ToUtf8(large) + "\n" + std::to_string(static_cast<int64_t>(s.duration));
    // The clock runs in Discord: only a seek (the start moves by more than 3 s) is news.
    if (last.any && key == last.key && (s.paused || std::llabs(startMs - last.startMs) < 3000)) return;
    last = {true, key, startMs};

    json::Value act = json::Value::MakeObject();
    act.Set("type", music ? 2 : 3);  // Listening, Watching
    act.Set("details", Clip(details));
    if (!state.empty()) act.Set("state", Clip(state));
    if (!s.paused && s.duration > 0) {
        json::Value ts = json::Value::MakeObject();
        ts.Set("start", startMs);
        ts.Set("end", startMs + static_cast<int64_t>(s.duration * 1000));
        act.Set("timestamps", std::move(ts));
    }
    json::Value assets = json::Value::MakeObject();
    assets.Set("large_image", "moonbow");  // an asset of the Discord application
    assets.Set("large_text", Clip(large.empty() ? std::wstring(L"Moonbow") : large));
    act.Set("assets", std::move(assets));
    act.Set("instance", false);
    ThePipe().Want(json::Serialize(act));
}

}  // namespace

void InitDiscordPresence() {
    TheApp().playerChanged.Connect([](unsigned changes) {
        if (changes & (kChangeFile | kChangeState | kChangeMeta | kChangeTime | kChangeEnd)) Update();
    });
    TheApp().config().Subscribe("privacy.discordStatus", [](const std::string&) { Update(); });
}

void ShutdownDiscordPresence() {
    ThePipe().Stop();
}

}  // namespace app
