#pragma once
// Session state behind the player popovers: mpv properties observed once (so panels never
// call synchronous getters) and the tweaks Lumen manages itself (flip, stereo mode, crop,
// color effects, sleep timer, "stop after this item", setting presets).
#include "app/Signal.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct AudioDevice {
    std::string name;  // mpv device id ("auto", "wasapi/{...}")
    std::wstring description;
};

struct LiveValues {
    double audioDelay = 0, subDelay = 0, subScale = 1, subPos = 100, subSpeed = 1;
    double brightness = 0, contrast = 0, saturation = 0, gamma = 0, hue = 0;
    double zoom = 0, rotate = 0;
    double abA = NAN, abB = NAN;  // A-B loop points, NaN = not set
    int64_t secondarySid = 0;
    bool subVisible = true;
    std::string audioDevice = "auto";
    std::vector<AudioDevice> devices;
};

class LiveState {
public:
    static LiveState& Get();

    const LiveValues& V() const { return v_; }
    Signal<> changed;

    // Observes the mpv properties as soon as the player is ready (safe to call any time).
    void Start();
    void RefreshDevices();

    // --- Lumen-managed video tweaks ---------------------------------------------------
    int flip = 0;                          // 0 none, 1 horizontal, 2 vertical
    std::string zoomMode = "fit";          // fit, quarter, half, original, double
    std::string aspect = "auto";           // auto, 16:9, 4:3, ...
    std::string crop = "none";             // none, 16:9, 4:3, 21:9, 2.35:1, custom
    double cropT = 0, cropB = 0, cropL = 0, cropR = 0;  // custom crop, source pixels
    std::string deinterlace = "auto";      // auto, yes, no
    std::string toneMapping = "auto";
    bool negate = false, grayscale = false;
    void SetFlip(int f);
    void SetZoomMode(const std::string& mode);
    void SetAspect(const std::string& a);
    void SetCrop(const std::string& c);
    void ApplyCrop();
    void SetDeinterlace(const std::string& d);
    void SetToneMapping(const std::string& t);
    void SetColorFx(bool negate, bool grayscale);
    void ResetPicture();  // brightness... hue back to 0

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

private:
    LiveState();
    void ObserveAll();
    void ApplyVideoFilters();
    void ApplyAudioFilters();
    void Notify();

    LiveValues v_;
    bool started_ = false;
    bool notifyPosted_ = false;
    uint64_t deviceObserver_ = 0;
    uint64_t remainingObserver_ = 0;
    uintptr_t sleepTimer_ = 0;
    ScopedConnection playerConn_;
    uint64_t configToken_ = 0;
};

inline LiveState& Live() {
    return LiveState::Get();
}

}  // namespace app
