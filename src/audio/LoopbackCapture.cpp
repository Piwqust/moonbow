#include "audio/LoopbackCapture.h"

#include "Trace.h"
#include "audio/LoopbackSupport.h"

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

constexpr double kFrameInterval = 1.0 / 60;  // analysis (and frame) rate cap
constexpr double kStallAfter = 0.5;          // no frame taken this long: the view is not drawn

// ActivateAudioInterfaceAsync's completion callback. Must be agile: Windows calls it on one of
// its own MTA threads.
class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivateHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivateHandler() {
        if (client_) client_->Release();
        if (done_) CloseHandle(done_);
    }
    HANDLE Done() const { return done_; }
    HRESULT Result() const { return hr_; }
    IAudioClient* TakeClient() {
        IAudioClient* c = client_;
        client_ = nullptr;
        return c;
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (iid == __uuidof(IAgileObject))
            *out = static_cast<IAgileObject*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT activated = E_FAIL;
        IUnknown* unk = nullptr;
        hr_ = op->GetActivateResult(&activated, &unk);
        if (SUCCEEDED(hr_)) hr_ = activated;
        if (SUCCEEDED(hr_) && unk) hr_ = unk->QueryInterface(IID_PPV_ARGS(&client_));
        if (unk) unk->Release();
        SetEvent(done_);
        return S_OK;
    }

private:
    LONG ref_ = 1;
    HANDLE done_;
    HRESULT hr_ = E_FAIL;
    IAudioClient* client_ = nullptr;
};

using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                     IActivateAudioInterfaceAsyncOperation**);

}  // namespace

LoopbackCapture::LoopbackCapture(HWND notify, UINT repaintMessage) : notify_(notify), message_(repaintMessage) {
    control_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

LoopbackCapture::~LoopbackCapture() {
    quit_ = true;
    SetEvent(control_);
    if (thread_.joinable()) thread_.join();
    CloseHandle(control_);
}

void LoopbackCapture::SetRunning(bool run) {
    const bool wake = run && stalled_.exchange(false);
    if (want_.exchange(run) != run || wake) SetEvent(control_);
}

void LoopbackCapture::SetGain(double gain) {
    gain_ = gain;
}

double LoopbackCapture::CpuSeconds() {
    FILETIME created, exited, kernel, user;
    if (!thread_.joinable() || !GetThreadTimes(thread_.native_handle(), &created, &exited, &kernel, &user)) return 0;
    auto ticks = [](FILETIME f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
}

bool LoopbackCapture::Take(VisualFrame& out) {
    std::lock_guard lock(mu_);
    posted_ = false;
    if (shared_.serial == out.serial) return false;
    out = shared_;
    return true;
}

void LoopbackCapture::Run() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    analyzer_.Configure(rate_);
    if (!Open()) {
        Trace("visualizer: process loopback unavailable");
        failed_ = true;
        if (notify_) PostMessageW(notify_, message_, 0, 0);
        Close();
        if (SUCCEEDED(co)) CoUninitialize();
        return;
    }
    double postedAt = 0;
    while (!quit_) {
        if (!want_ || stalled_) {
            // Nothing to show: no capture, no wakeups until the view asks again.
            if (started_) {
                client_->Stop();
                started_ = false;
                Trace(stalled_ ? "visualizer: parked (no frame taken)" : "visualizer: parked");
            }
            WaitForSingleObject(control_, INFINITE);
            continue;
        }
        if (!started_) {
            std::memset(ring_, 0, sizeof ring_);
            fresh_ = 0;
            lastAnalysis_ = NowSeconds();
            postedAt = 0;
            posted_ = false;
            started_ = SUCCEEDED(client_->Start());
            Trace(started_ ? "visualizer: capture started" : "visualizer: capture failed to start");
            if (!started_) {
                failed_ = true;
                break;
            }
        }
        HANDLE handles[] = {audio_, control_};
        WaitForMultipleObjects(2, handles, FALSE, 200);
        Read();
        const double now = NowSeconds();
        if (posted_ && postedAt > 0 && now - postedAt > kStallAfter) {
            stalled_ = true;  // SetRunning(true) from the next paint wakes it
            continue;
        }
        if (fresh_ * 1.0 / rate_ >= kFrameInterval * 0.9 && now - lastAnalysis_ >= kFrameInterval * 0.9 && !posted_) {
            Analyze(now - lastAnalysis_);
            lastAnalysis_ = now;
            posted_ = true;
            postedAt = now;
            if (notify_) PostMessageW(notify_, message_, 0, 0);
        }
    }
    Close();
    if (SUCCEEDED(co)) CoUninitialize();
}

bool LoopbackCapture::Open() {
    if (!ProcessLoopbackSupported()) return false;
    // mmdevapi.dll is loaded only when the visualizer is turned on (build.bat links no audio libs).
    const HMODULE mmdev = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto activate = mmdev ? reinterpret_cast<ActivateFn>(GetProcAddress(mmdev, "ActivateAudioInterfaceAsync"))
                                : nullptr;
    if (!activate) return false;
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    auto* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = activate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr)) {
        HANDLE wait[] = {handler->Done(), control_};
        for (;;) {
            const DWORD r = WaitForMultipleObjects(2, wait, FALSE, 5000);
            if (r == WAIT_OBJECT_0 + 1 && !quit_) continue;  // SetRunning while activating
            if (r == WAIT_OBJECT_0) {
                hr = handler->Result();
                if (SUCCEEDED(hr)) client_ = handler->TakeClient();
            } else {
                hr = E_ABORT;
            }
            break;
        }
    }
    if (op) op->Release();
    handler->Release();
    if (!client_) return false;
    // Process loopback has no mix format of its own: ask for float, the engine converts.
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = 8;
    fmt.nAvgBytesPerSec = 48000 * 8;
    const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &fmt, nullptr);
    if (FAILED(hr)) {
        // 16-bit PCM, as in Microsoft's sample; the tap at volume 0 then reads silence.
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = 48000 * 4;
        floatFormat_ = false;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &fmt, nullptr);
    }
    if (FAILED(hr)) return false;
    audio_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(client_->SetEventHandle(audio_))) return false;
    return SUCCEEDED(client_->GetService(IID_PPV_ARGS(&capture_)));
}

