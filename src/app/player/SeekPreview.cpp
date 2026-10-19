#include "app/player/SeekPreview.h"

#include "Util.h"
#include "app/App.h"
#include "app/MainWindow.h"
#include "gfx/Gpu.h"

#include <mpv/render.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <list>
#include <malloc.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app {

namespace {

constexpr int kW = 192, kH = 108;  // the preview box in DIPs; frames are this times the UI scale
constexpr size_t kMaxFrames = 128;     // grid cache of one file, ~10 MB
constexpr double kRestMs = 150;        // the cursor rests: decode the exact frame
constexpr double kPrefetchMs = 1500;   // neighbours only while the cursor moved recently
constexpr double kIdleMs = 30000;      // then the preview instance is shut down
constexpr double kFrameTimeoutMs = 4000;

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// LUMEN_TRACE: preview timings in %TEMP%\lumen-preview.txt ("<ms> <what>").
void PreviewTrace(double ms, const char* what) {
    static const bool enabled = GetEnvironmentVariableW(L"LUMEN_TRACE", nullptr, 0) > 0;
    if (!enabled) return;
    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"lumen-preview.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") == 0 && f) {
        fprintf(f, "%.1f %s\n", ms, what);
        fclose(f);
    }
}

struct Frame {
    std::vector<uint8_t> bgra;  // w * h * 4, opaque
    int w = kW, h = kH;
};

// The render API is not in MpvApi (the main player draws through its own window), so the few
// functions the preview needs are resolved from the already loaded DLL.
struct RenderApi {
    decltype(&mpv_render_context_create) create = nullptr;
    decltype(&mpv_render_context_set_update_callback) setUpdate = nullptr;
    decltype(&mpv_render_context_update) update = nullptr;
    decltype(&mpv_render_context_render) render = nullptr;
    decltype(&mpv_render_context_free) free = nullptr;

    bool Load(HMODULE m) {
        if (!m) return false;
        create = reinterpret_cast<decltype(create)>(GetProcAddress(m, "mpv_render_context_create"));
        setUpdate = reinterpret_cast<decltype(setUpdate)>(GetProcAddress(m, "mpv_render_context_set_update_callback"));
        update = reinterpret_cast<decltype(update)>(GetProcAddress(m, "mpv_render_context_update"));
        render = reinterpret_cast<decltype(render)>(GetProcAddress(m, "mpv_render_context_render"));
        free = reinterpret_cast<decltype(free)>(GetProcAddress(m, "mpv_render_context_free"));
        return create && setUpdate && update && render && free;
    }
};

struct Request {
    std::wstring path;
    double seconds = 0;   // hovered time
    double step = 2;      // grid bucket size
    double duration = 0;
    long long bucket = 0;
    bool hw = false;      // decode on the GPU with copy-back (above 4K)
    double changedAt = 0; // when the cursor last moved to another time
    int w = kW, h = kH;   // frame size in pixels: the box at the window's scale, not stretched
};

// Like mpv's thumbfast: one persistent, paused libmpv instance that seeks to the hovered time and
// renders the frame into a small buffer with the software render API (no window, no encoder,
// no temp files). Keyframe seeks while the cursor moves, an exact frame once it rests; a grid
// cache of keyframe thumbnails with neighbour prefetch; everything is freed after 30 s unused.
class Previewer {
public:
    ~Previewer() { Stop(); }

