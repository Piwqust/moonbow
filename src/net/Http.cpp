#include "Http.h"  // relative: the unit tests build it without /I src

#include "Cache.h"
#include "../core/Json.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#pragma comment(lib, "winhttp.lib")

namespace net {

namespace {

constexpr UINT kDoneMessage = WM_APP + 1;
constexpr int kMaxWorkers = 2;
constexpr auto kWorkerIdle = std::chrono::seconds(20);
constexpr int64_t kOfflineMs = 60000;
// Spacing between two requests to the same host: OpenSubtitles allows 5 per second per IP,
// TMDB about 50; one rule for all keeps it simple and polite.
constexpr int64_t kHostSpacingMs = 220;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Job {
    uint64_t id = 0;
    Request req;
    Callback cb;
    std::function<void()> work, done;  // Async() tasks
    Response resp;
};

struct State {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::shared_ptr<Job>> queue;
    std::vector<std::shared_ptr<Job>> completed;
    std::unordered_set<uint64_t> cancelled;
    int threads = 0, waiting = 0, running = 0;
    bool posted = false;
    HWND wnd = nullptr;
    HINTERNET session = nullptr;
    uint64_t nextId = 1;
    std::atomic<int64_t> offlineUntil{0};
    std::map<std::wstring, int64_t> hostNext;     // earliest start of the next request per host
    std::map<std::wstring, int64_t> hostBlocked;  // a 429 asked to wait until then
    std::atomic<uint64_t> networkRequests{0}, cacheHits{0};
};

// Never destroyed: detached workers may still run while the process exits.
State& S() {
    static State* s = new State;
    return *s;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg != kDoneMessage) return DefWindowProcW(hwnd, msg, wp, lp);
    State& s = S();
    std::vector<std::shared_ptr<Job>> done;
    {
        std::lock_guard lock(s.mu);
        done.swap(s.completed);
        s.posted = false;
    }
    for (auto& job : done) {
        bool dropped = false;
        {
            std::lock_guard lock(s.mu);
            dropped = s.cancelled.erase(job->id) > 0;
        }
        if (dropped) continue;
        if (job->work) {
            if (job->done) job->done();
        } else if (job->cb) {
            job->cb(job->resp);
        }
    }
    return 0;
}

HWND Window() {
    State& s = S();
    if (s.wnd) return s.wnd;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LumenNet";
    RegisterClassW(&wc);
    s.wnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return s.wnd;
}

// mutex held
void PostCompletedLocked(State& s) {
    if (s.posted || s.completed.empty() || !s.wnd) return;
    s.posted = PostMessageW(s.wnd, kDoneMessage, 0, 0) != 0;
}

HINTERNET Session() {
    State& s = S();
    std::lock_guard lock(s.mu);
    if (s.session) return s.session;
    s.session = WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s.session)  // before Windows 8.1
        s.session = WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (s.session) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(s.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(s.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
    }
    return s.session;
}

bool IsCancelled(uint64_t id) {
    State& s = S();
    std::lock_guard lock(s.mu);
    return s.cancelled.count(id) > 0;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void ParseHeaders(const std::wstring& raw, std::map<std::string, std::string>* out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t end = raw.find(L"\r\n", pos);
        if (end == std::wstring::npos) end = raw.size();
        const std::wstring line = raw.substr(pos, end - pos);
        pos = end + 2;
        const size_t colon = line.find(L':');
        if (colon == std::wstring::npos || colon == 0) continue;
        std::string name = Lower(json::ToUtf8(line.substr(0, colon)));
        std::wstring value = line.substr(colon + 1);
        while (!value.empty() && value.front() == L' ') value.erase(value.begin());
        (*out)[name] = json::ToUtf8(value);
    }
}

bool ConnectionError(DWORD e) {
    return e == ERROR_WINHTTP_NAME_NOT_RESOLVED || e == ERROR_WINHTTP_CANNOT_CONNECT ||
           e == ERROR_WINHTTP_CONNECTION_ERROR || e == ERROR_WINHTTP_TIMEOUT;
}

// Waits for the host's turn; false (with a synthesized 429) when a rate limit blocks it longer.
bool WaitForHost(const std::wstring& host, Response* resp) {
    State& s = S();
    int64_t wait = 0;
    {
        std::lock_guard lock(s.mu);
        const int64_t now = NowMs();
        auto blocked = s.hostBlocked.find(host);
        if (blocked != s.hostBlocked.end() && blocked->second > now + 1000) {
            resp->status = 429;
            resp->headers["retry-after"] = std::to_string((blocked->second - now + 999) / 1000);
            return false;
        }
        int64_t start = std::max(now, s.hostNext[host]);
        if (blocked != s.hostBlocked.end()) start = std::max(start, blocked->second);
        s.hostNext[host] = start + kHostSpacingMs;
        wait = start - now;
    }
    if (wait > 0) std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    return true;
}

void NoteRateLimit(const std::wstring& host, const Response& r) {
    if (r.status != 429) return;
    std::string v = r.Header("retry-after");
    if (v.empty()) v = r.Header("ratelimit-reset");
    int64_t seconds = std::clamp<int64_t>(v.empty() ? 1 : std::atoll(v.c_str()), 1, 3600);
    State& s = S();
    std::lock_guard lock(s.mu);
    s.hostBlocked[host] = NowMs() + seconds * 1000;
}

void TraceRequest(const std::wstring& method, const std::wstring& host, const std::wstring& path, const Response& r) {
    static const bool enabled = GetEnvironmentVariableW(L"LUMEN_TRACE", nullptr, 0) > 0;
    if (!enabled) return;
    static std::mutex mu;
    static const int64_t start = NowMs();
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) return;
    const std::wstring file = std::wstring(temp) + L"lumen-net-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    std::wstring shown = path.substr(0, path.find(L'?'));
    std::string line = std::to_string(NowMs() - start) + " " + json::ToUtf8(method) + " " + json::ToUtf8(host) +
                       json::ToUtf8(shown) + " -> " +
                       (r.error == Error::None ? std::to_string(r.status) : "error " + std::to_string(static_cast<int>(r.error))) +
                       "\r\n";
    std::lock_guard lock(mu);
    HANDLE f = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(f);
}

