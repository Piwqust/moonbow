#pragma once
// Small asynchronous HTTPS client for Moonbow's online features (LRCLIB, OpenSubtitles, TMDB).
//
// - WinHTTP on a pool of at most two worker threads. The threads start with the first request
//   and exit after 20 s without work, so a session that never goes online has no thread, no
//   window and no allocation here.
// - HTTPS only. Plain http is accepted for 127.0.0.1 / localhost in --automation runs (the
//   test mock servers) and nowhere else.
// - Responses come back on the UI thread: Send() must be called on the UI thread, and the
//   callback runs there from a message-only window's queue (ui::Host::Post is not
//   thread-safe, so the workers post to that window instead, like Thumbnails does).
// - gzip/deflate through WINHTTP_OPTION_DECOMPRESSION; the system proxy is used.
// - A disk cache (net/Cache.h) is read and written on the worker; a hit never touches the
//   network. A small in-memory LRU in front of it answers repeated lookups without a thread hop.
// - Connection failures (no route, DNS, refused, timeout) put the client "offline" for a minute:
//   background requests then fail at once without trying, user-initiated ones still try.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace net {

enum class Error : uint8_t {
    None,       // a response arrived (check status)
    Offline,    // not tried: a recent request could not connect
    Network,    // DNS, connect, TLS or transfer failure
    Timeout,
    TooLarge,   // the body exceeded Request::maxBytes
    Blocked,    // the URL is not https (or a host the caller refused)
    Cancelled,  // never reported to callbacks; Cancel() drops them
    File,       // Request::saveTo could not be written
};

struct Request {
    std::string method = "GET";
    std::wstring url;
    std::vector<std::pair<std::wstring, std::wstring>> headers;  // besides User-Agent / Accept-Encoding
    std::string body;            // POST body (sent as is)
    int timeoutMs = 15000;       // connect + each receive
    size_t maxBytes = 4u << 20;  // larger bodies fail with TooLarge
    bool user = false;           // the user asked for it: tried even while offline
    // Cache: when `cacheKey` is set, a fresh entry answers without the network and a 2xx answer
    // that is JSON (or a 404 when cache404 > 0) is stored for `cacheTtl` seconds.
    std::string cacheKey;
    int64_t cacheTtl = 0;
    int64_t cache404 = 0;
    // Download straight to this file (written as "<path>.part", renamed when complete). The
    // response body is then empty. Existing files are replaced.
    std::wstring saveTo;
    bool keepExisting = false;  // with saveTo: an existing file answers 200 without the network
    std::wstring userAgent;  // empty = "Moonbow/<version>"
    bool allowLoopbackHttp = false;  // http://127.0.0.1 (test mock servers, --automation only)
};

struct Response {
    Error error = Error::None;
    int status = 0;          // HTTP status when error == None
    bool fromCache = false;
    std::string body;
    std::map<std::string, std::string> headers;  // lower-case names
    unsigned long sysError = 0;                  // WinHTTP / Win32 code behind Network/Timeout/File

    bool Ok() const { return error == Error::None && status >= 200 && status < 300; }
    std::string Header(const std::string& lowerName) const {
        auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
};

using Callback = std::function<void(const Response&)>;

// UI thread. Returns an id for Cancel(); the callback runs later on the UI thread (never inside
// Send), exactly once unless cancelled.
uint64_t Send(Request request, Callback done);
void Cancel(uint64_t id);
// Runs `work` on a net worker and then `done` on the UI thread (file hashing, saving a
// download, loading a store). UI thread.
void Async(std::function<void()> work, std::function<void()> done = {});

// MOONBOW_TRACE: every request that reaches the network is logged to %TEMP%\moonbow-net-<pid>.txt as
// "<ms> <method> <host><path without the query> -> <status or error>" (no keys, no query values).

// Whether a recent request failed to connect (requests are skipped for a minute).
bool Offline();
// Requests queued or running (tests wait for 0).
size_t Pending();
// Counters for the write-up and tests: requests that reached the network, and cache hits.
uint64_t NetworkRequests();
uint64_t CacheHits();

// "Moonbow/0.1.0" from the VERSIONINFO resource ("Moonbow/?" without one, e.g. in unit tests).
const std::wstring& UserAgent();
std::wstring AppVersion();

// Percent-encoding for query values (UTF-8, unreserved characters kept). `plusForSpace`
// writes "+" for spaces (OpenSubtitles asks for it).
std::wstring UrlEncode(std::wstring_view text, bool plusForSpace = false);

// "https://host/path" checks, shared with the feature code: the scheme is https, or http to
// 127.0.0.1 / localhost when `allowLoopbackHttp`.
bool UrlAllowed(const std::wstring& url, bool allowLoopbackHttp);
std::wstring HostOf(const std::wstring& url);  // lower case, without port; empty when malformed

}  // namespace net