    // UI thread. The frame to show for `r`, or null; queues the work.
    std::shared_ptr<const Frame> Get(const Request& r) {
        std::lock_guard lock(mu_);
        if (path_ != r.path || sizeW_ != r.w || sizeH_ != r.h) {
            path_ = r.path;
            sizeW_ = r.w;
            sizeH_ = r.h;
            grid_.clear();
            lru_.clear();
            failed_.clear();
            rest_.reset();
            restTime_ = -1;
        }
        const double now = NowMs();
        lastGet_ = now;
        if (!(want_.path == r.path) || want_.w != r.w || want_.h != r.h || std::abs(want_.seconds - r.seconds) > 1e-6) {
            if (want_.path == r.path && r.bucket != want_.bucket) dir_ = r.bucket > want_.bucket ? 1 : -1;
            want_ = r;
            want_.changedAt = now;
            if (wantAt_ == 0 && !grid_.count(r.bucket)) wantAt_ = now;
            hasWant_ = true;
            Wake();
        }
        if (rest_ && std::abs(restTime_ - r.seconds) < 1e-6) return rest_;
        if (auto it = grid_.find(r.bucket); it != grid_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.second);
            return it->second.first;
        }
        // Not decoded yet: the nearest neighbour keeps the box from flashing empty while scrubbing.
        for (long long d = 1; d <= 3; ++d)
            for (long long b : {r.bucket - d, r.bucket + d})
                if (auto it = grid_.find(b); it != grid_.end()) return it->second.first;
        return nullptr;
    }

    void Stop() {
        {
            std::lock_guard lock(mu_);
            stop_ = true;
            Wake();
        }
        if (worker_.joinable()) worker_.join();
        if (wake_) CloseHandle(wake_);
        wake_ = nullptr;
    }

