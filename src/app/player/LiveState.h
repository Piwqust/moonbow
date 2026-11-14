#pragma once
// Session state behind the player popovers: mpv properties observed once (so panels never
// call synchronous getters) and the tweaks Moonbow manages itself (flip, stereo mode, crop,
// color effects, sleep timer, "stop after this item", setting presets).
#include "app/Signal.h"
#include "library/FileSettings.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace app {

struct AudioDevice {
    std::string name;  // mpv device id ("auto", "wasapi/{...}")
    std::wstring description;
};

struct LiveValues {
    double audioDelay = 0, subDelay = 0, subScale = 1, subPos = 100, subSpeed = 1;
    double brightness = 0, contrast = 0, saturation = 0, gamma = 0, hue = 0;
    double zoom = 0, rotate = 0, panX = 0, panY = 0;
    double abA = NAN, abB = NAN;  // A-B loop points, NaN = not set
    int64_t secondarySid = 0;
    bool subVisible = true;
    std::string audioDevice = "auto";
    std::vector<AudioDevice> devices;
};

class LiveState {
public:
    static LiveState& Get();
    ~LiveState();

    const LiveValues& V() const { return v_; }
    Signal<> changed;

    // Observes the mpv properties as soon as the player is ready (safe to call any time).
    void Start();
    void RefreshDevices();

    // --- Moonbow-managed video tweaks ---------------------------------------------------
    int flip = 0;                          // 0 none, 1 horizontal, 2 vertical
    std::string zoomMode = "fit";          // fit, quarter, half, original, double
    std::string aspect = "auto";           // auto, 16:9, 4:3, ...
    // Crop starts from the video.crop setting for every file; the quick settings change it for
    // the current one. Applied as mpv's video-crop (the GPU cuts the picture, no frame copies).
    std::string crop = "none";             // none, 16:9, 4:3, 21:9, 2.35:1, custom
    double cropT = 0, cropB = 0, cropL = 0, cropR = 0;  // custom crop, source pixels
    std::string deinterlace = "auto";      // auto, yes, no
    bool negate = false, grayscale = false;
    void SetFlip(int f);
    void SetZoomMode(const std::string& mode);
    void SetAspect(const std::string& a);
    void SetCrop(const std::string& c);
    void ApplyCrop();
    void SetDeinterlace(const std::string& d);
    const std::string& ToggleDeinterlace();  // returns the new value
    void SetColorFx(bool negate, bool grayscale);
    void ResetPicture();  // brightness... hue back to 0

    // AI upscaling / RTX Video HDR (video.superResolution, video.trueHdr): a d3d11vpp filter on
    // the hardware-decoded frames. Why it is not active right now, or empty when it is (or off).
    std::wstring UpscalerStatus() const;
    bool UpscalerActive() const { return !srFilter_.empty(); }
    // CPU (lavfi) video filters are on: the config's chain, flip or colour effects.
    bool CpuFilters() const;

    // --- audio / subtitles -------------------------------------------------------------
    std::string stereoMode = "stereo";  // stereo, mono, reverse, left, right
    void SetStereoMode(const std::string& m);
    bool subBackground = false;
    void SetSubBackground(bool on);

    // --- playback ----------------------------------------------------------------------
    int sleepMinutes = 0;  // 0 = off
    void SetSleepTimer(int minutes);
    bool stopAfter = false;  // pause at the end of the current item (one-shot)
    void SetStopAfter(bool on);

    // --- presets (ui.json "player.presets") -----------------------------------------------
    struct Preset {
        std::wstring name;
        std::string data;  // JSON object of property values
    };
    std::vector<Preset> Presets() const;
    int activePreset = -1;  // -1 = default
    void SavePreset(const std::wstring& name);
    void ApplyPreset(int index);
    void DeletePreset(int index);
    void ResetToDefaults();

