#include "app/player/IntroFinder.h"

#include "Player.h"
#include "Trace.h"
#include "Util.h"
#include "app/App.h"
#include "app/player/Episodes.h"
#include "audio/IntroPrint.h"
#include "core/Config.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "library/MediaTypes.h"

#include <windows.h>

#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace app::intros {

namespace {

constexpr double kScanSeconds = 600;  // the intro is in the first 10 minutes...
constexpr double kScanShare = 0.45;   // ...and in the first half: shared end credits are not it
constexpr double kScanAtLeast = 180;  // short episodes: their first three minutes all the same
constexpr size_t kMaxEntries = 4000;

struct Entry {
    std::wstring path;
    uint64_t size = 0;
    bool found = false;
    Range range;
};

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return 0;
    return (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}

std::wstring TempPcm() {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    static LONG counter = 0;
    wchar_t name[96];
    swprintf_s(name, L"moonbow-intro-%lu-%ld.pcm", GetCurrentProcessId(), InterlockedIncrement(&counter));
    return std::wstring(dir) + name;
}

class Finder {
public:
    ~Finder() {
        {
            std::lock_guard lock(mu_);
            quit_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void Load() {
        std::string text;
        if (!json::ReadFileUtf8(DataFilePath(L"intros.json"), &text)) return;
        const auto doc = json::Parse(text);
        if (!doc) return;
        std::lock_guard lock(mu_);
        if (const json::Value* files = doc->Find("files"))
            for (const json::Value& v : files->AsArray()) {
                Entry e;
                e.path = v.GetWString("path");
                e.size = static_cast<uint64_t>(v.GetNumber("size"));
                e.found = !v.GetBool("none");
                e.range = {v.GetNumber("start"), v.GetNumber("end")};
                if (!e.path.empty() && (!e.found || e.range.end > e.range.start))
                    store_[library::PathKey(e.path)] = std::move(e);
            }
    }

    std::optional<Range> Find(const std::wstring& path) {
        const std::wstring key = library::PathKey(path);
        std::lock_guard lock(mu_);
        if (const auto it = store_.find(key); it != store_.end()) {
            // Once a session, the file is checked for being the one the result is of.
            if (!checked_.count(key)) {
                checked_.insert(key);
                if (it->second.size != FileSize(path)) {
                    store_.erase(it);
                    Request(path, key);
                    return std::nullopt;
                }
            }
            if (it->second.found) return it->second.range;
            return std::nullopt;
        }
        Request(path, key);
        return std::nullopt;
    }

private:
    // UI thread, mu_ held: queue a comparison with a neighbouring episode, once a session.
    void Request(const std::wstring& path, const std::wstring& key) {
        if (!asked_.insert(key).second) return;
        const std::vector<std::wstring> siblings = ListSiblingMedia(path);
        std::wstring neighbour;
        if (const int next = episodes::FindNext(path, siblings); next >= 0) neighbour = siblings[static_cast<size_t>(next)];
        if (neighbour.empty())
            for (const std::wstring& s : siblings)
                if (episodes::IsNext(s, path)) neighbour = s;
        if (neighbour.empty()) return;  // a lone episode: nothing to compare with
        jobs_.push_back({path, neighbour});
        if (!worker_.joinable()) worker_ = std::thread([this] { Run(); });
        wake_.notify_all();
    }

    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock lock(mu_);
        for (;;) {
            wake_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            const auto [a, b] = jobs_.front();
            jobs_.pop_front();
            lock.unlock();
            Compare(a, b);
            lock.lock();
        }
    }

    // Worker: both prints, the stretch they share, the results for both files.
    void Compare(const std::wstring& a, const std::wstring& b) {
        Trace("intro: compare");
        const std::shared_ptr<audio::intro::Print> pa = PrintOf(a), pb = pa ? PrintOf(b) : nullptr;
        if (!pa || !pb) return;  // unreadable, or quitting: asked again next session
        const audio::intro::Shared s = audio::intro::FindShared(*pa, *pb);
        char line[200];
        snprintf(line, sizeof line, "intro: %.1f-%.1f s (and %.1f-%.1f s in the neighbour)", s.startA, s.endA, s.startB,
                 s.endB);
        Log(s.Length() > 0 ? line : "intro: none shared with the neighbour");
        {
            std::lock_guard lock(mu_);
            Put(a, s.Length() > 0, {s.startA, s.endA});
            if (s.Length() > 0) Put(b, true, {s.startB, s.endB});
        }
        Save();
    }

    void Put(const std::wstring& path, bool found, Range r) {  // mu_ held
        if (store_.size() >= kMaxEntries) store_.erase(store_.begin());
        Entry e{path, FileSize(path), found, r};
        const std::wstring key = library::PathKey(path);
        checked_.insert(key);
        store_[key] = std::move(e);
    }

    void Save() {
        json::Value files = json::Value::MakeArray();
        {
            std::lock_guard lock(mu_);
            for (const auto& [key, e] : store_) {
                json::Value v = json::Value::MakeObject();
                v.Set("path", json::ToUtf8(e.path));
                v.Set("size", e.size);
                if (e.found) {
                    v.Set("start", std::round(e.range.start * 10) / 10);
                    v.Set("end", std::round(e.range.end * 10) / 10);
                } else {
                    v.Set("none", true);
                }
                files.Push(std::move(v));
            }
        }
        json::Value doc = json::Value::MakeObject();
        doc.Set("version", 1);
        doc.Set("files", std::move(files));
        json::WriteFileAtomic(DataFilePath(L"intros.json"), json::Serialize(doc));
    }

    // Worker: a file's print, from the last few made or decoded now.
    std::shared_ptr<audio::intro::Print> PrintOf(const std::wstring& path) {
        const std::wstring key = library::PathKey(path);
        for (const auto& [k, p] : prints_)
            if (k == key) return p;
        std::vector<int16_t> pcm = Decode(path);
        if (pcm.empty()) return nullptr;
        auto print = std::make_shared<audio::intro::Print>(audio::intro::Fingerprint(pcm.data(), pcm.size()));
        prints_.push_back({key, print});
        if (prints_.size() > 3) prints_.pop_front();
        return print;
    }

    // Worker: the opening minutes as mono 16-bit samples at the print's rate, decoded by a helper
    // mpv faster than real time (as audio/TrackWave does for songs).
    std::vector<int16_t> Decode(const std::wstring& path) {
        const MpvApi& api = TheApp().player().Api();
        mpv_handle* h = api.mpv_create ? api.mpv_create() : nullptr;
        if (!h) return {};
        const std::wstring pcm = TempPcm();
        SetQuietMpvOptions(api, h);
        auto opt = [&](const char* name, const std::string& value) { api.mpv_set_option_string(h, name, value.c_str()); };
        for (const char* name : {"vid", "sid", "audio-display", "keep-open", "idle", "gapless-audio",
                                 "prefetch-playlist", "replaygain"})
            opt(name, "no");
        opt("vo", "null");
        opt("ao", "pcm");
        opt("ao-pcm-file", WideToUtf8(pcm));
        opt("ao-pcm-waveheader", "no");
        opt("untimed", "yes");
        opt("audio-format", "s16");
        opt("audio-channels", "mono");
        opt("audio-samplerate", std::to_string(audio::intro::kRate));
        opt("end", std::to_string(static_cast<int>(kScanSeconds)));
        bool ok = api.mpv_initialize(h) >= 0;
        if (ok) {
            const std::string file = WideToUtf8(path);
            const char* load[] = {"loadfile", file.c_str(), "replace", nullptr};
            ok = api.mpv_command(h, load) >= 0;
        }
        double duration = 0;
        const ULONGLONG deadline = GetTickCount64() + 120000;
        while (ok) {
            const mpv_event* ev = api.mpv_wait_event(h, 0.2);
            if (ev->event_id == MPV_EVENT_FILE_LOADED) api.mpv_get_property(h, "duration", MPV_FORMAT_DOUBLE, &duration);
            if (ev->event_id == MPV_EVENT_END_FILE) {
                const auto* end = static_cast<const mpv_event_end_file*>(ev->data);
                ok = end && end->reason == MPV_END_FILE_REASON_EOF;
                break;
            }
            if (ev->event_id == MPV_EVENT_SHUTDOWN) ok = false;
            std::lock_guard lock(mu_);
            if (quit_ || GetTickCount64() > deadline) ok = false;
        }
        api.mpv_terminate_destroy(h);  // closes the PCM file
        std::vector<int16_t> samples;
        if (ok) {
            FILE* f = nullptr;
            if (_wfopen_s(&f, pcm.c_str(), L"rb") == 0 && f) {
                int16_t buf[32768];
                size_t got;
                while ((got = fread(buf, sizeof(int16_t), 32768, f)) > 0) samples.insert(samples.end(), buf, buf + got);
                fclose(f);
            }
            // Only the first part of the episode: end credits are shared too, and longer.
            if (duration > 0) {
                const double seconds = std::min(kScanSeconds, std::max(duration * kScanShare, kScanAtLeast));
                const size_t keep = static_cast<size_t>(seconds * audio::intro::kRate);
                if (samples.size() > keep) samples.resize(keep);
            }
        }
        DeleteFileW(pcm.c_str());
        return samples;
    }

    std::mutex mu_;
    std::condition_variable wake_;
    bool quit_ = false;
    std::thread worker_;
    std::deque<std::pair<std::wstring, std::wstring>> jobs_;
    std::map<std::wstring, Entry> store_;       // PathKey -> result
    std::set<std::wstring> asked_, checked_;    // this session: compared / size verified
    std::deque<std::pair<std::wstring, std::shared_ptr<audio::intro::Print>>> prints_;  // worker only
};

std::unique_ptr<Finder> g_finder;

Finder& TheFinder() {
    if (!g_finder) g_finder = std::make_unique<Finder>();
    return *g_finder;
}

bool On() {
    return TheApp().config().GetBool("playback.findIntros");
}

}  // namespace

void Init() {
    TheFinder().Load();
}

void Shutdown() {
    g_finder.reset();  // the worker notices within 0.2 s and closes its mpv
}

std::optional<Range> Find(const std::wstring& path) {
    if (!g_finder || !On() || path.empty() || path.find(L"://") != std::wstring::npos) return std::nullopt;
    if (!episodes::Parse(path).Valid()) return std::nullopt;
    return TheFinder().Find(path);
}

}  // namespace app::intros
