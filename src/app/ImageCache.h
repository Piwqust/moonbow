#pragma once
#include "app/Signal.h"
#include "gfx/Gpu.h"
#include "ui/Anim.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace library {
class Thumbnails;
}

namespace app {

// Thumbnails and cover art as Direct2D bitmaps. Get() returns the bitmap when
// it is ready and otherwise queues a request; `ready` fires when new images
// arrive so views can repaint. UI thread only.
class ImageCache {
public:
    explicit ImageCache(library::Thumbnails& thumbs);

    // `sizePx` is the longest side wanted in physical pixels (rounded up to a bucket).
    ID2D1Bitmap* Get(const std::wstring& path, int sizePx);
    bool Missing(const std::wstring& path, int sizePx) const;  // the file has no thumbnail
    // Asked for and on its way: draw the plain card surface, not the "no picture" icon.
    bool Loading(const std::wstring& path, int sizePx) const;
    // 0..1: what replaces the plain surface once a request is answered after the window is on
    // screen, the picture or the "no picture" icon, fades in; 1 when it was there at once.
    float Appear(const std::wstring& path, int sizePx) const;
    // The last painted frame asked for images that are not decoded yet.
    bool Busy() const { return !pending_.empty(); }
    // Drops queued requests that no Get() asked for since the previous call (once per painted frame).
    void EndFrame();
    // Called by the app when the thumbnail service posts completions.
    void OnCompleted();
    void Clear();
    // New image files appeared outside the thumbnail service (online posters): views repaint
    // and ask Get() again.
    void Refresh() { ready.Emit(); }

    Signal<> ready;

private:
    struct Entry {
        gfx::ComPtr<ID2D1Bitmap> bitmap;
        size_t bytes = 0;
        uint32_t generation = 0;
        std::list<std::wstring>::iterator lru;
    };
    static int Bucket(int sizePx);
    static std::wstring Key(const std::wstring& path, int bucket);
    void NoteMissing(const std::wstring& key);

    library::Thumbnails& thumbs_;
    std::unordered_map<std::wstring, Entry> entries_;
    std::list<std::wstring> lru_;
    std::unordered_map<std::wstring, uint64_t> pending_;  // key -> cookie
    std::unordered_map<uint64_t, std::wstring> cookies_;
    std::unordered_set<std::wstring> missing_;
    std::unordered_map<std::wstring, std::unique_ptr<ui::AnimFloat>> fades_;  // answered on screen (Appear)
    std::unordered_set<uint64_t> wanted_;  // cookies of pending requests asked for since the last EndFrame
    size_t bytes_ = 0;                     // GPU bitmap pixels
    uint64_t nextCookie_ = 1;
};

}  // namespace app
