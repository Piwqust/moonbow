#include "audio/Crossfader.h"

#include "Trace.h"
#include "Util.h"
#include "audio/CrossfadeMath.h"

#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <shobjidl_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace audio {

namespace {

double NowSeconds() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / freq;
}

// The album tag as Windows reads it (the same reader as the library), empty when unknown.
std::wstring AlbumOf(const std::wstring& path) {
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))))
        return {};
    std::wstring out;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(PKEY_Music_AlbumTitle, &v)) && v.vt == VT_LPWSTR && v.pwszVal) out = v.pwszVal;
    PropVariantClear(&v);
    store->Release();
    return out;
}

void TraceXf(const char* fmt, double a = 0, double b = 0, double c = 0) {
    char text[200];
    snprintf(text, sizeof text, fmt, a, b, c);
    char line[240];
    snprintf(line, sizeof line, "xfade[%lu] %s", GetCurrentProcessId(), text);
    Trace(line);
}

}  // namespace

Crossfader::Crossfader(const MpvApi& api, mpv_handle* main, CrossfadePlan plan)
    : api_(api), main_(main), plan_(std::move(plan)) {
    // Not in MpvApi: only this worker makes synchronous sets.
    setProperty_ = reinterpret_cast<decltype(setProperty_)>(GetProcAddress(api_.module, "mpv_set_property"));
    paused_ = mainPaused_ = plan_.paused;
    volume_ = plan_.volume;
    mute_ = plan_.mute;
    speed_ = speedApplied_ = plan_.speed > 0 ? plan_.speed : 1;
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

Crossfader::~Crossfader() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
    }
    SetEvent(wake_);
    if (thread_.joinable()) thread_.join();
    if (wake_) CloseHandle(wake_);
    if (timer_) CloseHandle(timer_);
}

void Crossfader::Cancel() {
    {
        std::lock_guard lock(mu_);
        cancel_ = true;
    }
    SetEvent(wake_);
}

void Crossfader::SetPaused(bool paused) {
    {
        std::lock_guard lock(mu_);
        paused_ = paused;
    }
    SetEvent(wake_);
}

void Crossfader::SetVolume(double volume, bool mute) {
    {
        std::lock_guard lock(mu_);
        volume_ = volume;
        mute_ = mute;
        volumeDirty_ = true;
    }
    SetEvent(wake_);
}

void Crossfader::SetSpeed(double speed) {
    {
        std::lock_guard lock(mu_);
        speed_ = speed > 0 ? speed : 1;
        speedDirty_ = true;
    }
    SetEvent(wake_);
}

void Crossfader::HelperWakeup(void* ctx) {
    SetEvent(static_cast<Crossfader*>(ctx)->wake_);
}

// --- worker ------------------------------------------------------------------------------------

void Crossfader::Run() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (plan_.differentAlbumsOnly) {
        std::wstring a = AlbumOf(plan_.outgoing);
        if (a.empty()) a = plan_.outgoingAlbum;
        if (SameAlbum(a, AlbumOf(plan_.incoming), plan_.outgoing, plan_.incoming)) {
            TraceXf("same album, no crossfade");
            Finish(Outcome::Skipped);
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
    }
    const double t = plan_.duration - plan_.fade;   // main moves on here
    const double s = HelperStart(plan_.fade, plan_.duration);
    TraceXf("armed: fade %.2f s, helper at %.2f, switch at %.2f", plan_.fade, s, t);
    Outcome outcome = Outcome::Failed;
    const bool prepared = PrepareHelper();
    if (!prepared) {
        std::lock_guard lock(mu_);
        if (cancel_) outcome = Outcome::Cancelled;
    } else {
        outcome = Outcome::Cancelled;
        if (WaitUntilMainAt(s)) {
            helperStarted_ = true;
            SetHelperFlag("pause", false);
            pausedApplied_ = false;
            if (Align(t - kHandoff - 0.05) && Handoff()) {
                Fade();
                outcome = Outcome::Done;
            }
        }
    }
    if (mainGainTouched_) SetMainGain(1);
    if (helper_) {
        api_.mpv_set_wakeup_callback(helper_, nullptr, nullptr);
        api_.mpv_terminate_destroy(helper_);
        helper_ = nullptr;
    }
    TraceXf("finished (outcome %.0f, switched %.0f)", static_cast<double>(outcome), switched_ ? 1.0 : 0.0);
    Finish(outcome);
    if (SUCCEEDED(co)) CoUninitialize();
}

