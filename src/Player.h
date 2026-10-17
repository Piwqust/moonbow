#pragma once
#include "MpvApi.h"

#include <atomic>
#include <cstdint>
#include <initializer_list>
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

    std::wstring Label() const;
};

struct PlayerState {
    bool ready = false;
    bool idle = true;
    bool fileLoaded = false;
    bool paused = false;
    bool mute = false;
    bool eof = false;
    double position = 0;
    double duration = 0;
    double volume = 100;
    double speed = 1;
    int64_t videoW = 0;
    int64_t videoH = 0;
    std::wstring title;
    std::wstring path;
    std::wstring lastError;
    std::vector<Track> tracks;
};

enum PlayerChange : unsigned {
    kChangeTime      = 1u << 0,
    kChangeState     = 1u << 1,
    kChangeTracks    = 1u << 2,
    kChangeVideoSize = 1u << 3,
    kChangeFile      = 1u << 4,
    kChangeError     = 1u << 5,
    kChangeVolume    = 1u << 6,
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

    void Open(const std::wstring& path);
    void Stop();
    void TogglePause();
    void SeekAbsolute(double seconds, bool exact);
    void SeekRelative(double seconds);
    void FrameStep(bool back);
    void SetVolume(double volume);
    void ToggleMute();
    void SetSpeed(double speed);
    void SetTrack(const char* prop, int64_t id);  // prop "aid"/"sid", id 0 = off
    void CycleTrack(const char* prop);
    void Screenshot();
    void SaveWatchLater();

private:
    bool InitOnWorker(int64_t wid);
    void Command(std::initializer_list<const char*> args, bool async = true);
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
};
