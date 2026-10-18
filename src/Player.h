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
    int channels = 0;   // audio: demux-channel-count, 0 when unknown
    bool selected = false;
    bool external = false;
    std::wstring file;  // external-filename for external tracks

    std::wstring Label() const;  // Name() + " · codec" (menus, media info)
    std::wstring Name() const;   // "Title (lang)", the external file name, the language or "#id" (OSD)
    bool TitleFromFileName() const;  // an external track whose best name is its file name
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
    std::wstring failedPath;   // the file the last kChangeError is about (empty once a file loads)
    std::wstring skippedPath;  // with kChangeAdvance: the file that failed, so mpv went on to the next
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
    kChangeAdvance   = 1u << 10, // mpv moved on to the file given to QueueNext (gapless)
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
    void Open(const std::wstring& path);
    void Stop();
    // The file to play when the current one ends, kept in mpv's playlist so mpv prefetches it
    // and plays it gaplessly (gapless-audio, prefetch-playlist). Empty = stop at the end.
    // Open() and Stop() forget it. When mpv starts it, ProcessEvents reports kChangeAdvance.
    void QueueNext(const std::wstring& path);
    bool HasQueuedNext() const { return !queuedNext_.empty(); }
    // Duration of the file that ended, and the file mpv went on to, when kChangeAdvance was reported.
    double AdvancedFromDuration() const { return advancedDuration_; }
    const std::wstring& AdvancedPath() const { return advancedPath_; }
    // Drops the resume point of a file that played to its end.
    void ForgetPosition(const std::wstring& path);
    // hwdec value that decodes on the same device but copies frames back to RAM, for CPU
    // filters ("d3d11va,d3d11va-copy" -> "d3d11va-copy", "auto" -> "auto-copy").
    static std::string CopyBackHwdec(const std::string& hwdec);
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
    // withSubtitles: burn the visible subtitles into the image.
    void Screenshot(const std::wstring& file = {}, bool withSubtitles = true);
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
    double volumeMax_ = 150;  // mpv volume-max (Settings > Audio > Maximum volume)
    decltype(&::mpv_request_log_messages) requestLog_ = nullptr;  // not in MpvApi: only passthrough needs it
    bool spdifLog_ = false;  // mpv log messages requested while audio-spdif is set
    std::map<uint64_t, PropertyCallback> observers_;
    std::wstring queuedNext_;   // QueueNext's file, empty when none
    int64_t queuedEntry_ = 0;   // its mpv playlist entry id, 0 until loadfile replies
    uint64_t queueGen_ = 0;     // tells the reply of the latest append from stale ones
    // Every entry appended since the last Open/Stop: mpv may already have moved on to one that a
    // later QueueNext replaced (the queue was edited at the very end of a file).
    std::vector<std::pair<int64_t, std::wstring>> appended_;
    std::wstring advancedPath_;
    std::wstring startedPath_;  // the file of the latest MPV_EVENT_START_FILE (Open or an appended entry)
    double advancedDuration_ = 0;
    double endedDuration_ = 0;  // duration of the file at its MPV_EVENT_END_FILE
    uint64_t nextObserver_ = 1000;
};