void Crossfader::Finish(Outcome outcome) {
    outcome_ = outcome;
    finished_ = true;
}

bool Crossfader::PrepareHelper() {
    if (!setProperty_) return false;
    helper_ = api_.mpv_create();
    if (!helper_) return false;
    auto opt = [this](const char* name, const std::string& value) {
        api_.mpv_set_option_string(helper_, name, value.c_str());
    };
    // Audio only, no UI, no scripts, nothing remembered, no media session next to Lumen's own.
    for (const char* name : {"osc", "terminal", "config", "load-scripts", "ytdl", "input-default-bindings",
                             "input-vo-keyboard", "resume-playback", "save-position-on-quit", "keep-open",
                             "media-controls", "load-stats-overlay", "load-console", "load-select",
                             "load-positioning", "load-commands", "load-context-menu", "sub-auto", "audio-file-auto",
                             "cover-art-auto", "audio-display", "gapless-audio", "prefetch-playlist", "cache",
                             "audio-pitch-correction"})
        opt(name, "no");
    opt("vid", "no");
    opt("sid", "no");
    opt("vo", "null");
    opt("idle", "yes");
    opt("hr-seek", "yes");
    opt("pause", "yes");
    opt("volume-gain-min", "-150");
    opt("volume-gain", "-150");
    char num[64];
    snprintf(num, sizeof num, "%.6f", HelperStart(plan_.fade, plan_.duration));
    opt("start", num);
    snprintf(num, sizeof num, "%.6f", volume_);
    opt("volume", num);
    opt("mute", mute_ ? "yes" : "no");
    snprintf(num, sizeof num, "%.6f", speedApplied_);
    opt("speed", num);
    // The tail must sound like it did a moment ago in the main instance: same filters, device and
    // loudness settings.
    for (const char* name : {"volume-max", "af", "audio-device", "audio-channels", "replaygain", "replaygain-preamp",
                             "replaygain-clip", "replaygain-fallback", "audio-normalize-downmix", "audio-samplerate",
                             "audio-format"}) {
        char* v = nullptr;
        if (api_.mpv_get_property(main_, name, MPV_FORMAT_STRING, &v) >= 0 && v) {
            api_.mpv_set_option_string(helper_, name, v);
            api_.mpv_free(v);
        }
    }
    if (api_.mpv_initialize(helper_) < 0) return false;
    api_.mpv_set_wakeup_callback(helper_, &Crossfader::HelperWakeup, this);
    const std::string path = WideToUtf8(plan_.outgoing);
    const char* load[] = {"loadfile", path.c_str(), "replace", nullptr};
    if (api_.mpv_command(helper_, load) < 0) return false;
    // Ready once the start position is decoded (MPV_EVENT_PLAYBACK_RESTART).
    const double deadline = NowSeconds() + 4;
    while (!helperReady_) {
        if (helperEnded_ || NowSeconds() > deadline) return false;
        if (!Wait(0.05)) return false;
    }
    TraceXf("helper ready");
    return true;
}

bool Crossfader::WaitUntilMainAt(double target) {
    for (;;) {
        if (mainPaused_) {
            if (!Wait(-1)) return false;
            continue;
        }
        const double pts = MainPts();
        if (!std::isfinite(pts)) {
            if (!Wait(0.05)) return false;
            continue;
        }
        const double remain = (target - pts) / speedApplied_;
        if (remain < -0.3) return false;  // a seek past it; Player is told by the seek itself
        if (remain <= 0.0015) return true;
        if (remain > 0.02) {
            if (!Wait(std::min(remain - 0.012, 0.25))) return false;
            continue;
        }
        // The last few milliseconds: spin on the clock, the helper must start on time.
        const double until = NowSeconds() + remain;
        while (NowSeconds() < until) YieldProcessor();
        return true;
    }
}

