#include "audio/TrackWave.h"

#include "MpvApi.h"
#include "Player.h"
#include "Util.h"

#include <cstdio>

namespace audio {

namespace {

std::wstring TempPcmPath() {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    static LONG counter = 0;
    wchar_t name[96];
    swprintf_s(name, L"moonbow-wave-%lu-%ld.pcm", GetCurrentProcessId(), InterlockedIncrement(&counter));
    return std::wstring(dir) + name;
}

}  // namespace

TrackWave::~TrackWave() {
    {
        std::lock_guard lock(mu_);
        quit_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

const std::vector<float>* TrackWave::Find(const std::wstring& path, double duration, HWND notify, UINT message) {
    if (path.empty() || path.find(L"://") != std::wstring::npos) return nullptr;  // streams: no second download
    std::lock_guard lock(mu_);
    const auto it = done_.find(path);
    if (it != done_.end()) return it->second.empty() ? nullptr : &it->second;
    if (want_ != path) {
        want_ = path;
        wantDuration_ = duration;
        notify_ = notify;
        message_ = message;
        if (!worker_.joinable()) worker_ = std::thread([this] { Run(); });
        wake_.notify_all();
    }
    return nullptr;
}

void TrackWave::Run() {
    std::unique_lock lock(mu_);
    for (;;) {
        wake_.wait(lock, [this] { return quit_ || (!want_.empty() && !done_.count(want_)); });
        if (quit_) return;
        const std::wstring path = want_;
        const double duration = wantDuration_;
        lock.unlock();
        std::vector<float> wave = Scan(path, duration);
        lock.lock();
        if (quit_) return;
        if (done_.size() >= 64) done_.erase(done_.begin());  // a few KB each; any old one may go
        // A scan cut short for a newer song is not a failure: it runs again when asked.
        if (!wave.empty() || want_ == path) done_[path] = std::move(wave);
        if (notify_) PostMessageW(notify_, message_, 0, 0);
    }
}

std::vector<float> TrackWave::Scan(const std::wstring& path, double duration) {
    mpv_handle* h = api_.mpv_create ? api_.mpv_create() : nullptr;
    if (!h) return {};
    const std::wstring pcm = TempPcmPath();
    // A low rate keeps a long mix small (an hour at 1 kHz is 7 MB); the bars only need the envelope.
    const int rate = duration > 900 ? 1000 : 4000;
    SetQuietMpvOptions(api_, h);
    auto opt = [&](const char* name, const std::string& value) { api_.mpv_set_option_string(h, name, value.c_str()); };
    for (const char* name : {"vid", "sid", "audio-display", "keep-open", "idle", "gapless-audio", "prefetch-playlist",
                             "replaygain"})
        opt(name, "no");
    opt("vo", "null");
    opt("ao", "pcm");
    opt("ao-pcm-file", WideToUtf8(pcm));
    opt("ao-pcm-waveheader", "no");
    opt("untimed", "yes");  // as fast as it decodes
    opt("audio-format", "s16");
    opt("audio-channels", "mono");
    opt("audio-samplerate", std::to_string(rate));
    bool ok = api_.mpv_initialize(h) >= 0;
    if (ok) {
        const std::string file = WideToUtf8(path);
        const char* load[] = {"loadfile", file.c_str(), "replace", nullptr};
        ok = api_.mpv_command(h, load) >= 0;
    }
    const ULONGLONG deadline = GetTickCount64() + 120000;
    while (ok) {
        const mpv_event* ev = api_.mpv_wait_event(h, 0.1);
        if (ev->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<const mpv_event_end_file*>(ev->data);
            ok = end && end->reason == MPV_END_FILE_REASON_EOF;
            break;
        }
        if (ev->event_id == MPV_EVENT_SHUTDOWN) ok = false;
        std::lock_guard lock(mu_);
        if (quit_ || want_ != path || GetTickCount64() > deadline) ok = false;
    }
    api_.mpv_terminate_destroy(h);  // closes the PCM file
    std::vector<float> wave;
    if (ok) {
        std::string bytes;
        FILE* f = nullptr;
        if (_wfopen_s(&f, pcm.c_str(), L"rb") == 0 && f) {
            char buf[65536];
            size_t got;
            while ((got = fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, got);
            fclose(f);
        }
        wave = ReduceWave(reinterpret_cast<const int16_t*>(bytes.data()), bytes.size() / 2, kTrackWaveColumns);
    }
    DeleteFileW(pcm.c_str());
    return wave;
}

}  // namespace audio
