#pragma once
#include "MpvApi.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <thread>
#include <vector>

inline constexpr UINT WM_APP_MPV_READY  = WM_APP + 1;  // wParam: 1 = ok, 0 = failed
inline constexpr UINT WM_APP_MPV_WAKEUP = WM_APP + 2;

struct Track {
    int64_t id = 0;
    std::wstring type;  // "audio", "sub", "video"
    std::wstring title;
    std::wstring lang;
    std::wstring codec;
    bool selected = false;
    bool external = false;
    std::wstring file;  // external-filename for external tracks

    std::wstring Label() const;
};

struct Chapter {
    std::wstring title;
    double time = 0;
};

struct PlayerState {
    uint64_t loadSerial = 0;  // +1 on every MPV_EVENT_FILE_LOADED
    bool ready = false;
    bool idle = true;
    bool fileLoaded = false;
    bool paused = false;
    bool mute = false;
    bool eof = false;
    bool seekable = true;
    bool buffering = false;   // paused-for-cache
    double bufferPercent = 0; // cache-buffering-state
    double cacheTime = 0;     // demuxer-cache-time (seconds ahead)
    double position = 0;
    double duration = 0;
    double volume = 100;
    double speed = 1;
    int64_t videoW = 0;
    int64_t videoH = 0;
    int64_t chapter = -1;
    bool hasVideo = false;    // a real video track (not just cover art)
    bool hdr = false;         // PQ/HLG transfer
    std::wstring title;       // media-title
    std::wstring path;
    std::wstring artist, album;
    std::wstring hwdec;       // hwdec-current
    std::wstring lastError;
    std::vector<Track> tracks;
    std::vector<Chapter> chapters;
};

enum PlayerChange : unsigned {
    kChangeTime      = 1u << 0,
    kChangeState     = 1u << 1,
    kChangeTracks    = 1u << 2,
    kChangeVideoSize = 1u << 3,
    kChangeFile      = 1u << 4,
    kChangeError     = 1u << 5,
    kChangeVolume    = 1u << 6,
    kChangeChapters  = 1u << 7,
    kChangeMeta      = 1u << 8,
    kChangeEnd       = 1u << 9,  // file reached its end (not stopped by the user)
};

class Player {
public:
    ~Player();

    // Loads libmpv and initializes it on a worker thread, then posts
    // WM_APP_MPV_READY to notifyWnd. Video is rendered into videoWnd.
    void StartAsync(HWND notifyWnd, HWND videoWnd);
    // Call on the UI thread when WM_APP_MPV_READY arrives.
    bool FinishStart(bool ok);
    // Call on WM_APP_MPV_WAKEUP. Returns a PlayerChange mask.
    unsigned ProcessEvents();
    void Shutdown();

    bool Ready() const { return state_.ready; }
    const std::wstring& Error() const { return error_; }
    const PlayerState& State() const { return state_; }

    // mpv options applied before mpv_initialize (from Config). Call before StartAsync.
    void SetStartOptions(std::vector<std::pair<std::string, std::string>> options) { startOptions_ = std::move(options); }
    // privacy.rememberPosition: save the position when switching files and on exit.
    bool rememberPosition = true;

    // `start` > 0 begins playback at that position (seconds); otherwise mpv's
    // watch-later resume applies.
    void Open(const std::wstring& path, double start = -1);
    void Stop();
    void TogglePause();
    void SetPause(bool paused);
    void SeekAbsolute(double seconds, bool exact);
    void SeekRelative(double seconds);
    void SeekChapter(int delta);
    void FrameStep(bool back);
    void SetVolume(double volume);
    void SetMute(bool mute);
    void ToggleMute();
    void SetSpeed(double speed);
    void SetTrack(const char* prop, int64_t id);  // prop "aid"/"sid"/"vid", id 0 = off
    void CycleTrack(const char* prop);
    void AddSubtitle(const std::wstring& path);
    void AddAudio(const std::wstring& path);
    void Screenshot(const std::wstring& file = {});
    // Starts writing the stream to `file` (empty stops). mpv "stream-record".
    void SetRecording(const std::wstring& file);
    void SaveWatchLater();
    bool NearEnd() const;

    // Generic access for feature panels (filters, delays, zoom...). All async.
    void Command(const std::vector<std::string>& args);
    void SetProperty(const char* name, double value);
    void SetProperty(const char* name, bool value);
    void SetProperty(const char* name, const std::string& value);
    // Synchronous read; only for rarely used values (never in paint).
    std::string GetPropertyString(const char* name);
    double GetPropertyDouble(const char* name, double fallback = 0);
    // Observes any property; `cb` runs on the UI thread inside ProcessEvents.
    // Returns an id for Unobserve.
    using PropertyCallback = std::function<void(const mpv_event_property&)>;
    uint64_t Observe(const char* name, mpv_format format, PropertyCallback cb);
    void Unobserve(uint64_t id);

    mpv_handle* Handle() const { return mpv_; }
    const MpvApi& Api() const { return api_; }

private:
    bool InitOnWorker(int64_t wid);
    void Cmd(std::initializer_list<const char*> args, bool async = true);
    void SetDouble(const char* name, double value);
    void SetString(const char* name, const std::string& value);
    unsigned OnProperty(const mpv_event_property* prop, uint64_t id);
    static void Wakeup(void* ctx);

    MpvApi api_;
    mpv_handle* mpv_ = nullptr;
    std::thread init_;
    std::wstring error_;
    PlayerState state_;
    HWND notify_ = nullptr;
    std::atomic<bool> wakePending_{false};
    std::vector<std::pair<std::string, std::string>> startOptions_;
    std::map<uint64_t, PropertyCallback> observers_;
    uint64_t nextObserver_ = 1000;
};
