#pragma once
// Moonbow's own sound for every view that draws it: the music view's visualizer, the audio meters
// and the separate window of each meter. One capture and one analysis serve them all
// (audio/LoopbackCapture); the meters' running state (held peaks, clip lights, the loudness
// history) and their options are shared too, so "Reset" in one window resets every one.
//
// A view attaches while it may draw (its constructor) and detaches in its destructor. Each new
// frame repaints the attached views that are on screen; their Paint calls Pull(), which takes the
// frame. When none of them is drawn (hidden, minimized), no frame is taken and the capture parks
// itself after half a second, as it did with a single view.

#include "audio/LoopbackCapture.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ui {
class Widget;
}

namespace app {

class AudioFeed {
public:
    static AudioFeed& Get();

    void Attach(ui::Widget* view);
    void Detach(ui::Widget* view);

    // From an attached view's Paint: starts the capture while the song plays (parks it otherwise),
    // takes the newest frame and moves the held peaks and the loudness history on. Several views
    // may call it in one frame; the second call finds nothing new.
    void Pull(const ui::Widget* view);
    const audio::VisualFrame& Frame() const { return frame_; }
    // The song plays and the capture works: the frame is the sound now.
    bool IsLive() const { return live_; }
    bool Failed() const { return capture_ && capture_->Failed(); }
    bool Stalled() const { return capture_ && capture_->Stalled(); }
    uint64_t Captured() const { return capture_ ? capture_->Captured() : 0; }
    double CpuMs() const { return capture_ ? capture_->CpuSeconds() * 1000 : 0.0; }

    // The meters' options: Settings > interface.meters*, set from their menus.
    struct Options {
        bool balance = true, gonioDots = false, spectrumLine = false, spectrumPeaks = true;
        bool vuPeak = true;
        float target = audio::kLoudnessNone;  // LUFS, none: no target line
        size_t historyN = 600;                // points the loudness graph shows
        float floor = -60;                    // level bars, dBFS
        double holdMs = 1500;                 // 0: no hold, < 0: until reset
    };
    const Options& Opt() const { return opt_; }
    float Reference() const { return reference_; }  // dBFS that reads 0 VU (audio.vuReference)

    struct Hold {
        float db = -200;
        double at = 0;
    };
    Hold hold[2], max[2];        // held peak, highest peak since the last reset (clip lights)
    std::vector<float> history;  // short-term loudness, oldest first, a point per 100 ms

    void ResetLoudness();
    void ResetClips();
    void Reset();  // peaks, clip lights and loudness

private:
    AudioFeed();
    void LoadOptions();
    void OnPlayerChanged(unsigned changes);
    // A new frame: repaint the views on screen. Fair to every window: while one of them has not
    // drawn the last frame yet, the new one waits (Windows hands out WM_PAINT in window order, so
    // with many windows on a busy thread the last ones would never get a turn).
    void Notify();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    struct View {
        ui::Widget* widget;
        bool pending = false;   // asked to repaint, not drawn yet
        double askedAt = 0;     // AnimFloat::Now
    };
    std::vector<View> views_;
    std::unique_ptr<audio::LoopbackCapture> capture_;
    audio::VisualFrame frame_;
    HWND notify_ = nullptr;  // message-only window the capture posts to
    Options opt_;
    float reference_ = -7;
    bool live_ = false;
    bool loaded_ = false;
    uint64_t serial_ = 0;     // PlayerState::loadSerial the state belongs to
    double historyAt_ = 0;
};

}  // namespace app
