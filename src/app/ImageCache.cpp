#include "app/ImageCache.h"

#include "app/App.h"
#include "library/Thumbnails.h"

namespace app {

namespace {

// Decoded pixels of the thumbnail service are only a second-level cache (the shell keeps
// its own on disk), the GPU copies are what painting uses.
constexpr size_t kGpuBytes = 128u << 20;
constexpr size_t kServiceBytes = 48u << 20;

}  // namespace

ImageCache::ImageCache(library::Thumbnails& thumbs) : thumbs_(thumbs) {
    thumbs_.SetMemoryLimit(kServiceBytes);
}

int ImageCache::Bucket(int sizePx) {
    // A few fixed sizes keep the cache small and the shell cache warm.
    for (int b : {96, 192, 320, 480, 720})
        if (sizePx <= b) return b;
    return 1024;
}

std::wstring ImageCache::Key(const std::wstring& path, int bucket) {
    return std::to_wstring(bucket) + L"|" + path;
}

bool ImageCache::Missing(const std::wstring& path, int sizePx) const {
    return missing_.count(Key(path, Bucket(sizePx))) > 0;
}

ID2D1Bitmap* ImageCache::Get(const std::wstring& path, int sizePx) {
    if (path.empty() || !TheApp().config().GetBool("library.thumbnails")) return nullptr;
    const int bucket = Bucket(sizePx);
    const std::wstring key = Key(path, bucket);
    gfx::Gpu& gpu = gfx::Gpu::Get();
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        if (it->second.generation == gpu.Generation()) {
            lru_.splice(lru_.begin(), lru_, it->second.lru);
            return it->second.bitmap.Get();
        }
        // Device was recreated: rebuild from the service's pixel cache.
        bytes_ -= it->second.bytes;
        lru_.erase(it->second.lru);
        entries_.erase(it);
    }
    if (missing_.count(key) || !gpu.Ready()) return nullptr;

    if (auto pixels = thumbs_.Cached(path, bucket)) {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        gfx::ComPtr<ID2D1Bitmap> bmp;
        if (pixels->width && pixels->height &&
            SUCCEEDED(gpu.Ctx()->CreateBitmap(D2D1::SizeU(pixels->width, pixels->height), pixels->pixels.data(),
                                              pixels->width * 4, &props, &bmp))) {
            lru_.push_front(key);
            Entry& e = entries_[key];
            e.bitmap = bmp;
            e.bytes = pixels->Bytes();
            e.generation = gpu.Generation();
            e.lru = lru_.begin();
            bytes_ += e.bytes;
            while (bytes_ > kGpuBytes && lru_.size() > 1) {
                auto old = entries_.find(lru_.back());
                if (old != entries_.end()) {
                    bytes_ -= old->second.bytes;
                    entries_.erase(old);
                }
                lru_.pop_back();
            }
            return bmp.Get();
        }
    }
    if (thumbs_.KnownMissing(path, bucket)) {
        missing_[key] = true;
        return nullptr;
    }
    auto p = pending_.find(key);
    if (p == pending_.end()) {
        const uint64_t cookie = nextCookie_++;
        pending_[key] = cookie;
        cookies_[cookie] = key;
        wanted_.insert(cookie);
        thumbs_.Request(path, bucket, cookie);
    } else {
        wanted_.insert(p->second);
    }
    return nullptr;
}

void ImageCache::EndFrame() {
    if (pending_.size() > wanted_.size()) {
        thumbs_.CancelAllExcept(wanted_);
        for (auto it = cookies_.begin(); it != cookies_.end();) {
            if (wanted_.count(it->first)) {
                ++it;
                continue;
            }
            pending_.erase(it->second);
            it = cookies_.erase(it);
        }
    }
    wanted_.clear();
}

void ImageCache::OnCompleted() {
    bool any = false;
    for (const library::ThumbnailResult& r : thumbs_.TakeCompleted()) {
        auto c = cookies_.find(r.cookie);
        if (c == cookies_.end()) continue;
        const std::wstring key = c->second;
        cookies_.erase(c);
        pending_.erase(key);
        if (!r.bitmap) missing_[key] = true;
        any = true;
    }
    if (any) ready.Emit();
}

void ImageCache::Clear() {
    entries_.clear();
    lru_.clear();
    bytes_ = 0;
    missing_.clear();
}

}  // namespace app