// Creates the missing folders above `file` (the image and subtitle folders on first use).
void EnsureParentDir(const std::wstring& file) {
    const size_t slash = file.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < 3) return;
    const std::wstring dir = file.substr(0, slash);
    const DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return;
    EnsureParentDir(dir);
    CreateDirectoryW(dir.c_str(), nullptr);
}

void Fetch(Job& job) {
    Request& rq = job.req;
    Response& resp = job.resp;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(rq.url.c_str(), static_cast<DWORD>(rq.url.size()), 0, &uc)) {
        resp.error = Error::Blocked;
        return;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    if (!WaitForHost(host, &resp)) return;
    if (IsCancelled(job.id)) {
        resp.error = Error::Cancelled;
        return;
    }
    HINTERNET session = Session();
    if (!session) {
        resp.error = Error::Network;
        resp.sysError = GetLastError();
        return;
    }
    ++S().networkRequests;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, json::ToWide(rq.method).c_str(), path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              secure ? WINHTTP_FLAG_SECURE : 0)
                       : nullptr;
    auto fail = [&](Error e) {
        const DWORD err = GetLastError();
        resp.error = e;
        resp.sysError = err;
        if (e == Error::Network && err == ERROR_WINHTTP_TIMEOUT) resp.error = Error::Timeout;
        if (ConnectionError(err)) S().offlineUntil = NowMs() + kOfflineMs;
    };
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring part;
    if (!req) {
        fail(Error::Network);
    } else {
        const int t = std::max(1000, rq.timeoutMs);
        WinHttpSetTimeouts(req, t, t, t, t);
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        std::wstring headers;
        if (!rq.userAgent.empty()) headers += L"User-Agent: " + rq.userAgent + L"\r\n";
        for (const auto& [name, value] : rq.headers) headers += name + L": " + value + L"\r\n";
        if (!headers.empty())
            WinHttpAddRequestHeaders(req, headers.c_str(), static_cast<DWORD>(-1),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        const DWORD bodyLen = static_cast<DWORD>(rq.body.size());
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyLen ? rq.body.data() : WINHTTP_NO_REQUEST_DATA,
                                bodyLen, bodyLen, 0) ||
            !WinHttpReceiveResponse(req, nullptr)) {
            fail(Error::Network);
        } else {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            resp.status = static_cast<int>(status);
            DWORD rawLen = 0;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &rawLen,
                                WINHTTP_NO_HEADER_INDEX);
            if (rawLen > 0) {
                std::wstring raw(rawLen / sizeof(wchar_t) + 1, L'\0');
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
                                        &rawLen, WINHTTP_NO_HEADER_INDEX)) {
                    raw.resize(rawLen / sizeof(wchar_t));
                    ParseHeaders(raw, &resp.headers);
                }
            }
            const bool toFile = !rq.saveTo.empty() && status >= 200 && status < 300;
            if (toFile) {
                part = rq.saveTo + L".part";
                EnsureParentDir(part);
                file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    resp.error = Error::File;
                    resp.sysError = GetLastError();
                }
            }
            size_t total = 0;
            std::vector<char> buf(64 * 1024);
            while (resp.error == Error::None) {
                DWORD read = 0;
                if (!WinHttpReadData(req, buf.data(), static_cast<DWORD>(buf.size()), &read)) {
                    fail(Error::Network);
                    break;
                }
                if (read == 0) break;
                total += read;
                if (total > rq.maxBytes) {
                    resp.error = Error::TooLarge;
                    break;
                }
                if (IsCancelled(job.id)) {
                    resp.error = Error::Cancelled;
                    break;
                }
                if (toFile) {
                    DWORD written = 0;
                    if (!WriteFile(file, buf.data(), read, &written, nullptr) || written != read) {
                        resp.error = Error::File;
                        resp.sysError = GetLastError();
                    }
                } else {
                    resp.body.append(buf.data(), read);
                }
            }
        }
    }
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        if (resp.error == Error::None) {
            if (!MoveFileExW(part.c_str(), rq.saveTo.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                resp.error = Error::File;
                resp.sysError = GetLastError();
            }
        }
        if (resp.error != Error::None) DeleteFileW(part.c_str());
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (resp.error == Error::None) {
        S().offlineUntil = 0;
        NoteRateLimit(host, resp);
    }
    TraceRequest(json::ToWide(rq.method), host, path, resp);
}

