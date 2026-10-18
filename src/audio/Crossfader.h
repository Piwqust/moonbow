#pragma once
// One crossfade between two songs (see audio/CrossfadeMath.h for the timeline).
//
// mpv plays one file at a time, so a short-lived second, audio-only mpv instance (the helper)
// plays the last seconds of the song that ends while the main instance has already moved on to
// the next one. Everything that belongs to a file (the queue, history, tracks, filters, the
// title) stays with the main instance and moves on at the start of the fade. The helper exists
// from a few seconds before the fade until its end, never longer.
//
// All the work runs on its own thread; the UI thread only sets flags and never waits, except
// in the destructor (mpv shutdown), which also tears the helper down.

#include "MpvApi.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace audio {

struct CrossfadePlan {
    std::wstring outgoing, incoming;  // file paths
    std::wstring outgoingAlbum;       // mpv's tag, when Windows can't read the file's
    double duration = 0;              // of the outgoing file
    double fade = 0;                  // EffectiveFade()
    bool differentAlbumsOnly = false;
    double volume = 100;  // mpv volume as set on the main instance
    bool mute = false;
    double speed = 1;  // armed only while playing
};

class Crossfader {
public:
    enum class Outcome {
        Running,
        Skipped,    // same album: the songs stay gapless
        Cancelled,  // a seek, a pause before the fade or another file: it may be armed again
        Failed,     // the helper could not play the file
        Done,
    };

    Crossfader(const MpvApi& api, mpv_handle* main, CrossfadePlan plan);
    ~Crossfader();
    Crossfader(const Crossfader&) = delete;
    Crossfader& operator=(const Crossfader&) = delete;

    // UI thread; none of these waits.
    void Cancel();  // the user seeked or chose another file: the old song stops (fades out quickly)
    void SetPaused(bool paused);
    void SetVolume(double volume, bool mute);
    void SetSpeed(double speed);

    bool Finished() const { return finished_.load(); }
    Outcome Result() const { return outcome_.load(); }
    bool Switched() const { return switched_.load(); }  // main went on to the next file

private:
    void Run();
    bool PrepareHelper();
    bool WaitUntilMainAt(double pts);
    bool Align(double until);
    bool Handoff();
    void Fade();
    void Settle(double seconds);
    void Finish(Outcome outcome);

    // Waits up to `seconds` (< 0 = until woken) and applies what the UI asked for meanwhile.
    // False when the job must end (cancelled or shutting down).
    bool Wait(double seconds);
    void ApplyRequests();
    void DrainHelperEvents();
    double MainPts() const;
    double HelperPts() const;
    void SetMainGain(double gain);
    void SetHelperGain(double gain);
    void SetHelperFlag(const char* name, bool value);
    void SetHelperDouble(const char* name, double value);
    static void HelperWakeup(void* ctx);

    const MpvApi& api_;
    mpv_handle* main_;
    mpv_handle* helper_ = nullptr;
    CrossfadePlan plan_;

    std::mutex mu_;  // requests below, and the switch (so Cancel never races with it)
    bool cancel_ = false;
    bool stop_ = false;
    bool paused_ = false;
    double volume_ = 100, speed_ = 1;
    bool mute_ = false;
    bool volumeDirty_ = false, speedDirty_ = false;

    // Worker-only copies.
    decltype(&::mpv_set_property) setProperty_ = nullptr;
    bool mainPaused_ = false;
    bool helperStarted_ = false;  // unpaused at S
    bool pausedApplied_ = true;   // the helper's pause property
    double speedApplied_ = 1;
    bool helperReady_ = false, helperEnded_ = false;
    bool mainGainTouched_ = false;
    double mainGain_ = 1, helperGain_ = 0;  // as last set

    std::atomic<bool> finished_{false}, switched_{false};
    std::atomic<Outcome> outcome_{Outcome::Running};
    void* wake_ = nullptr;   // auto-reset event
    void* timer_ = nullptr;  // high-resolution waitable timer
    std::thread thread_;
};

}  // namespace audio
