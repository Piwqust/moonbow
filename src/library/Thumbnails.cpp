#include "Thumbnails.h"

#include "MediaTypes.h"

#include <objbase.h>
#include <shobjidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <mutex>

// Media Foundation GUIDs only (a static library); the DLLs are loaded on first use.
#pragma comment(lib, "mfuuid.lib")

namespace library {

namespace {

constexpr size_t kMaxMissing = 20000;

std::wstring JobKey(const std::wstring& path, int size) {
    return PathKey(path) + L"|" + std::to_wstring(size);
}

std::shared_ptr<ThumbnailBitmap> FromHBitmap(HBITMAP hbm) {
    BITMAP bm{};
    if (!GetObjectW(hbm, sizeof(bm), &bm)) return nullptr;
    int w = bm.bmWidth, h = std::abs(bm.bmHeight);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return nullptr;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    auto out = std::make_shared<ThumbnailBitmap>();
    out->width = static_cast<uint32_t>(w);
    out->height = static_cast<uint32_t>(h);
    out->pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    HDC dc = CreateCompatibleDC(nullptr);
    int lines = GetDIBits(dc, hbm, 0, static_cast<UINT>(h), out->pixels.data(), &bi, DIB_RGB_COLORS);
    DeleteDC(dc);
    if (lines != h) return nullptr;

    // Shell bitmaps are 32-bit with premultiplied alpha, except opaque sources (JPEG frames)
    // that often come with alpha = 0 everywhere: treat those as opaque.
    uint8_t* p = out->pixels.data();
    size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    bool hasAlpha = false;
    if (bm.bmBitsPixel == 32)
        for (size_t i = 0; i < n && !hasAlpha; ++i) hasAlpha = p[i * 4 + 3] != 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* px = p + i * 4;
        if (!hasAlpha) {
            px[3] = 255;
        } else {
            px[0] = std::min(px[0], px[3]);  // keep it valid premultiplied data
            px[1] = std::min(px[1], px[3]);
            px[2] = std::min(px[2], px[3]);
        }
    }
    return out;
}

// Box filter; fine for the modest downscales BIGGERSIZEOK produces.
std::shared_ptr<ThumbnailBitmap> Downscale(const ThumbnailBitmap& src, int size) {
    double scale = static_cast<double>(size) / static_cast<double>(std::max(src.width, src.height));
    uint32_t nw = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(src.width * scale)));
    uint32_t nh = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(src.height * scale)));
    auto out = std::make_shared<ThumbnailBitmap>();
    out->width = nw;
    out->height = nh;
    out->pixels.resize(static_cast<size_t>(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y) {
        uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * src.height / nh);
        uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * src.height / nh));
        for (uint32_t x = 0; x < nw; ++x) {
            uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(x) * src.width / nw);
            uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * src.width / nw));
            uint32_t sum[4] = {};
            for (uint32_t sy = y0; sy < y1; ++sy) {
                const uint8_t* row = src.pixels.data() + (static_cast<size_t>(sy) * src.width + x0) * 4;
                for (uint32_t sx = x0; sx < x1; ++sx, row += 4)
                    for (int c = 0; c < 4; ++c) sum[c] += row[c];
            }
            uint32_t count = (y1 - y0) * (x1 - x0);
            uint8_t* d = out->pixels.data() + (static_cast<size_t>(y) * nw + x) * 4;
            for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
        }
    }
    return out;
}

std::shared_ptr<ThumbnailBitmap> FromShell(const std::wstring& path, int size) {
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return nullptr;
    HBITMAP hbm = nullptr;
    HRESULT hr = factory->GetImage(SIZE{size, size}, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY,
                                   &hbm);
    factory->Release();
    if (FAILED(hr) || !hbm) return nullptr;
    std::shared_ptr<ThumbnailBitmap> bmp = FromHBitmap(hbm);
    DeleteObject(hbm);
    if (bmp && (bmp->width > static_cast<uint32_t>(size) || bmp->height > static_cast<uint32_t>(size)))
        bmp = Downscale(*bmp, size);
    return bmp;
}

// --- a frame at a given time (Media Foundation) ---------------------------------------------