void Process(Job& job) {
    if (job.work) {
        job.work();
        return;
    }
    Request& rq = job.req;
    Response& resp = job.resp;
    if (!UrlAllowed(rq.url, rq.allowLoopbackHttp)) {
        resp.error = Error::Blocked;
        return;
    }
    if (!rq.cacheKey.empty()) {
        CacheEntry e;
        if (SharedDisk().Get(rq.cacheKey, &e)) {
            resp.status = e.status;
            resp.body = std::move(e.body);
            resp.fromCache = true;
            ++S().cacheHits;
            SharedMemory().Put(rq.cacheKey, CacheEntry{resp.status, e.expires, resp.body});
            return;
        }
    }
    if (rq.keepExisting && !rq.saveTo.empty()) {
        const DWORD attr = GetFileAttributesW(rq.saveTo.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            resp.status = 200;
            resp.fromCache = true;
            ++S().cacheHits;
            return;
        }
    }
    if (!rq.user && NowMs() < S().offlineUntil.load()) {
        resp.error = Error::Offline;
        return;
    }
    Fetch(job);
    if (resp.error != Error::None || rq.cacheKey.empty()) return;
    const int64_t ttl = resp.Ok() ? rq.cacheTtl : resp.status == 404 ? rq.cache404 : 0;
    if (ttl > 0) {
        SharedDisk().Put(rq.cacheKey, resp.status, resp.body, ttl);
        SharedMemory().Put(rq.cacheKey, CacheEntry{resp.status, UnixNow() + ttl, resp.body});
    }
}

void Worker() {
    State& s = S();
    std::unique_lock lock(s.mu);
    for (;;) {
        if (s.queue.empty()) {
            ++s.waiting;
            const bool woke = s.cv.wait_for(lock, kWorkerIdle, [&s] { return !s.queue.empty(); });
            --s.waiting;
            if (!woke) {
                --s.threads;
                // The session stays open: WinHttpOpen is cheap to keep and costly to repeat.
                return;
            }
        }
        std::shared_ptr<Job> job = s.queue.front();
        s.queue.pop_front();
        ++s.running;
        const bool cancelled = s.cancelled.count(job->id) > 0;
        lock.unlock();
        if (!cancelled) Process(*job);
        lock.lock();
        --s.running;
        s.completed.push_back(std::move(job));
        PostCompletedLocked(s);
    }
}