private:
    // Caller holds mu_. While the idle worker is still closing its instance it is not joined
    // here (that would block the UI thread on mpv_terminate_destroy): it rechecks for work after
    // the close and carries on itself.
    void Wake() {
        if (!running_ && !closing_ && !stop_) {
            if (worker_.joinable()) worker_.join();  // already past Close(), only exiting
            if (!wake_) wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            running_ = true;
            worker_ = std::thread([this] { Run(); });
        }
        if (wake_) SetEvent(wake_);
    }

    static void OnRenderUpdate(void* self) {
        auto* p = static_cast<Previewer*>(self);
        p->updates_.fetch_add(1);
        SetEvent(p->wake_);
    }
    static void OnMpvWakeup(void* self) { SetEvent(static_cast<Previewer*>(self)->wake_); }

    struct Job {
        enum Kind { None, Keyframe, Exact } kind = None;
        std::wstring path;
        double time = 0;
        long long bucket = 0;
        bool hw = false;
        int w = kW, h = kH;
    };

    // Chooses the next job (caller holds mu_): the hovered bucket, then the exact frame after
    // the cursor rested, then neighbours. Returns the wait in ms when there is nothing to do.
    Job Next(double now, double* waitMs) {
        Job j;
        *waitMs = kIdleMs - (now - lastGet_);
        if (!hasWant_ || want_.path.empty()) return j;
        auto missing = [&](long long b) {
            return b >= 0 && static_cast<double>(b) * want_.step < want_.duration && !grid_.count(b) &&
                   !failed_.count(b);
        };
        j.path = want_.path;
        j.hw = want_.hw;
        j.w = want_.w;
        j.h = want_.h;
        if (missing(want_.bucket)) {
            j.kind = Job::Keyframe;
            j.bucket = want_.bucket;
            j.time = (static_cast<double>(want_.bucket) + 0.5) * want_.step;
            return j;
        }
        const double rested = now - want_.changedAt;
        if (std::abs(restTime_ - want_.seconds) > 1e-6) {  // tried once, even when it failed
            if (rested >= kRestMs) {
                j.kind = Job::Exact;
                j.time = want_.seconds;
                return j;
            }
            *waitMs = std::min(*waitMs, kRestMs - rested);
        }
        if (rested < kPrefetchMs) {
            // Ahead in the direction of movement first.
            for (long long d : {1LL, 2LL, 3LL})
                for (long long b : {want_.bucket + d * dir_, want_.bucket - d * dir_})
                    if (missing(b)) {
                        j.kind = Job::Keyframe;
                        j.bucket = b;
                        j.time = (static_cast<double>(b) + 0.5) * want_.step;
                        return j;
                    }
        }
        return j;
    }

    // Idle or exit: the cache and the request are dropped and the worker is marked not running
    // under one lock, and only then the instance is closed, so a hover during the close starts
    // from a clean state instead of meeting frames of a half-destroyed instance. Returns true when
    // the worker should carry on (a hover arrived while closing).
    bool ShutDownIdle() {
        {
            std::lock_guard lock(mu_);
            grid_.clear();
            lru_.clear();
            failed_.clear();
            rest_.reset();
            restTime_ = -1;
            path_.clear();
            want_ = {};
            hasWant_ = false;
            wantAt_ = 0;
            running_ = false;
            closing_ = true;
        }
        const bool open = h_ != nullptr;
        const double started = NowMs();
        Close();
        // The instance's decoder buffers, packets and the frame grid were many mid-sized blocks:
        // give the heap's free pages back, or private memory stays ~25 MB above where it was.
        if (open) HeapCompact(GetProcessHeap(), 0);
        if (open) PreviewTrace(NowMs() - started, "preview instance closed");
        std::lock_guard lock(mu_);
        closing_ = false;
        if (stop_ || !hasWant_) return false;
        running_ = true;
        return true;
    }

    void Run() {
        for (;;) {
            Job job;
            double waitMs = 0;
            bool idle = false;
            {
                std::lock_guard lock(mu_);
                if (!stop_) job = Next(NowMs(), &waitMs);
                idle = stop_ || (job.kind == Job::None && waitMs <= 0);
            }
            if (idle) {
                if (ShutDownIdle()) continue;
                break;
            }
            if (job.kind == Job::None) {
                WaitForSingleObject(wake_, static_cast<DWORD>(std::clamp(waitMs, 1.0, kIdleMs)));
                if (h_) DrainEvents();
                continue;
            }
            const double started = NowMs();
            const bool cold = !h_;
            std::shared_ptr<Frame> frame = Decode(job);
            const double done = NowMs();
            {
                std::lock_guard lock(mu_);
                if (job.path == path_ && job.w == sizeW_ && job.h == sizeH_) {
                    if (job.kind == Job::Exact) {
                        rest_ = frame;
                        restTime_ = job.time;
                    } else if (frame) {
                        lru_.push_front(job.bucket);
                        grid_[job.bucket] = {frame, lru_.begin()};
                        // The same ~10 MB at any scale: fewer frames when they are bigger.
                        const size_t cap = std::max<size_t>(
                            32, kMaxFrames * kW * kH / (static_cast<size_t>(job.w) * static_cast<size_t>(job.h)));
                        while (grid_.size() > cap) {
                            grid_.erase(lru_.back());
                            lru_.pop_back();
                        }
                    } else {
                        failed_.insert(job.bucket);
                    }
                }
                if (hasWant_ && job.kind != Job::None && wantAt_ > 0 && frame &&
                    ((job.kind == Job::Keyframe && job.bucket == want_.bucket) || job.kind == Job::Exact)) {
                    char what[96];
                    snprintf(what, sizeof(what), "hover-to-frame (%s%s, decode %.0f ms)",
                             job.kind == Job::Exact ? "exact" : "keyframe", cold ? ", cold start" : "", done - started);
                    PreviewTrace(done - wantAt_, what);
                    wantAt_ = 0;
                } else if (frame) {
                    char what[64];
                    snprintf(what, sizeof(what), "%s decode", job.kind == Job::Exact ? "exact" : "prefetch");
                    PreviewTrace(done - started, what);
                }
            }
            if (MainWindow* w = TheApp().main()) PostMessageW(w->Hwnd(), ui::Host::kRepaintMessage, 0, 0);
        }
    }

    bool Open() {
        const MpvApi& api = TheApp().player().Api();
        if (!api.mpv_create || !render_.Load(api.module)) return false;
        h_ = api.mpv_create();
        if (!h_) return false;
        SetQuietMpvOptions(api, h_);
        const char* opts[][2] = {
            {"vo", "libmpv"}, {"aid", "no"}, {"sid", "no"}, {"pause", "yes"}, {"keep-open", "always"},
            {"idle", "yes"}, {"force-window", "no"}, {"msg-level", "all=no"}, {"osd-level", "0"}, {"osd-on-seek", "no"},
            // Read as little as possible: no cache (above), no read-ahead, no back buffer.
            {"demuxer-readahead-secs", "0"}, {"demuxer-max-bytes", "2MiB"},
            {"demuxer-max-back-bytes", "0"}, {"demuxer-seekable-cache", "no"},
            // Decode fast rather than pretty: no loop filter, no bit-exact tricks, one frame early.
            {"vd-lavc-skiploopfilter", "all"}, {"vd-lavc-fast", "yes"}, {"vd-lavc-threads", "2"},
            {"video-latency-hacks", "yes"}, {"hr-seek-framedrop", "yes"}, {"hwdec-extra-frames", "0"},
            // The thumbnail scale filter (vf) is set per frame size in Decode.
            {"sws-scaler", "fast-bilinear"}, {"sws-allow-zimg", "no"}, {"hwdec", "no"},
        };
        for (auto& kv : opts) api.mpv_set_option_string(h_, kv[0], kv[1]);
        if (api.mpv_initialize(h_) < 0) return Close(), false;
        mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};
        if (render_.create(&ctx_, h_, params) < 0) return Close(), false;
        render_.setUpdate(ctx_, &Previewer::OnRenderUpdate, this);
        api.mpv_set_wakeup_callback(h_, &Previewer::OnMpvWakeup, this);
        return true;
    }

    // Scale to the thumbnail size right after decoding; the renderer then only converts.
    bool SetSize(int w, int h) {
        if (pixels_ && w == pixW_ && h == pixH_) return true;
        if (pixels_) _aligned_free(pixels_);
        pixels_ = static_cast<uint8_t*>(_aligned_malloc(static_cast<size_t>(w) * h * 4, 64));
        if (!pixels_) return false;
        pixW_ = w;
        pixH_ = h;
        char vf[160];
        snprintf(vf, sizeof(vf),
                 "scale=w=%d:h=%d:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=fast_bilinear", w, h);
        TheApp().player().Api().mpv_set_option_string(h_, "vf", vf);
        loaded_.clear();  // the filter applies from the next load
        return true;
    }

    void Close() {
        const MpvApi& api = TheApp().player().Api();
        if (ctx_) render_.free(ctx_);  // before the core goes away
        ctx_ = nullptr;
        if (h_) api.mpv_terminate_destroy(h_);
        h_ = nullptr;
        if (pixels_) _aligned_free(pixels_);
        pixels_ = nullptr;
        pixW_ = pixH_ = 0;
        loaded_.clear();
    }

    // Handles pending mpv events; returns the flags seen.
    struct Seen {
        bool restart = false, loaded = false, ended = false;
    };
    // "loadfile replace" first ends the file that was open: that END_FILE (before the new file's
    // START_FILE) is no failure of the new one. Taken as one, it failed every first frame of the
    // next file and marked its buckets as undecodable for good (the 160x90 clip after another).
    Seen DrainEvents() {
        Seen seen;
        const MpvApi& api = TheApp().player().Api();
        for (;;) {
            mpv_event* ev = api.mpv_wait_event(h_, 0);
            if (ev->event_id == MPV_EVENT_NONE) break;
            if (ev->event_id == MPV_EVENT_START_FILE) started_ = true;
            if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) seen.restart = true;
            if (ev->event_id == MPV_EVENT_FILE_LOADED) seen.loaded = true;
            if ((ev->event_id == MPV_EVENT_END_FILE && started_) || ev->event_id == MPV_EVENT_SHUTDOWN) seen.ended = true;
        }
        return seen;
    }

    // Renders the pending frame into pixels_; true when there was a new video frame. Rendering
    // promptly also releases mpv's VO thread, which waits for each frame to be presented.
    bool RenderIfNew() {
        if (!(render_.update(ctx_) & MPV_RENDER_UPDATE_FRAME) || !pixels_) return false;
        int size[2] = {pixW_, pixH_};
        size_t stride = static_cast<size_t>(pixW_) * 4;
        mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                                     {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("bgr0")},
                                     {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                                     {MPV_RENDER_PARAM_SW_POINTER, pixels_},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};
        return render_.render(ctx_, params) >= 0;
    }

    std::shared_ptr<Frame> Decode(const Job& job) {
        if (!h_ && !Open()) return nullptr;
        if (!SetSize(job.w, job.h)) return nullptr;
        const MpvApi& api = TheApp().player().Api();
        // Copy-back hardware decoding only where software decoding of a keyframe is slow.
        const char* hw = job.hw ? "d3d11va-copy" : "no";
        if (loaded_ != job.path || hw_ != job.hw) {
            api.mpv_set_option_string(h_, "hwdec", hw);
            hw_ = job.hw;
        }
        const std::string t = std::to_string(job.time);
        const char* how = job.kind == Job::Exact ? "absolute+exact" : "absolute+keyframes";
        DrainEvents();
        RenderIfNew();  // a frame left over from an abandoned job
        const unsigned before = updates_.load();
        if (loaded_ != job.path) {
            const std::string p = WideToUtf8(job.path);
            const std::string start = "start=" + t + (job.kind == Job::Exact ? ",hr-seek=yes" : ",hr-seek=no");
            const char* cmd[] = {"loadfile", p.c_str(), "replace", "-1", start.c_str(), nullptr};
            started_ = false;
            if (api.mpv_command(h_, cmd) < 0) return nullptr;
            loaded_ = job.path;
        } else {
            const char* cmd[] = {"seek", t.c_str(), how, nullptr};
            if (api.mpv_command(h_, cmd) < 0) return nullptr;
        }
        bool restart = false, rendered = false;
        const double deadline = NowMs() + kFrameTimeoutMs;
        for (;;) {
            const Seen seen = DrainEvents();
            restart |= seen.restart;
            if (seen.ended && !restart) {
                loaded_.clear();  // unplayable: reload next time
                return nullptr;
            }
            if (updates_.load() != before && RenderIfNew()) rendered = true;
            if (restart && rendered) break;
            const double now = NowMs();
            if (now >= deadline) return nullptr;
            {
                std::lock_guard lock(mu_);
                if (stop_) return nullptr;
                // An exact decode can take a while on long GOPs: give up when the cursor moved on.
                if (job.kind == Job::Exact && std::abs(want_.seconds - job.time) > 1e-6) return nullptr;
            }
            WaitForSingleObject(wake_, static_cast<DWORD>(std::min(50.0, deadline - now)));
        }
        auto frame = std::make_shared<Frame>();
        frame->w = pixW_;
        frame->h = pixH_;
        frame->bgra.assign(pixels_, pixels_ + static_cast<size_t>(pixW_) * pixH_ * 4);
        for (size_t i = 3; i < frame->bgra.size(); i += 4) frame->bgra[i] = 0xFF;  // "bgr0" padding
        return frame;
    }

    std::mutex mu_;
    std::thread worker_;
    HANDLE wake_ = nullptr;
    bool stop_ = false, running_ = false, closing_ = false;
    // UI side (under mu_).
    std::wstring path_;
    Request want_;
    bool hasWant_ = false;
    double wantAt_ = 0;   // first request since the last delivered frame (for the trace)
    double lastGet_ = 0;
    long long dir_ = 1;  // scrubbing direction, for the prefetch order
    std::unordered_map<long long, std::pair<std::shared_ptr<const Frame>, std::list<long long>::iterator>> grid_;
    std::list<long long> lru_;
    std::unordered_set<long long> failed_;  // buckets mpv could not decode
    std::shared_ptr<const Frame> rest_;
    double restTime_ = -1;
    // Worker side.
    RenderApi render_;
    mpv_handle* h_ = nullptr;
    mpv_render_context* ctx_ = nullptr;
    uint8_t* pixels_ = nullptr;
    int pixW_ = 0, pixH_ = 0;  // pixels_ and the vf scale
    int sizeW_ = kW, sizeH_ = kH;  // UI side (under mu_): the frame size the cache holds
    bool started_ = false;     // START_FILE seen since the last loadfile
    std::wstring loaded_;
    bool hw_ = false;
    std::atomic<unsigned> updates_{0};
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

