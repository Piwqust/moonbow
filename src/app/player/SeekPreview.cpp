#include "app/player/SeekPreview.h"

#include "Util.h"
#include "app/App.h"
#include "app/MainWindow.h"
#include "gfx/Gpu.h"

#include <condition_variable>
#include <cstdio>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace app {

namespace {

constexpr int kW = 192, kH = 108;
constexpr size_t kMaxFrames = 240;

struct Frame {
    std::vector<uint8_t> bgra;  // kW * kH * 4, opaque
};

class Previewer {
public:
    ~Previewer() { Stop(); }

    // Returns a cached frame or queues it (only the newest request is kept).
    std::shared_ptr<const Frame> Get(const std::wstring& path, long long bucket, double seconds) {
        std::lock_guard lock(mu_);
        const std::wstring key = Key(path, bucket);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.second);
            return it->second.first;
        }
        if (!(wanted_ == key)) {
            wanted_ = key;
            wantedPath_ = path;
            wantedTime_ = seconds;
            hasWork_ = true;
            if (!worker_.joinable()) worker_ = std::thread([this] { Run(); });
            cv_.notify_one();
        }
        return nullptr;
    }

    void Stop() {
        {
            std::lock_guard lock(mu_);
            stop_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

private:
    static std::wstring Key(const std::wstring& path, long long bucket) { return std::to_wstring(bucket) + L"|" + path; }

    void Run() {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring out = std::wstring(tmp) + L"lumen-seek-" + std::to_wstring(GetCurrentProcessId()) + L".bgra";
        for (;;) {
            std::wstring key, path;
            double t = 0;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] { return stop_ || hasWork_; });
                if (stop_) break;
                hasWork_ = false;
                key = wanted_;
                path = wantedPath_;
                t = wantedTime_;
            }
            auto frame = Decode(path, t, out);
            {
                std::lock_guard lock(mu_);
                if (frame) {
                    lru_.push_front(key);
                    cache_[key] = {frame, lru_.begin()};
                    while (cache_.size() > kMaxFrames) {
                        cache_.erase(lru_.back());
                        lru_.pop_back();
                    }
                }
                if (wanted_ == key) wanted_.clear();
            }
            // Repaint the window that shows the tooltip.
            if (MainWindow* w = TheApp().main()) PostMessageW(w->Hwnd(), ui::Host::kRepaintMessage, 0, 0);
        }
        DeleteFileW(out.c_str());
    }

    std::shared_ptr<Frame> Decode(const std::wstring& path, double t, const std::wstring& out) {
        const MpvApi& api = TheApp().player().Api();
        if (!api.mpv_create) return nullptr;
        DeleteFileW(out.c_str());
        mpv_handle* h = api.mpv_create();
        if (!h) return nullptr;
        const std::string start = std::to_string(t);
        const std::string o = WideToUtf8(out);
        const char* opts[][2] = {
            {"o", o.c_str()}, {"of", "rawvideo"}, {"ovc", "rawvideo"},
            {"vf", "scale=192:108:force_original_aspect_ratio=decrease,pad=192:108:(ow-iw)/2:(oh-ih)/2,format=bgra"},
            {"frames", "1"}, {"start", start.c_str()}, {"aid", "no"}, {"sid", "no"}, {"hr-seek", "yes"},
            {"hwdec", "no"}, {"terminal", "no"}, {"config", "no"}, {"load-scripts", "no"}, {"ytdl", "no"},
            {"idle", "no"}, {"msg-level", "all=no"},
            // One small frame: no read-ahead cache, few decoder threads (4K sources).
            {"cache", "no"}, {"demuxer-max-bytes", "8MiB"}, {"demuxer-readahead-secs", "0"}, {"vd-lavc-threads", "2"},
        };
        for (auto& kv : opts) api.mpv_set_option_string(h, kv[0], kv[1]);
        std::shared_ptr<Frame> frame;
        if (api.mpv_initialize(h) >= 0) {
            const std::string p = WideToUtf8(path);
            const char* cmd[] = {"loadfile", p.c_str(), nullptr};
            api.mpv_command(h, cmd);
            for (;;) {
                mpv_event* ev = api.mpv_wait_event(h, 3.0);
                if (ev->event_id == MPV_EVENT_END_FILE || ev->event_id == MPV_EVENT_SHUTDOWN ||
                    ev->event_id == MPV_EVENT_NONE)
                    break;
                std::lock_guard lock(mu_);
                if (stop_) break;
            }
        }
        api.mpv_terminate_destroy(h);
        FILE* f = nullptr;
        if (_wfopen_s(&f, out.c_str(), L"rb") == 0 && f) {
            frame = std::make_shared<Frame>();
            frame->bgra.resize(static_cast<size_t>(kW) * kH * 4);
            if (fread(frame->bgra.data(), 1, frame->bgra.size(), f) != frame->bgra.size()) frame.reset();
            fclose(f);
        }
        return frame;
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false, hasWork_ = false;
    std::wstring wanted_, wantedPath_;
    double wantedTime_ = 0;
    std::unordered_map<std::wstring, std::pair<std::shared_ptr<const Frame>, std::list<std::wstring>::iterator>> cache_;
    std::list<std::wstring> lru_;
};

Previewer& Instance() {
    static Previewer p;
    return p;
}

// The D2D bitmap of the frame shown last (frames change only as the mouse moves).
struct ShownBitmap {
    std::shared_ptr<const Frame> frame;
    uint32_t generation = 0;
    gfx::ComPtr<ID2D1Bitmap> bitmap;
} g_shown;

}  // namespace

float PaintSeekPreview(ui::Painter& p, const ui::Rect& track, float x, double seconds) {
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    const QueueItem* item = app.queue().CurrentItem();
    if (!s.hasVideo || !item || item->path.find(L"://") != std::wstring::npos || s.duration <= 0) return 0;
    if (!app.config().GetBool("playback.seekPreview")) return 0;

    // Bucket times so nearby hovers share a frame (≈ 300 frames per file, at least 2 s apart).
    const double step = std::max(2.0, s.duration / 300.0);
    const long long bucket = static_cast<long long>(seconds / step);
    auto frame = Instance().Get(item->path, bucket, (static_cast<double>(bucket) + 0.5) * step);

    const float w = kW, h = kH;
    const ui::Rect box{std::clamp(x - w / 2, track.x, track.Right() - w), track.y - 34 - h - 6, w, h};
    p.Shadow(box, 6, 10, 4, 0.35f);
    p.Fill(box, ui::P().bgDeep, 6);
    if (frame) {
        gfx::Gpu& gpu = gfx::Gpu::Get();
        if (g_shown.frame != frame || g_shown.generation != gpu.Generation()) {
            g_shown = {};
            D2D1_BITMAP_PROPERTIES props =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(gpu.Ctx()->CreateBitmap(D2D1::SizeU(kW, kH), frame->bgra.data(), kW * 4, &props, &g_shown.bitmap))) {
                g_shown.frame = frame;
                g_shown.generation = gpu.Generation();
            }
        }
        p.Image(g_shown.bitmap.Get(), box, 6, true);
    }
    p.Stroke(box, ui::P().line, 6);
    return h + 6;
}

void ShutdownSeekPreview() {
    Instance().Stop();
}

}  // namespace app