struct MfApi {
    HRESULT(WINAPI* createAttributes)(IMFAttributes**, UINT32) = nullptr;
    HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* createReader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) = nullptr;
    bool ok = false;
};

// Loaded and started once, on a worker thread: Media Foundation costs nothing until a card
// asks for a frame.
const MfApi& Mf() {
    static MfApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !rw) return;
        auto startup = reinterpret_cast<HRESULT(WINAPI*)(ULONG, DWORD)>(GetProcAddress(plat, "MFStartup"));
        api.createAttributes = reinterpret_cast<decltype(api.createAttributes)>(GetProcAddress(plat, "MFCreateAttributes"));
        api.createMediaType = reinterpret_cast<decltype(api.createMediaType)>(GetProcAddress(plat, "MFCreateMediaType"));
        api.createReader = reinterpret_cast<decltype(api.createReader)>(GetProcAddress(rw, "MFCreateSourceReaderFromURL"));
        api.ok = startup && api.createAttributes && api.createMediaType && api.createReader &&
                 SUCCEEDED(startup(MF_VERSION, MFSTARTUP_LITE));
    });
    return api;
}

// The video frame within a second of `seconds` (decoding on from the key frame before it, at most
// 240 frames), as BGRA; null when Media Foundation can't decode the file (then the shell's
// thumbnail is used).
std::shared_ptr<ThumbnailBitmap> FrameAt(const std::wstring& path, double seconds, int size) {
    using Microsoft::WRL::ComPtr;
    const MfApi& mf = Mf();
    if (!mf.ok) return nullptr;
    ComPtr<IMFAttributes> attrs;
    if (FAILED(mf.createAttributes(&attrs, 1))) return nullptr;
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);  // decode to RGB32
    ComPtr<IMFSourceReader> reader;
    if (FAILED(mf.createReader(path.c_str(), attrs.Get(), &reader))) return nullptr;
    const DWORD video = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(video, TRUE))) return nullptr;
    ComPtr<IMFMediaType> want;
    if (FAILED(mf.createMediaType(&want))) return nullptr;
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(video, nullptr, want.Get()))) return nullptr;
    ComPtr<IMFMediaType> got;
    if (FAILED(reader->GetCurrentMediaType(video, &got))) return nullptr;
    UINT32 w = 0, h = 0;
    if (FAILED(MFGetAttributeSize(got.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return nullptr;
    const LONGLONG target = static_cast<LONGLONG>(std::max(0.0, seconds) * 1e7);
    PROPVARIANT pos;
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = target;
    reader->SetCurrentPosition(GUID_NULL, pos);  // lands on the key frame before the target
    ComPtr<IMFSample> sample;
    for (int i = 0; i < 240; ++i) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> s;
        if (FAILED(reader->ReadSample(video, 0, nullptr, &flags, &ts, &s))) break;
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
        if (!s) continue;
        sample = s;
        if (ts + 10000000 >= target) break;  // within a second of the moment
    }
    if (!sample) return nullptr;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return nullptr;
    auto frame = std::make_shared<ThumbnailBitmap>();
    frame->width = w;
    frame->height = h;
    frame->pixels.resize(static_cast<size_t>(w) * h * 4);
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> buffer2d;
    bool locked2d = SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch));
    BYTE* data = nullptr;
    DWORD length = 0;
    if (!locked2d) {
        if (FAILED(buffer->Lock(&data, nullptr, &length))) return nullptr;
        UINT32 stride = 0;
        pitch = SUCCEEDED(got->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride) : static_cast<LONG>(w * 4);
        scan0 = pitch < 0 ? data + static_cast<size_t>(-pitch) * (h - 1) : data;
        if (static_cast<size_t>(std::abs(pitch)) * h > length) {
            buffer->Unlock();
            return nullptr;
        }
    }
    for (UINT32 y = 0; y < h; ++y) {
        const BYTE* src = scan0 + static_cast<ptrdiff_t>(pitch) * static_cast<ptrdiff_t>(y);
        uint8_t* dst = frame->pixels.data() + static_cast<size_t>(y) * w * 4;
        memcpy(dst, src, static_cast<size_t>(w) * 4);
        for (UINT32 x = 0; x < w; ++x) dst[x * 4 + 3] = 255;  // RGB32's fourth byte is padding
    }
    if (locked2d)
        buffer2d->Unlock2D();
    else
        buffer->Unlock();
    if (frame->width > static_cast<uint32_t>(size) || frame->height > static_cast<uint32_t>(size)) return Downscale(*frame, size);
    return frame;
}

}  // namespace

