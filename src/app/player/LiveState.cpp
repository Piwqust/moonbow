#include "app/player/LiveState.h"

#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "core/Json.h"
#include "ui/Controls.h"

#include <cstdlib>

namespace app {

namespace {

Player& Mpv() {
    return TheApp().player();
}

std::string Num(double v) {
    return json::FormatNumber(v);
}

// "16:9" -> 1.7777; 0 when not a ratio.
double Ratio(const std::string& s) {
    const size_t colon = s.find(':');
    if (colon == std::string::npos) return std::atof(s.c_str());
    const double a = std::atof(s.substr(0, colon).c_str()), b = std::atof(s.substr(colon + 1).c_str());
    return b > 0 ? a / b : 0;
}

constexpr const char* kPresetsKey = "player.presets";

}  // namespace

LiveState& LiveState::Get() {
    static LiveState s;
    return s;
}

LiveState::LiveState() {
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (!started_ && Mpv().Ready()) Start();
        if ((m & kChangeVideoSize) && crop != "none") ApplyCrop();
    });
    // Config writes the whole vf/af chains when mapped settings change; put ours back on top.
    configToken_ = TheApp().config().Subscribe("", [this](const std::string& key) {
        if (key.rfind("video.", 0) == 0 || key.rfind("performance.", 0) == 0) ApplyVideoFilters();
        if (key.rfind("audio.", 0) == 0) ApplyAudioFilters();
    });
}

void LiveState::Start() {
    if (started_ || !Mpv().Ready()) return;
    started_ = true;
    ObserveAll();
}

void LiveState::Notify() {
    // Several properties arrive in one ProcessEvents batch; repaint once.
    if (notifyPosted_) return;
    MainWindow* w = TheApp().main();
    if (!w) {
        changed.Emit();
        return;
    }
    notifyPosted_ = true;
    w->Post([this] {
        notifyPosted_ = false;
        changed.Emit();
    });
}

void LiveState::ObserveAll() {
    Player& p = Mpv();
    auto num = [this, &p](const char* name, double LiveValues::*field) {
        p.Observe(name, MPV_FORMAT_DOUBLE, [this, field](const mpv_event_property& e) {
            if (e.format == MPV_FORMAT_DOUBLE && e.data) v_.*field = *static_cast<double*>(e.data);
            Notify();
        });
    };
    num("audio-delay", &LiveValues::audioDelay);
    num("sub-delay", &LiveValues::subDelay);
    num("sub-scale", &LiveValues::subScale);
    num("sub-pos", &LiveValues::subPos);
    num("sub-speed", &LiveValues::subSpeed);
    num("brightness", &LiveValues::brightness);
    num("contrast", &LiveValues::contrast);
    num("saturation", &LiveValues::saturation);
    num("gamma", &LiveValues::gamma);
    num("hue", &LiveValues::hue);
    num("video-zoom", &LiveValues::zoom);
    num("video-rotate", &LiveValues::rotate);
    auto ab = [this, &p](const char* name, double LiveValues::*field) {
        p.Observe(name, MPV_FORMAT_STRING, [this, field](const mpv_event_property& e) {
            double v = NAN;
            if (e.format == MPV_FORMAT_STRING && e.data) {
                const char* s = *static_cast<char**>(e.data);
                if (s && std::strcmp(s, "no") != 0) v = std::atof(s);
            }
            v_.*field = v;
            Notify();
        });
    };
    ab("ab-loop-a", &LiveValues::abA);
    ab("ab-loop-b", &LiveValues::abB);
    p.Observe("secondary-sid", MPV_FORMAT_STRING, [this](const mpv_event_property& e) {
        int64_t id = 0;
        if (e.format == MPV_FORMAT_STRING && e.data) {
            const char* s = *static_cast<char**>(e.data);
            if (s) id = std::atoll(s);
        }
        v_.secondarySid = id;
        Notify();
    });
    p.Observe("sub-visibility", MPV_FORMAT_FLAG, [this](const mpv_event_property& e) {
        if (e.format == MPV_FORMAT_FLAG && e.data) v_.subVisible = *static_cast<int*>(e.data) != 0;
        Notify();
    });
    p.Observe("audio-device", MPV_FORMAT_STRING, [this](const mpv_event_property& e) {
        if (e.format == MPV_FORMAT_STRING && e.data) {
            const char* s = *static_cast<char**>(e.data);
            v_.audioDevice = s ? s : "auto";
        }
        Notify();
    });
    RefreshDevices();
}