    // --- per-file settings (playback.perFile, playback.perFileAll; library/FileSettings) ------
    // What changed for a file is stored while it plays and comes back the next time it opens,
    // through mpv's loadfile options (Player::fileOptions), so it ends with the file like the
    // round 5 per-file rule wants. Nothing runs while the setting is off.
    bool FileSettingsOn() const;   // playback.perFile
    bool FileSettingsAll() const;  // playback.perFileAll too: more than the tracks and delays
    // What is remembered for the playing file (the parts the settings above apply).
    library::FileOptions FileSettingsStored() const;
    // Re-reads the store from disk: this instance took the data folder over from one that exited.
    void ReloadFileSettings();
    // Forgets what is remembered for the playing file and returns it to the defaults.
    void ResetFileSettings();
    // Stores the last change for the playing file and writes the store now (the session ends).
    void FlushFileSettings();
    // The languages chosen in a series (`show`: NextEpisodeCard's key), kept in the same store and
    // under the same switches as the per-file settings; nullptr when off or nothing is stored.
    // `path`: the episode playing, for privacy.excludePatterns.
    const library::ShowLanguages* ShowLanguagesFor(const std::wstring& show, const std::wstring& path);
    void RememberShowLanguages(const std::wstring& show, const std::wstring& path, const library::ShowLanguages& langs);
    // The playing song's lyrics show this many seconds later (negative: earlier), for an .lrc that
    // runs off the song. Remembered for the file like the subtitle delay; 0 for each new file.
    double LyricsDelay() const { return lyricsDelay_; }
    void SetLyricsDelay(double seconds);

private:
    LiveState();
    void ObserveAll();
    void ApplyVideoFilters(bool force = false);  // force: don't wait for copy-back decoding
    void ApplyUpscaler();
    void ApplyHwdec();
    void LoadCropSetting();
    void ApplyAudioFilters();
    void Notify();   // something changed: repaint, and remember it for the file
    void Repaint();  // the panels show something new
    // Per-file settings.
    library::FileSettings& Store();  // loaded on first use
    std::string FileOptionsFor(const std::wstring& path);
    // A file loaded (`loaded`), or the settings were switched while it plays.
    void BeginFile(bool loaded);
    void FixTracks();
    void ScheduleCapture();
    void Capture(bool notify = true);
    void CaptureNow();
    void LeaveFile();
    void ScheduleSave();
    void ForgetAllFileSettings();

    LiveValues v_;
    bool started_ = false;
    bool notifyPosted_ = false;
    uint64_t deviceObserver_ = 0;
    uint64_t remainingObserver_ = 0;
    uintptr_t sleepTimer_ = 0;
    ScopedConnection playerConn_;
    uint64_t configToken_ = 0;
    int64_t srcW_ = 0, srcH_ = 0;  // decoded frame size (video-params/w, h): crop coordinates
    std::string srFilter_;         // the @lsr filter in mpv's chain, empty when none
    int64_t outW_ = 0, outH_ = 0;  // after the filters (video-out-params): video-crop's frame
    std::string appliedCrop_;      // last video-crop sent to mpv
    int64_t areaW_ = 0, areaH_ = 0;  // video area (osd-dimensions), the upscaler's target
    uintptr_t areaTimer_ = 0;
    std::string appliedHwdec_;     // hwdec mpv runs with (copy-back while CPU filters are on)
    bool vfPending_ = false;       // CPU filters wait for the decoder to switch to copy-back
    uintptr_t vfTimer_ = 0;        // applies them anyway if it never reports that
    // Per-file settings of the loaded file.
    library::FileSettings store_;  // only the loader thread touches it until Store() joins it
    std::thread loader_;
    std::wstring filePath_;
    uint64_t fileSerial_ = 0;      // its PlayerState::loadSerial
    bool fileOpen_ = false;        // changes are being remembered for it
    library::FileOptions fileOpts_;  // what is stored for it
    uint32_t trackBase_[3] = {};   // Player::TrackChoices when it loaded (or after our own picks)
    bool tracksTouched_[3] = {};   // audio, subtitles, secondary: the user chose one for this file
    bool trackFixPending_ = false; // the stored tracks are still to be checked, sidecars picked or added
    bool tracksTrusted_ = false;   // State().tracks is this file's (switched on mid-file)
    double lastSpeed_ = 0;
    double lyricsDelay_ = 0;
    uintptr_t captureTimer_ = 0, saveTimer_ = 0;
};

inline LiveState& Live() {
    return LiveState::Get();
}

}  // namespace app