// Copy-back hardware decoding costs a decoder pool of full-size surfaces and device setup, and
// part of that memory stays with the process after the instance closes (4K HEVC: +250 MB while
// scrubbing, +135 MB for good). Software decoding with the loop filter skipped is as quick on
// 4K keyframes (H.264 35 ms, HEVC 93 ms cold) and gives everything back, so only video above
// 4K goes to the GPU.
bool HeavyVideo(const PlayerState& s) {
    return s.videoW > 4096 || s.videoH > 2304;
}

// Pixels per DIP of the context being painted (its DPI over 96).
float DeviceScale(ui::Painter& p) {
    float dx = 96, dy = 96;
    p.Ctx()->GetDpi(&dx, &dy);
    return dx / 96.0f;
}

// Top of the preview as last painted (window DIPs), 0 while none shows.
float g_previewTop = 0;

void SetPreviewTop(float top) {
    top = std::round(top);
    if (top == g_previewTop) return;
    g_previewTop = top;
    // Called while painting: the next layout moves the subtitles (MainWindow::UpdateSubtitleLift).
    if (MainWindow* w = TheApp().main())
        w->Post([] {
            if (MainWindow* mw = TheApp().main()) mw->RequestLayout();
        });
}

}  // namespace

float SeekPreviewTop() {
    return g_previewTop;
}