// mutex held
void WakeWorkerLocked(State& s) {
    if (s.waiting > 0) {
        s.cv.notify_one();
    } else if (s.threads < kMaxWorkers) {
        ++s.threads;
        std::thread(Worker).detach();
    }
}

uint64_t Enqueue(std::shared_ptr<Job> job) {
    State& s = S();
    Window();
    std::lock_guard lock(s.mu);
    job->id = s.nextId++;
    s.queue.push_back(job);
    WakeWorkerLocked(s);
    return job->id;
}

}  // namespace

uint64_t Send(Request request, Callback done) {
    auto job = std::make_shared<Job>();
    job->req = std::move(request);
    job->cb = std::move(done);
    // A memory hit skips the worker but still answers through the queue, never inside Send.
    CacheEntry e;
    if (!job->req.cacheKey.empty() && SharedMemory().Get(job->req.cacheKey, &e)) {
        State& s = S();
        Window();
        job->resp.status = e.status;
        job->resp.body = std::move(e.body);
        job->resp.fromCache = true;
        ++s.cacheHits;
        std::lock_guard lock(s.mu);
        job->id = s.nextId++;
        s.completed.push_back(job);
        PostCompletedLocked(s);
        return job->id;
    }
    return Enqueue(std::move(job));
}

void Async(std::function<void()> work, std::function<void()> done) {
    auto job = std::make_shared<Job>();
    job->work = work ? std::move(work) : [] {};
    job->done = std::move(done);
    Enqueue(std::move(job));
}

void Cancel(uint64_t id) {
    if (!id) return;
    State& s = S();
    std::lock_guard lock(s.mu);
    s.cancelled.insert(id);
}

bool Offline() {
    return NowMs() < S().offlineUntil.load();
}

size_t Pending() {
    State& s = S();
    std::lock_guard lock(s.mu);
    return s.queue.size() + static_cast<size_t>(s.running) + s.completed.size();
}

uint64_t NetworkRequests() {
    return S().networkRequests.load();
}

uint64_t CacheHits() {
    return S().cacheHits.load();
}

std::wstring AppVersion() {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));  // VS_VERSION_INFO, RT_VERSION
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const auto* data = mem ? static_cast<const BYTE*>(LockResource(mem)) : nullptr;
    const DWORD size = res ? SizeofResource(nullptr, res) : 0;
    for (DWORD i = 0; data && i + 16 <= size; i += 4) {
        DWORD sig = 0;
        memcpy(&sig, data + i, 4);
        if (sig != 0xFEEF04BD) continue;  // VS_FIXEDFILEINFO
        DWORD ms = 0, ls = 0;
        memcpy(&ms, data + i + 8, 4);
        memcpy(&ls, data + i + 12, 4);
        return std::to_wstring(HIWORD(ms)) + L"." + std::to_wstring(LOWORD(ms)) + L"." + std::to_wstring(HIWORD(ls));
    }
    return L"?";
}

const std::wstring& UserAgent() {
    static const std::wstring ua = L"Lumen/" + AppVersion();
    return ua;
}

std::wstring UrlEncode(std::wstring_view text, bool plusForSpace) {
    static const char* hex = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : json::ToUtf8(text)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else if (c == ' ' && plusForSpace) {
            out.push_back(L'+');
        } else {
            out.push_back(L'%');
            out.push_back(static_cast<wchar_t>(hex[c >> 4]));
            out.push_back(static_cast<wchar_t>(hex[c & 15]));
        }
    }
    return out;
}

std::wstring HostOf(const std::wstring& url) {
    const size_t scheme = url.find(L"://");
    if (scheme == std::wstring::npos) return {};
    size_t start = scheme + 3;
    size_t end = url.find_first_of(L"/?#", start);
    std::wstring host = url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (host.find(L'@') != std::wstring::npos) return {};  // no credentials in URLs
    if (const size_t colon = host.rfind(L':'); colon != std::wstring::npos) host.resize(colon);
    for (wchar_t& c : host) c = static_cast<wchar_t>(towlower(c));
    return host;
}

bool UrlAllowed(const std::wstring& url, bool allowLoopbackHttp) {
    const std::wstring host = HostOf(url);
    if (host.empty()) return false;
    if (_wcsnicmp(url.c_str(), L"https://", 8) == 0) return true;
    return allowLoopbackHttp && _wcsnicmp(url.c_str(), L"http://", 7) == 0 && (host == L"127.0.0.1" || host == L"localhost");
}

}  // namespace net
