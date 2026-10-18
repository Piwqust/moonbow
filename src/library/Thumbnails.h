#pragma once
// Asynchronous thumbnails and cover art from the Windows shell.
//
// Request(path, size, cookie) queues work for a pool of COM MTA worker threads that call
// IShellItemImageFactory::GetImage(SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK |
// SIIGBF_THUMBNAILONLY). Icons are never returned: files without a real thumbnail (or embedded
// cover; for audio also cover.jpg / folder.jpg next to the file) complete with a null bitmap.
// Results are premultiplied BGRA (ready for ID2D1RenderTarget::CreateBitmap with
// DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED), at most `size` px on the long side.
//
// Completion posts the notify message once per batch; the UI thread then calls TakeCompleted().
// Identical requests in flight share one job; finished bitmaps stay in an LRU cache capped by
// memory (256 MB of pixels by default); requests the UI no longer needs can be cancelled.
//
// Threading: Request / Cancel* / TakeCompleted / Cached are called from the UI thread (they are
// internally synchronized, so other threads work too). Workers start on the first request.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace library {

struct ThumbnailBitmap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;  // premultiplied BGRA, top-down, stride = width * 4

    size_t Bytes() const { return pixels.size(); }
};

struct ThumbnailResult {
    uint64_t cookie = 0;  // what the requester passed in
    std::wstring path;
    int size = 0;
    std::shared_ptr<const ThumbnailBitmap> bitmap;  // null = no thumbnail for this file
};

class Thumbnails {
public:
    explicit Thumbnails(int workers = 2);
    ~Thumbnails();  // stops and joins the workers
    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;

    void SetNotifyWindow(HWND hwnd, UINT msg);  // PostMessageW(hwnd, msg, 0, 0) per batch
    void SetMemoryLimit(size_t bytes);
    size_t MemoryUsed() const;

    // Fast path for painting: the cached bitmap, or null. Marks it recently used.
    std::shared_ptr<const ThumbnailBitmap> Cached(const std::wstring& path, int size);
    // True when the file is known to have no thumbnail at this size (no need to request).
    bool KnownMissing(const std::wstring& path, int size) const;

    // Always completes through TakeCompleted() (immediately when cached). The cookie lets the
    // UI match results to tiles; the same cookie may be reused for a new path.
    void Request(const std::wstring& path, int size, uint64_t cookie);

    void Cancel(uint64_t cookie);
    // Drops every pending request whose cookie is not in `keep` (call after scrolling).
    void CancelAllExcept(const std::unordered_set<uint64_t>& keep);
    void CancelAll();

    std::vector<ThumbnailResult> TakeCompleted();
    size_t PendingCount() const;  // queued + running jobs
    void ClearCache();

private:
    struct Job {
        std::wstring key;
        std::wstring path;
        int size = 0;
        std::vector<uint64_t> cookies;
        bool running = false;
    };
    struct CacheEntry {
        std::shared_ptr<const ThumbnailBitmap> bitmap;
        std::list<std::wstring>::iterator lru;
    };

    void StartWorkers();  // mutex_ held
    void Worker();
    void Complete(const std::shared_ptr<Job>& job, std::shared_ptr<const ThumbnailBitmap> bitmap);  // mutex_ held
    void PostIfNeeded();  // mutex_ held
    void Insert(const std::wstring& key, std::shared_ptr<const ThumbnailBitmap> bitmap);  // mutex_ held
    void Evict();          // mutex_ held
    void DropCookies(const std::function<bool(uint64_t)>& drop);

    int workerCount_;
    std::vector<std::thread> workers_;
    bool stop_ = false;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::unordered_map<std::wstring, std::shared_ptr<Job>> inflight_;  // queued or running
    std::vector<ThumbnailResult> completed_;
    bool posted_ = false;

    std::unordered_map<std::wstring, CacheEntry> cache_;
    std::list<std::wstring> lru_;  // front = most recent
    std::unordered_set<std::wstring> missing_;
    size_t bytes_ = 0;
    size_t limit_ = 256u << 20;

    HWND notifyWnd_ = nullptr;
    UINT notifyMsg_ = 0;
};

// Synchronous extraction used by the workers (exposed for tests and one-off callers). The
// calling thread must have COM initialized. Null when the shell has no thumbnail.
std::shared_ptr<ThumbnailBitmap> ExtractThumbnail(const std::wstring& path, int size);

// Image source for the frame of a video at a moment ("<path>|<seconds>"), for Request/Cached like a
// path: Continue Watching shows where playback stopped, not the shell's own frame.
std::wstring FramePath(const std::wstring& path, double seconds);

}  // namespace library