// The helper has just started a little late or early (the audio device takes a moment to
// start): play it slightly faster or slower while it is still silent, until it is in step with
// the main instance.
bool Crossfader::Align(double until) {
    const double started = NowSeconds();
    double correctUntil = 0;
    int measured = 0;
    for (;;) {
        if (mainPaused_) return false;  // a pause right before the fade: no crossfade this time
        if (!Wait(0.03)) return false;
        const double m1 = MainPts(), h = HelperPts(), m2 = MainPts();
        if (!std::isfinite(m1) || !std::isfinite(h) || !std::isfinite(m2)) continue;
        const double main = (m1 + m2) / 2;
        if (main >= until) break;
        const double now = NowSeconds();
        if (correctUntil > 0 && now >= correctUntil) {
            SetHelperDouble("speed", speedApplied_);
            correctUntil = 0;
        }
        if (now - started < 0.12 || correctUntil > 0) continue;
        const double delta = main - h;  // > 0: the helper is behind
        if (measured++ < 6) TraceXf("align: helper %+.1f ms behind (main %.3f)", delta * 1000, main);
        const double window = std::min(0.25, (until - main) / speedApplied_ - 0.05);
        if (std::fabs(delta) < 0.002 || window < 0.06) continue;
        const double k = std::clamp(1 + delta / window, 0.9, 1.1);
        SetHelperDouble("speed", speedApplied_ * k);
        correctUntil = now + window;
    }
    if (correctUntil > 0) SetHelperDouble("speed", speedApplied_);
    const double m1 = MainPts(), h = HelperPts();
    TraceXf("aligned: helper %+.1f ms behind at %.3f", (m1 - h) * 1000, m1);
    return true;
}

// Both instances play the same audio for kHandoff seconds: main fades out, the helper in.
bool Crossfader::Handoff() {
    const double t = plan_.duration - plan_.fade;
    const double pts = MainPts();
    if (!std::isfinite(pts)) return false;
    const double begin = NowSeconds() + (t - kHandoff - pts) / speedApplied_;
    const double end = begin + kHandoff / speedApplied_;
    mainGainTouched_ = true;
    for (;;) {
        if (mainPaused_) return false;
        const double now = NowSeconds();
        const double x = std::clamp((now - begin) / (end - begin), 0.0, 1.0);
        SetMainGain(1 - x);
        SetHelperGain(x);
        if (now >= end) break;
        if (!Wait(std::min(0.005, std::max(0.001, end - now)))) return false;
    }
    std::lock_guard lock(mu_);
    if (cancel_ || stop_) return false;
    // Still the same file: had mpv already gone on by itself (a duration estimate that was too
    // long), playlist-next would skip a song.
    char* path = nullptr;
    const bool same = api_.mpv_get_property(main_, "path", MPV_FORMAT_STRING, &path) >= 0 && path &&
                      Utf8ToWide(path) == plan_.outgoing;
    if (path) api_.mpv_free(path);
    if (!same) return false;
    const char* next[] = {"playlist-next", nullptr};
    api_.mpv_command_async(main_, 0, next);
    switched_ = true;
    TraceXf("switched at main %.3f (planned %.3f)", MainPts(), t);
    return true;
}

void Crossfader::Fade() {
    double elapsed = 0;  // seconds of the fade played (pauses and speed taken into account)
    double last = NowSeconds();
    while (!helperEnded_) {
        const bool wasPaused = mainPaused_;
        const bool ok = Wait(wasPaused ? -1 : 0.01);
        const double now = NowSeconds();
        if (!wasPaused) elapsed += (now - last) * speedApplied_;
        last = now;
        if (!ok) {
            bool hard;
            {
                std::lock_guard lock(mu_);
                hard = stop_;
            }
            SetMainGain(1);
            if (!hard) FadeOutHelper(0.12);
            return;
        }
        const double u = elapsed / plan_.fade;
        SetMainGain(FadeInGain(u));
        SetHelperGain(FadeOutGain(u));
        if (u >= 1) break;
    }
    SetMainGain(1);
}