void LiveState::RefreshDevices() {
    if (!started_) return;
    Player& p = Mpv();
    if (deviceObserver_) p.Unobserve(deviceObserver_);
    // Node properties arrive as JSON text when observed as strings.
    deviceObserver_ = p.Observe("audio-device-list", MPV_FORMAT_STRING, [this](const mpv_event_property& e) {
        if (e.format != MPV_FORMAT_STRING || !e.data) return;
        const char* s = *static_cast<char**>(e.data);
        if (!s) return;
        auto doc = json::Parse(s);
        if (!doc || !doc->IsArray()) return;
        v_.devices.clear();
        for (const json::Value& d : doc->AsArray()) {
            AudioDevice dev{d.GetString("name"), d.GetWString("description")};
            if (!dev.name.empty()) v_.devices.push_back(std::move(dev));
        }
        Notify();
    });
}

// --- video --------------------------------------------------------------------------------

void LiveState::ApplyVideoFilters() {
    if (!Mpv().Ready()) return;
    Mpv().Command({"vf", "remove", "@lflip"});
    if (flip == 1) Mpv().Command({"vf", "add", "@lflip:hflip"});
    if (flip == 2) Mpv().Command({"vf", "add", "@lflip:vflip"});
    Mpv().Command({"vf", "remove", "@lfx"});
    std::string fx;
    if (negate) fx = "negate";
    if (grayscale) fx += std::string(fx.empty() ? "" : ",") + "hue=s=0";
    if (!fx.empty()) Mpv().Command({"vf", "add", "@lfx:lavfi=[" + fx + "]"});
}

void LiveState::SetFlip(int f) {
    flip = f;
    ApplyVideoFilters();
    Notify();
}

void LiveState::SetColorFx(bool n, bool g) {
    negate = n;
    grayscale = g;
    ApplyVideoFilters();
    Notify();
}

void LiveState::SetZoomMode(const std::string& mode) {
    zoomMode = mode;
    const bool unscaled = mode != "fit";
    Mpv().SetProperty("video-unscaled", std::string(unscaled ? "yes" : "no"));
    double z = 0;
    if (mode == "quarter") z = -2;
    if (mode == "half") z = -1;
    if (mode == "double") z = 1;
    Mpv().SetProperty("video-zoom", z);
    Notify();
}

void LiveState::SetAspect(const std::string& a) {
    aspect = a;
    Mpv().SetProperty("video-aspect-override", a == "auto" ? std::string("-1") : a);
    Notify();
}

void LiveState::SetCrop(const std::string& c) {
    crop = c;
    ApplyCrop();
    Notify();
}

void LiveState::ApplyCrop() {
    const PlayerState& s = Mpv().State();
    const double w = static_cast<double>(s.videoW), h = static_cast<double>(s.videoH);
    std::string value;  // empty = no crop
    if (w > 0 && h > 0) {
        if (crop == "custom") {
            const double cw = w - cropL - cropR, ch = h - cropT - cropB;
            if ((cropL + cropR + cropT + cropB) > 0 && cw > 16 && ch > 16)
                value = Num(std::round(cw)) + "x" + Num(std::round(ch)) + "+" + Num(cropL) + "+" + Num(cropT);
        } else if (crop != "none") {
            const double r = Ratio(crop);
            if (r > 0) {
                double cw = w, ch = std::round(w / r);
                if (ch > h) {
                    ch = h;
                    cw = std::round(h * r);
                }
                value = Num(cw) + "x" + Num(ch) + "+" + Num(std::round((w - cw) / 2)) + "+" + Num(std::round((h - ch) / 2));
            }
        }
    }
    Mpv().SetProperty("video-crop", value.empty() ? std::string("0x0+0+0") : value);
}

