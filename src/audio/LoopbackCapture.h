#pragma once
// The music view's visualizer source: Moonbow's own sound, captured with WASAPI process loopback
// (this process tree only, never other apps), analyzed on a background thread.
//
// The thread exists while the visualizer setting is on. It does work only while the view asks
// for frames: SetRunning(false) (paused, stopped) or no frame taken for half a second (the view
// is hidden, the window minimized) stops the capture and parks the thread on an event.
// Each analyzed frame posts ui::Host::kRepaintMessage to the window, one at a time: the next is
// posted only after Take() has picked up the previous one.

#include "audio/Spectrum.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

struct IAudioClient;
struct IAudioCaptureClient;

namespace audio {

struct VisualFrame {
    float bands[kBands] = {};      // bar heights 0..1, smoothed
    float peaks[kBands] = {};      // falling peak marks 0..1
    float scope[kScopePoints] = {};  // the waveform, -1..1, starting at a rising zero crossing
    float wave[kWaveColumns] = {};   // loudness history 0..1, oldest first
    float level = 0;               // overall loudness 0..1
    uint64_t serial = 0;
};

class LoopbackCapture {
public:
    LoopbackCapture(HWND notify, UINT repaintMessage);
    ~LoopbackCapture();
    LoopbackCapture(const LoopbackCapture&) = delete;
    LoopbackCapture& operator=(const LoopbackCapture&) = delete;

    // UI thread.
    void SetRunning(bool run);
    void SetGain(double gain);  // mpv's software gain (Player::OutputGain): the levels ignore the volume
    // Copies the newest frame into `out`; false when there is none newer than out.serial.
    bool Take(VisualFrame& out);
    bool Failed() const { return failed_.load(); }  // process loopback unavailable: show nothing
    bool Stalled() const { return stalled_.load(); }
    uint64_t Captured() const { return captured_.load(); }  // sample frames read so far (tree dumps)
    double CpuSeconds();  // the capture thread's CPU time so far (tree dumps, perf runs)

private:
    void Run();
    bool Open();
    void Close();
    void Read();
    void Analyze(double dt);

    HWND notify_;
    UINT message_;
    HANDLE control_ = nullptr;  // auto-reset: SetRunning, the destructor
    HANDLE audio_ = nullptr;    // the capture client's buffer event
    std::atomic<bool> want_{false}, quit_{false}, failed_{false}, stalled_{false}, posted_{false};
    std::atomic<double> gain_{1.0};
    std::atomic<uint64_t> captured_{0};

    std::mutex mu_;
    VisualFrame shared_;

    // Capture thread only.
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    bool started_ = false;
    bool floatFormat_ = true;
    double rate_ = 48000;
    float ring_[kFftSize] = {};  // mono, ring_[head_] is the oldest
    int head_ = 0;
    int fresh_ = 0;        // samples since the last analysis
    float scopeGain_ = 1;
    double lastAnalysis_ = 0;
    float bandsDb_[kBands] = {};
    float linear_[kFftSize] = {};
    VisualFrame work_;
    PeakDrop drops_[kBands];
    SpectrumAnalyzer analyzer_;
    std::thread thread_;
};

}  // namespace audio