// A quick fade instead of a cut: stopping a waveform mid-swing clicks.
void Crossfader::FadeOutHelper(double seconds) {
    double from = 1;
    char* v = nullptr;
    if (api_.mpv_get_property(helper_, "volume-gain", MPV_FORMAT_STRING, &v) >= 0 && v) {
        from = std::pow(10.0, std::atof(v) / 20);
        api_.mpv_free(v);
    }
    const double begin = NowSeconds();
    for (;;) {
        const double x = std::clamp((NowSeconds() - begin) / seconds, 0.0, 1.0);
        SetHelperGain(from * (1 - x));
        if (x >= 1) break;
        Sleep(5);
    }
}

bool Crossfader::Wait(double seconds) {
    if (seconds >= 0) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7);
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
        HANDLE handles[] = {wake_, timer_};
        WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    } else {
        WaitForSingleObject(wake_, INFINITE);
    }
    DrainHelperEvents();
    ApplyRequests();
    std::lock_guard lock(mu_);
    return !cancel_ && !stop_;
}

void Crossfader::ApplyRequests() {
    bool paused, volumeDirty, speedDirty, mute;
    double volume, speed;
    {
        std::lock_guard lock(mu_);
        paused = paused_;
        volumeDirty = volumeDirty_;
        speedDirty = speedDirty_;
        volume = volume_;
        mute = mute_;
        speed = speed_;
        volumeDirty_ = speedDirty_ = false;
    }
    mainPaused_ = paused;
    if (!helper_) return;
    // Until its start the helper stays paused whatever main does.
    const bool helperPause = !helperStarted_ || paused;
    if (helperPause != pausedApplied_) {
        pausedApplied_ = helperPause;
        SetHelperFlag("pause", helperPause);
    }
    if (volumeDirty) {
        SetHelperDouble("volume", volume);
        SetHelperFlag("mute", mute);
    }
    if (speedDirty && speed != speedApplied_) {
        speedApplied_ = speed;
        SetHelperDouble("speed", speed);
    }
}

void Crossfader::DrainHelperEvents() {
    if (!helper_) return;
    for (;;) {
        const mpv_event* ev = api_.mpv_wait_event(helper_, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) helperReady_ = true;
        if (ev->event_id == MPV_EVENT_END_FILE || ev->event_id == MPV_EVENT_SHUTDOWN) helperEnded_ = true;
    }
}

double Crossfader::MainPts() const {
    double v = NAN;
    if (api_.mpv_get_property(main_, "audio-pts", MPV_FORMAT_DOUBLE, &v) < 0) return NAN;
    return v;
}

double Crossfader::HelperPts() const {
    double v = NAN;
    if (!helper_ || api_.mpv_get_property(helper_, "audio-pts", MPV_FORMAT_DOUBLE, &v) < 0) return NAN;
    return v;
}

// Synchronous on this thread: an async set would post a reply that wakes the UI thread for
// nothing, 100 times a second during a fade.
void Crossfader::SetMainGain(double gain) {
    double db = GainToDb(gain);
    if (setProperty_) setProperty_(main_, "volume-gain", MPV_FORMAT_DOUBLE, &db);
}

void Crossfader::SetHelperGain(double gain) {
    SetHelperDouble("volume-gain", GainToDb(gain));
}

void Crossfader::SetHelperFlag(const char* name, bool value) {
    if (!helper_ || !setProperty_) return;
    int flag = value ? 1 : 0;
    setProperty_(helper_, name, MPV_FORMAT_FLAG, &flag);
}

void Crossfader::SetHelperDouble(const char* name, double value) {
    if (!helper_ || !setProperty_) return;
    setProperty_(helper_, name, MPV_FORMAT_DOUBLE, &value);
}

}  // namespace audio