void LiveState::SetDeinterlace(const std::string& d) {
    deinterlace = d;
    Mpv().SetProperty("deinterlace", d);
    Notify();
}

void LiveState::SetToneMapping(const std::string& t) {
    toneMapping = t;
    Mpv().SetProperty("tone-mapping", t);
    Notify();
}

void LiveState::ResetPicture() {
    for (const char* name : {"brightness", "contrast", "saturation", "gamma", "hue"}) Mpv().SetProperty(name, 0.0);
    SetColorFx(false, false);
}

// --- audio / subtitles ----------------------------------------------------------------------

void LiveState::ApplyAudioFilters() {
    if (!Mpv().Ready()) return;
    Mpv().Command({"af", "remove", "@lstereo"});
    std::string pan;
    if (stereoMode == "mono") pan = "pan=stereo|c0=0.5*c0+0.5*c1|c1=0.5*c0+0.5*c1";
    if (stereoMode == "reverse") pan = "pan=stereo|c0=c1|c1=c0";
    if (stereoMode == "left") pan = "pan=stereo|c0=c0|c1=c0";
    if (stereoMode == "right") pan = "pan=stereo|c0=c1|c1=c1";
    if (!pan.empty()) Mpv().Command({"af", "add", "@lstereo:lavfi=[" + pan + "]"});
}

void LiveState::SetStereoMode(const std::string& m) {
    stereoMode = m;
    ApplyAudioFilters();
    Notify();
}

void LiveState::SetSubBackground(bool on) {
    subBackground = on;
    Mpv().SetProperty("sub-border-style", std::string(on ? "background-box" : "outline-and-shadow"));
    Mpv().SetProperty("sub-back-color", std::string(on ? "#99000000" : "#00000000"));
    Notify();
}

// --- playback -------------------------------------------------------------------------------

void LiveState::SetSleepTimer(int minutes) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    if (sleepTimer_) w->CancelTimer(sleepTimer_);
    sleepTimer_ = 0;
    sleepMinutes = minutes;
    if (minutes > 0) {
        sleepTimer_ = w->StartTimer(static_cast<UINT>(minutes) * 60000u, [this, w] {
            sleepTimer_ = 0;
            sleepMinutes = 0;
            Mpv().SetPause(true);
            ui::ShowToast(w, Tr(L"Sleep timer: playback paused"), "recent", 2500);
            Notify();
        });
    }
    Notify();
}

void LiveState::SetStopAfter(bool on) {
    stopAfter = on;
    Player& p = Mpv();
    if (remainingObserver_) p.Unobserve(remainingObserver_);
    remainingObserver_ = 0;
    if (on && p.Ready()) {
        remainingObserver_ = p.Observe("time-remaining", MPV_FORMAT_DOUBLE, [this](const mpv_event_property& e) {
            if (!stopAfter || e.format != MPV_FORMAT_DOUBLE || !e.data) return;
            const double left = *static_cast<double*>(e.data);
            const PlayerState& s = Mpv().State();
            if (s.fileLoaded && !s.paused && s.duration > 1 && left < 0.25) {
                Mpv().SetPause(true);
                MainWindow* w = TheApp().main();
                if (w)
                    w->Post([this, w] {
                        SetStopAfter(false);
                        ui::ShowToast(w, Tr(L"Stopped after this item"), "stop", 2000);
                    });
            }
        });
    }
    Notify();
}

// --- presets --------------------------------------------------------------------------------