std::wstring FramePath(const std::wstring& path, double seconds) {
    // Whole 5-second steps: the position saved every few seconds doesn't make a new image each time.
    const long long s = static_cast<long long>(std::max(0.0, seconds) / 5) * 5;
    return path + L"|" + std::to_wstring(s);
}

std::shared_ptr<ThumbnailBitmap> ExtractThumbnail(const std::wstring& path, int size) {
    if (size <= 0) return nullptr;
    // "<file>|<seconds>": that moment of the video (Continue Watching), else its usual thumbnail.
    if (const size_t bar = path.rfind(L'|'); bar != std::wstring::npos) {
        const std::wstring file = path.substr(0, bar);
        if (auto frame = FrameAt(file, _wtof(path.c_str() + bar + 1), size)) return frame;
        return ExtractThumbnail(file, size);
    }
    std::shared_ptr<ThumbnailBitmap> bmp = FromShell(path, size);
    if (bmp || KindOf(path) != MediaKind::Audio) return bmp;
    // No embedded cover: look for the usual album art files next to the song.
    std::wstring folder = ParentFolder(path);
    for (const wchar_t* name : {L"cover.jpg", L"folder.jpg", L"front.jpg", L"cover.png", L"folder.png", L"AlbumArt.jpg"}) {
        std::wstring candidate = folder + (folder.empty() || folder.back() == L'\\' ? L"" : L"\\") + name;
        DWORD attr = GetFileAttributesW(candidate.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if ((bmp = FromShell(candidate, size))) return bmp;
    }
    return nullptr;
}

Thumbnails::Thumbnails(int workers) : workerCount_(std::clamp(workers, 1, 8)) {}

Thumbnails::~Thumbnails() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();
}

void Thumbnails::SetNotifyWindow(HWND hwnd, UINT msg) {
    std::lock_guard lock(mutex_);
    notifyWnd_ = hwnd;
    notifyMsg_ = msg;
}

void Thumbnails::SetMemoryLimit(size_t bytes) {
    std::lock_guard lock(mutex_);
    limit_ = bytes;
    Evict();
}

size_t Thumbnails::MemoryUsed() const {
    std::lock_guard lock(mutex_);
    return bytes_;
}

std::shared_ptr<const ThumbnailBitmap> Thumbnails::Cached(const std::wstring& path, int size) {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(JobKey(path, size));
    if (it == cache_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.bitmap;
}

bool Thumbnails::KnownMissing(const std::wstring& path, int size) const {
    std::lock_guard lock(mutex_);
    return missing_.count(JobKey(path, size)) != 0;
}

void Thumbnails::Request(const std::wstring& path, int size, uint64_t cookie) {
    std::wstring key = JobKey(path, size);
    std::lock_guard lock(mutex_);

    // A cookie belongs to one request at a time: a reused tile must not get the old picture.
    for (auto& [k, job] : inflight_)
        if (k != key) std::erase(job->cookies, cookie);

    auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, cached->second.lru);
        completed_.push_back(ThumbnailResult{cookie, path, size, cached->second.bitmap});
        PostIfNeeded();
        return;
    }
    if (missing_.count(key)) {
        completed_.push_back(ThumbnailResult{cookie, path, size, nullptr});
        PostIfNeeded();
        return;
    }
    auto in = inflight_.find(key);
    if (in != inflight_.end()) {
        auto& cookies = in->second->cookies;
        if (std::find(cookies.begin(), cookies.end(), cookie) == cookies.end()) cookies.push_back(cookie);
        return;
    }
    auto job = std::make_shared<Job>();
    job->key = key;
    job->path = path;
    job->size = size;
    job->cookies.push_back(cookie);
    inflight_.emplace(key, job);
    queue_.push_back(std::move(job));
    StartWorkers();
    cv_.notify_one();
}