void LoopbackCapture::Close() {
    if (client_ && started_) client_->Stop();
    started_ = false;
    if (capture_) capture_->Release();
    capture_ = nullptr;
    if (client_) client_->Release();
    client_ = nullptr;
    if (audio_) CloseHandle(audio_);
    audio_ = nullptr;
}

void LoopbackCapture::Read() {
    UINT32 size = 0;
    while (SUCCEEDED(capture_->GetNextPacketSize(&size)) && size) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        for (UINT32 i = 0; i < frames; ++i) {
            float m = 0;
            if (!silent) {
                if (floatFormat_) {
                    const float* f = reinterpret_cast<const float*>(data) + i * 2;
                    m = (f[0] + f[1]) * 0.5f;
                } else {
                    const int16_t* s = reinterpret_cast<const int16_t*>(data) + i * 2;
                    m = (s[0] + s[1]) * (0.5f / 32768.0f);
                }
            }
            ring_[head_] = m;
            head_ = (head_ + 1) % kFftSize;
        }
        fresh_ += static_cast<int>(frames);
        captured_ += frames;
        capture_->ReleaseBuffer(frames);
    }
}

void LoopbackCapture::Analyze(double dt) {
    // The newest kFftSize samples in order, scaled back to the level before mpv's volume.
    const double g = gain_.load();
    const float inv = g > 1e-30 ? static_cast<float>(1.0 / g) : 0.0f;
    float peak = 0, energy = 0;
    const int freshCount = std::min(fresh_, kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        const float v = ring_[(head_ + i) % kFftSize] * inv;
        linear_[i] = std::isfinite(v) ? std::clamp(v, -4.0f, 4.0f) : 0.0f;
        if (i >= kFftSize - freshCount) {
            peak = std::max(peak, std::fabs(linear_[i]));
            energy += linear_[i] * linear_[i];
        }
    }
    fresh_ = 0;
    analyzer_.Analyze(linear_, bandsDb_);
    const float t = static_cast<float>(std::min(dt, 0.1));
    for (int b = 0; b < kBands; ++b) {
        const float target = BarLevel(bandsDb_[b]);
        work_.bands[b] = Smooth(work_.bands[b], target, t, 0.025f, 0.22f);
        UpdatePeak(drops_[b], work_.bands[b], t);
        work_.peaks[b] = drops_[b].value;
    }
    const float rms = freshCount ? std::sqrt(energy / static_cast<float>(freshCount)) : 0.0f;
    const float rmsDb = rms > 1e-9f ? 20.0f * std::log10(rms) : -180.0f;
    work_.level = Smooth(work_.level, std::clamp((rmsDb + 60.0f) / 57.0f, 0.0f, 1.0f), t, 0.03f, 0.3f);
    // The waveform history scrolls by one column per frame.
    std::memmove(work_.wave, work_.wave + 1, sizeof(float) * (kWaveColumns - 1));
    work_.wave[kWaveColumns - 1] = std::clamp(peak, 0.0f, 1.0f);
    // Oscilloscope: half the window from a rising zero crossing, with a slow automatic gain so
    // quiet music still fills the height.
    const int span = kScopePoints * 2;
    const int start = ScopeTrigger(linear_ + kFftSize - 2 * span, span);
    const float* s = linear_ + kFftSize - 2 * span + start;
    scopeGain_ = Smooth(scopeGain_, 0.9f / std::max(peak, 0.08f), t, 0.05f, 0.8f);
    for (int i = 0; i < kScopePoints; ++i) work_.scope[i] = std::clamp(s[i * 2] * scopeGain_, -1.0f, 1.0f);
    ++work_.serial;
    std::lock_guard lock(mu_);
    shared_ = work_;
}

}  // namespace audio