std::vector<LiveState::Preset> LiveState::Presets() const {
    std::vector<Preset> out;
    auto doc = json::Parse(TheApp().UiValue(kPresetsKey, "[]"));
    if (!doc || !doc->IsArray()) return out;
    for (const json::Value& v : doc->AsArray()) {
        const json::Value* values = v.Find("values");
        out.push_back({v.GetWString("name"), values ? json::Serialize(*values) : "{}"});
    }
    return out;
}

void LiveState::SavePreset(const std::wstring& name) {
    const PlayerState& s = Mpv().State();
    json::Value values = json::Value::MakeObject();
    values.Set("speed", s.speed);
    values.Set("audio-delay", v_.audioDelay);
    values.Set("sub-delay", v_.subDelay);
    values.Set("sub-scale", v_.subScale);
    values.Set("sub-pos", v_.subPos);
    values.Set("brightness", v_.brightness);
    values.Set("contrast", v_.contrast);
    values.Set("saturation", v_.saturation);
    values.Set("gamma", v_.gamma);
    values.Set("hue", v_.hue);
    values.Set("video-rotate", v_.rotate);
    values.Set("zoom", zoomMode);
    values.Set("aspect", aspect);
    values.Set("flip", flip);
    values.Set("stereo", stereoMode);
    auto doc = json::Parse(TheApp().UiValue(kPresetsKey, "[]"));
    json::Value list = doc && doc->IsArray() ? *doc : json::Value::MakeArray();
    json::Value entry = json::Value::MakeObject();
    entry.Set("name", json::ToUtf8(name));
    entry.Set("values", std::move(values));
    list.Push(std::move(entry));
    TheApp().SetUiValue(kPresetsKey, json::Serialize(list));
    activePreset = static_cast<int>(list.size()) - 1;
    Notify();
}

void LiveState::ApplyPreset(int index) {
    const auto presets = Presets();
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    auto v = json::Parse(presets[static_cast<size_t>(index)].data);
    if (!v || !v->IsObject()) return;
    Player& p = Mpv();
    p.SetSpeed(v->GetNumber("speed", 1));
    for (const char* name : {"audio-delay", "sub-delay", "brightness", "contrast", "saturation", "gamma", "hue",
                             "video-rotate"})
        p.SetProperty(name, v->GetNumber(name, 0));
    p.SetProperty("sub-scale", v->GetNumber("sub-scale", 1));
    p.SetProperty("sub-pos", v->GetNumber("sub-pos", 100));
    SetZoomMode(v->GetString("zoom", "fit"));
    SetAspect(v->GetString("aspect", "auto"));
    flip = static_cast<int>(v->GetInt("flip", 0));
    ApplyVideoFilters();
    SetStereoMode(v->GetString("stereo", "stereo"));
    activePreset = index;
    Notify();
}

void LiveState::DeletePreset(int index) {
    auto doc = json::Parse(TheApp().UiValue(kPresetsKey, "[]"));
    if (!doc || !doc->IsArray() || index < 0 || index >= static_cast<int>(doc->size())) return;
    doc->MutArray().erase(doc->MutArray().begin() + index);
    TheApp().SetUiValue(kPresetsKey, json::Serialize(*doc));
    if (activePreset == index) activePreset = -1;
    else if (activePreset > index) --activePreset;
    Notify();
}

void LiveState::ResetToDefaults() {
    Player& p = Mpv();
    p.SetSpeed(1);
    for (const char* name : {"audio-delay", "sub-delay", "video-rotate"}) p.SetProperty(name, 0.0);
    p.SetProperty("sub-scale", 1.0);
    p.SetProperty("sub-pos", 100.0);
    ResetPicture();
    SetZoomMode("fit");
    SetAspect("auto");
    SetCrop("none");
    flip = 0;
    ApplyVideoFilters();
    SetStereoMode("stereo");
    activePreset = -1;
    Notify();
}

}  // namespace app