void Thumbnails::DropCookies(const std::function<bool(uint64_t)>& drop) {
    std::lock_guard lock(mutex_);
    for (auto& [key, job] : inflight_) std::erase_if(job->cookies, drop);
    // Jobs nobody waits for and that have not started yet are dropped from the queue.
    std::erase_if(queue_, [&](const std::shared_ptr<Job>& job) {
        if (!job->cookies.empty() || job->running) return false;
        inflight_.erase(job->key);
        return true;
    });
}

void Thumbnails::Cancel(uint64_t cookie) {
    DropCookies([cookie](uint64_t c) { return c == cookie; });
}

void Thumbnails::CancelAllExcept(const std::unordered_set<uint64_t>& keep) {
    DropCookies([&keep](uint64_t c) { return keep.count(c) == 0; });
}

void Thumbnails::CancelAll() {
    DropCookies([](uint64_t) { return true; });
}

std::vector<ThumbnailResult> Thumbnails::TakeCompleted() {
    std::lock_guard lock(mutex_);
    std::vector<ThumbnailResult> out;
    out.swap(completed_);
    posted_ = false;
    return out;
}

size_t Thumbnails::PendingCount() const {
    std::lock_guard lock(mutex_);
    return inflight_.size();
}

void Thumbnails::ClearCache() {
    std::lock_guard lock(mutex_);
    cache_.clear();
    lru_.clear();
    missing_.clear();
    bytes_ = 0;
}

void Thumbnails::StartWorkers() {
    if (!workers_.empty() || stop_) return;
    for (int i = 0; i < workerCount_; ++i) {
        try {
            workers_.emplace_back(&Thumbnails::Worker, this);
        } catch (...) {
            break;  // run with fewer workers
        }
    }
}

void Thumbnails::Worker() {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    std::unique_lock lock(mutex_);
    while (true) {
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) break;
        std::shared_ptr<Job> job = std::move(queue_.front());
        queue_.pop_front();
        if (job->cookies.empty()) {  // cancelled while queued
            inflight_.erase(job->key);
            continue;
        }
        job->running = true;
        lock.unlock();
        std::shared_ptr<const ThumbnailBitmap> bmp = ExtractThumbnail(job->path, job->size);
        lock.lock();
        if (bmp) {
            Insert(job->key, bmp);
        } else {
            if (missing_.size() >= kMaxMissing) missing_.clear();
            missing_.insert(job->key);
        }
        Complete(job, std::move(bmp));
    }
    lock.unlock();
    if (SUCCEEDED(com)) CoUninitialize();
}

void Thumbnails::Complete(const std::shared_ptr<Job>& job, std::shared_ptr<const ThumbnailBitmap> bitmap) {
    for (uint64_t cookie : job->cookies) completed_.push_back(ThumbnailResult{cookie, job->path, job->size, bitmap});
    inflight_.erase(job->key);
    PostIfNeeded();
}

void Thumbnails::PostIfNeeded() {
    if (posted_ || completed_.empty() || !notifyWnd_) return;
    posted_ = PostMessageW(notifyWnd_, notifyMsg_, 0, 0) != 0;
}

void Thumbnails::Insert(const std::wstring& key, std::shared_ptr<const ThumbnailBitmap> bitmap) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        bytes_ -= it->second.bitmap->Bytes();
        lru_.erase(it->second.lru);
        cache_.erase(it);
    }
    lru_.push_front(key);
    bytes_ += bitmap->Bytes();
    cache_.emplace(key, CacheEntry{std::move(bitmap), lru_.begin()});
    Evict();
}

void Thumbnails::Evict() {
    while (bytes_ > limit_ && !lru_.empty()) {
        auto it = cache_.find(lru_.back());
        if (it != cache_.end()) {
            bytes_ -= it->second.bitmap->Bytes();
            cache_.erase(it);
        }
        lru_.pop_back();
    }
}

}  // namespace library