void HideSeekPreview() {
    SetPreviewTop(0);
}

float ClampedLeft(float x, float w, const ui::Rect& track, const ui::Rect& bounds) {
    // Over the bar while it is wider than the box; else within the window, 8 DIP from its edges.
    float lo = track.x, hi = track.Right() - w;
    if (hi < lo) {
        lo = bounds.x + 8;
        hi = bounds.Right() - 8 - w;
    }
    return hi < lo ? lo : std::clamp(x - w / 2, lo, hi);  // std::clamp needs lo <= hi
}

float PaintSeekPreview(ui::Painter& p, const ui::Rect& track, float x, double seconds, const ui::Rect& bounds) {
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    const QueueItem* item = app.queue().CurrentItem();
    if (!s.hasVideo || !item || item->path.find(L"://") != std::wstring::npos || s.duration <= 0 ||
        !app.config().GetBool("playback.seekPreview")) {
        HideSeekPreview();
        return 0;
    }

    // Bucket times so nearby hovers share a frame (≈ 300 frames per file, at least 2 s apart).
    Request r;
    r.path = item->path;
    r.seconds = seconds;
    r.step = std::max(2.0, s.duration / 300.0);
    r.duration = s.duration;
    r.bucket = static_cast<long long>(seconds / r.step);
    r.hw = app.config().GetString("performance.hwdec") != "off" && HeavyVideo(s);
    // At 125-200 % a 192x108 pixel frame was stretched over 240x135 to 384x216 pixels.
    const float scale = std::clamp(p.Ctx() ? DeviceScale(p) : 1.0f, 1.0f, 3.0f);
    r.w = static_cast<int>(std::lround(kW * scale / 2)) * 2;
    r.h = static_cast<int>(std::lround(kH * scale / 2)) * 2;
    auto frame = Instance().Get(r);

    const float w = kW, h = kH;
    const ui::Rect box{ClampedLeft(x, w, track, bounds), track.y - 34 - h - 6, w, h};
    p.Shadow(box, 6, 10, 4, 0.35f);
    p.Fill(box, ui::P().bgDeep, 6);
    if (frame) {
        gfx::Gpu& gpu = gfx::Gpu::Get();
        if (g_shown.frame != frame || g_shown.generation != gpu.Generation()) {
            g_shown = {};
            D2D1_BITMAP_PROPERTIES props =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(gpu.Ctx()->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(frame->w), static_cast<UINT32>(frame->h)),
                                                  frame->bgra.data(), static_cast<UINT32>(frame->w) * 4, &props,
                                                  &g_shown.bitmap))) {
                g_shown.frame = frame;
                g_shown.generation = gpu.Generation();
            }
        }
        p.Image(g_shown.bitmap.Get(), box, 6, true);
    }
    p.Stroke(box, ui::P().line, 6);
    SetPreviewTop(box.y);
    return h + 6;
}

void ShutdownSeekPreview() {
    Instance().Stop();
    g_shown = {};
}

}  // namespace app
