#include "app/player/LiveState.h"

#include "Trace.h"
#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "core/Json.h"
#include "core/SettingsSchema.h"
#include "gfx/Gpu.h"
#include "library/MediaTypes.h"
#include "ui/Controls.h"

#include <dxgi1_6.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

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

Config& Cfg() {
    return TheApp().config();
}

// Video Super Resolution flavour of the graphics card Lumen (and mpv, on the default adapter)
// renders with: "nvidia", "intel" or "" when it has none. Asked once the device exists.
const char* SuperResVendor() {
    static int vendor = -1;
    if (vendor < 0) {
        IDXGIDevice* dev = gfx::Gpu::Get().DxgiDevice();
        if (!dev) return "";
        vendor = 0;
        gfx::ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(dev->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc)))
            vendor = static_cast<int>(desc.VendorId);
    }
    if (vendor == 0x10DE) return "nvidia";
    if (vendor == 0x8086) return "intel";
    return "";
}

// True when Windows HDR is on for the monitor: its DXGI output (on any adapter, so hybrid
// laptops work too) reports the PQ colour space. Cached per monitor; ForgetDisplayHdr() clears it
// when a file starts, so a toggle in Windows settings applies from the next file.
HMONITOR g_hdrMonitor = nullptr;
int g_hdrState = -1;  // -1 unknown, 0 SDR, 1 HDR

void ForgetDisplayHdr() {
    g_hdrState = -1;
}

bool DisplayIsHdr(HMONITOR mon) {
    if (mon == g_hdrMonitor && g_hdrState >= 0) return g_hdrState == 1;
    g_hdrMonitor = mon;
    g_hdrState = 0;
    IDXGIDevice* dev = gfx::Gpu::Get().DxgiDevice();
    gfx::ComPtr<IDXGIAdapter> own;
    gfx::ComPtr<IDXGIFactory1> factory;
    if (!dev || FAILED(dev->GetAdapter(&own)) || FAILED(own->GetParent(IID_PPV_ARGS(&factory)))) return false;
    gfx::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++a) {
        gfx::ComPtr<IDXGIOutput> out;
        for (UINT o = 0; adapter->EnumOutputs(o, out.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o) {
            gfx::ComPtr<IDXGIOutput6> out6;
            DXGI_OUTPUT_DESC1 desc{};
            if (SUCCEEDED(out.As(&out6)) && SUCCEEDED(out6->GetDesc1(&desc)) && desc.Monitor == mon) {
                g_hdrState = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? 1 : 0;
                return g_hdrState == 1;
            }
        }
    }
    return false;
}

HMONITOR VideoMonitor() {
    MainWindow* w = TheApp().main();
    return w ? MonitorFromWindow(w->Hwnd(), MONITOR_DEFAULTTONEAREST) : nullptr;
}

// True when the config's vf chain has CPU filters (a deinterlacing method, deblocking...).
bool ConfigCpuFilters() {
    for (const auto& [name, value] : MpvOptionFor(Cfg(), "performance.postprocessing"))
        if (name == "vf") return !value.empty();
    return false;
}

// The configured hwdec (performance.hwdec) as mpv gets it.
std::string ConfigHwdec() {
    for (const auto& [name, value] : MpvOptionFor(Cfg(), "performance.hwdec"))
        if (name == "hwdec") return value;
    return settings::kAutoHwdec;
}

// --- per-file settings: the values a file opens with when nothing is stored for it ------------

// What a setting maps to for one mpv option, or `fallback`.
std::string ConfigOption(const char* key, const char* option, const char* fallback) {
    for (const auto& [name, value] : MpvOptionFor(Cfg(), key))
        if (name == option) return value;
    return fallback;
}

double DefaultSpeed() {
    const double s = Cfg().GetDouble("playback.speed");
    return s > 0 ? s : 1.0;
}

double DefaultSubPos() {
    return Cfg().GetDouble("subtitles.position");
}

std::string DefaultDeinterlace() {
    return ConfigOption("video.deinterlace", "deinterlace", "no");
}

// video-zoom of a LiveState zoom mode (they also set video-unscaled).
double ModeZoom(const std::string& mode) {
    if (mode == "quarter") return -2;
    if (mode == "half") return -1;
    if (mode == "double") return 1;
    return 0;
}

bool ExcludedFromMemory(const std::wstring& path) {
    return TheApp().history().IsExcluded(path);
}

// Settings whose mpv mapping rewrites the whole vf chain (dropping Lumen's labelled filters).
bool RewritesVf(const std::string& key) {
    const settings::Item* item = settings::FindItem(key);
    if (!item) return false;
    for (const settings::MpvMap& m : item->mpv)
        if (m.option == "vf") return true;
    return false;
}

}  // namespace

LiveState& LiveState::Get() {
    static LiveState s;
    return s;
}

LiveState::LiveState() {
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (!started_ && Mpv().Ready()) Start();
        // Crop goes back to its setting for every file.
        static std::wstring lastPath;
        if ((m & kChangeFile) && Mpv().State().path != lastPath) {
            lastPath = Mpv().State().path;
            LoadCropSetting();
            ApplyCrop();
        }
        if ((m & kChangeFile) && Mpv().FileSettled() && Mpv().State().loadSerial != fileSerial_) BeginFile(true);
        if (fileOpen_ && (m & kChangeTracks)) {
            if (trackFixPending_) FixTracks();
            ScheduleCapture();
        }
        if (fileOpen_ && (m & kChangeState) && Mpv().State().speed != lastSpeed_) {
            lastSpeed_ = Mpv().State().speed;
            ScheduleCapture();
        }
        // hwdec-current and the HDR flag arrive as state changes.
        if (m & kChangeFile) ForgetDisplayHdr();
        if (m & (kChangeState | kChangeFile)) ApplyUpscaler();
    });
    LoadCropSetting();
    // Config writes the whole vf/af chains when mapped settings change; put ours back on top.
    configToken_ = TheApp().config().Subscribe("", [this](const std::string& key) {
        if (RewritesVf(key)) {
            srFilter_.clear();  // gone with the old chain
            ApplyVideoFilters();
        }
        if (key == "performance.hwdec") {
            appliedHwdec_ = ConfigHwdec();  // what the app just set; adjust it for CPU filters
            ApplyHwdec();
        }
        if (key.rfind("video.crop", 0) == 0) {
            LoadCropSetting();
            ApplyCrop();
            Notify();
        }
        if (key == "video.superResolution" || key == "video.trueHdr") {
            ApplyUpscaler();
            Notify();
        }
        if (key.rfind("audio.", 0) == 0) ApplyAudioFilters();
        if (key == "playback.perFile" || key == "playback.perFileAll") {
            Mpv().confirmTracks = FileSettingsOn();
            // From now on for the playing file too (what it opened with is its starting point).
            if (Mpv().FileSettled()) BeginFile(false);
        }
    });
    Player& p = Mpv();
    p.confirmTracks = FileSettingsOn();
    p.fileOptions = [this](const std::wstring& path) { return FileOptionsFor(path); };
    p.beforeLeave = [this] { LeaveFile(); };
    TheApp().history().onCleared = [this] { ForgetAllFileSettings(); };
    // A full store (5000 files) takes ~9 ms to parse: read it while mpv starts, not in the first Open.
    if (FileSettingsOn())
        loader_ = std::thread([this] {
            Trace("file settings load");
            store_.Load();
            Trace("file settings loaded");
        });
}

LiveState::~LiveState() {
    if (loader_.joinable()) loader_.join();
    // At exit: the last change may still wait for its capture (the window and its timers are gone).
    if (captureTimer_) Capture(false);
    if (store_.Loaded()) store_.Flush();
}

void LiveState::LoadCropSetting() {
    const Config& c = Cfg();
    crop = c.GetString("video.crop");
    if (crop.empty()) crop = "none";
    cropT = c.GetDouble("video.cropTop");
    cropB = c.GetDouble("video.cropBottom");
    cropL = c.GetDouble("video.cropLeft");
    cropR = c.GetDouble("video.cropRight");
}

void LiveState::Start() {
    if (started_ || !Mpv().Ready()) return;
    started_ = true;
    // Player started mpv with the copy-back variant when the config has CPU filters.
    appliedHwdec_ = ConfigCpuFilters() ? Player::CopyBackHwdec(ConfigHwdec()) : ConfigHwdec();
    ObserveAll();
}

void LiveState::Notify() {
    ScheduleCapture();
    Repaint();
}

void LiveState::Repaint() {
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
    num("video-pan-x", &LiveValues::panX);
    num("video-pan-y", &LiveValues::panY);
    // The Video tab's Deinterlace follows mpv, also when the D key cycles it.
    p.Observe("deinterlace", MPV_FORMAT_STRING, [this](const mpv_event_property& e) {
        if (e.format != MPV_FORMAT_STRING || !e.data) return;
        const char* s = *static_cast<char**>(e.data);
        if (!s || deinterlace == s) return;
        deinterlace = s;
        Notify();
    });
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
    // Decoded frame size: crop rectangles and the upscaler's factor are computed from it.
    auto size = [this, &p](const char* name, int64_t LiveState::*field) {
        p.Observe(name, MPV_FORMAT_INT64, [this, field](const mpv_event_property& e) {
            const int64_t v = e.format == MPV_FORMAT_INT64 && e.data ? *static_cast<int64_t*>(e.data) : 0;
            if (this->*field == v) return;
            this->*field = v;
            ApplyUpscaler();
            ApplyCrop();
        });
    };
    size("video-params/w", &LiveState::srcW_);
    size("video-params/h", &LiveState::srcH_);
    // Size after the filters (the upscaler changes it): where video-crop's rectangle lives.
    auto outSize = [this, &p](const char* name, int64_t LiveState::*field) {
        p.Observe(name, MPV_FORMAT_INT64, [this, field](const mpv_event_property& e) {
            const int64_t v = e.format == MPV_FORMAT_INT64 && e.data ? *static_cast<int64_t*>(e.data) : 0;
            if (this->*field == v) return;
            this->*field = v;
            ApplyCrop();
        });
    };
    outSize("video-out-params/w", &LiveState::outW_);
    outSize("video-out-params/h", &LiveState::outH_);
    // The video area in pixels: the upscaler's target. Window drags send a stream of sizes, and
    // every new factor rebuilds mpv's filter chain, so wait until the size settles.
    p.Observe("osd-dimensions", MPV_FORMAT_NODE, [this](const mpv_event_property& e) {
        const mpv_node* n = e.format == MPV_FORMAT_NODE ? static_cast<const mpv_node*>(e.data) : nullptr;
        int64_t w = 0, h = 0;
        if (n && n->format == MPV_FORMAT_NODE_MAP)
            for (int i = 0; i < n->u.list->num; ++i) {
                const mpv_node& v = n->u.list->values[i];
                if (v.format != MPV_FORMAT_INT64) continue;
                if (std::strcmp(n->u.list->keys[i], "w") == 0) w = v.u.int64;
                if (std::strcmp(n->u.list->keys[i], "h") == 0) h = v.u.int64;
            }
        if (w == areaW_ && h == areaH_) return;
        const bool first = areaW_ <= 0 || areaH_ <= 0;  // the video window just came up
        areaW_ = w, areaH_ = h;
        MainWindow* win = TheApp().main();
        if (!win || !Cfg().GetBool("video.superResolution")) return;
        if (areaTimer_) win->CancelTimer(areaTimer_);
        areaTimer_ = 0;
        if (first) {
            ApplyUpscaler();
            return;
        }
        areaTimer_ = win->StartTimer(300, [this] {
            areaTimer_ = 0;
            ApplyUpscaler();
        });
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
    ApplyHwdec();
    ApplyUpscaler();
}

bool LiveState::CpuFilters() const {
    return ConfigCpuFilters() || flip != 0 || negate || grayscale;
}

// lavfi filters take frames in RAM. On zero-copy d3d11 frames mpv can't convert them, drops the
// filter ("Disabling filter ... because it has failed") and plays on unfiltered, so decoding
// copies frames back while one is on, and returns to zero-copy when the last one goes.
void LiveState::ApplyHwdec() {
    if (!started_ || !Mpv().Ready()) return;
    std::string hwdec = ConfigHwdec();
    if (CpuFilters()) hwdec = Player::CopyBackHwdec(hwdec);
    if (hwdec == appliedHwdec_) return;
    appliedHwdec_ = hwdec;
    Mpv().SetProperty("hwdec", hwdec);
}

// AI upscaling: mpv's d3d11vpp filter with the vendor's Video Super Resolution, scaling the
// decoded frames on the GPU to about the size they are shown at (the video area, the whole
// screen in fullscreen), so the renderer only fine-tunes. It
// takes D3D11 frames only, so it waits for zero-copy d3d11va decoding and steps aside while a
// CPU filter is on (frames are in RAM then). RTX Video HDR rides on the same filter.
void LiveState::ApplyUpscaler() {
    if (!Mpv().Ready()) return;
    const PlayerState& s = Mpv().State();
    const Config& c = Cfg();
    const bool sr = c.GetBool("video.superResolution"), trueHdr = c.GetBool("video.trueHdr");
    std::string want;
    if ((sr || trueHdr) && s.fileLoaded && s.hasVideo && s.hwdec == L"d3d11va" && srcW_ > 0 && srcH_ > 0 &&
        !CpuFilters()) {
        const std::string vendor = SuperResVendor();
        std::string args;
        const HMONITOR mon = VideoMonitor();
        // Until mpv reports the video area (its window comes up with the first frame) the
        // target is unknown; the osd-dimensions observer applies the filter then.
        const double aw = static_cast<double>(areaW_), ah = static_cast<double>(areaH_);
        if (sr && !vendor.empty() && aw > 0 && ah > 0) {
            // The height the picture gets when fitted into the area at its display aspect.
            const double sw = static_cast<double>(srcW_), sh = static_cast<double>(srcH_);
            const double dar = s.videoW > 0 && s.videoH > 0 ? static_cast<double>(s.videoW) / static_cast<double>(s.videoH)
                                                            : sw / sh;
            const double shownH = std::min(ah, aw / dar);
            // In 0.05 steps, rounded up: small resizes keep the filter, and the picture is never
            // enlarged again after the AI pass, only scaled down a little.
            const double f = std::min(4.0, std::ceil(shownH / sh * 20 - 1e-6) / 20);
            // Below ~1.2x the renderer's own scaler is as good and the extra pass isn't worth it.
            if (f >= 1.2) {
                args = "scale=" + Num(f) + ":scaling-mode=" + vendor;
            }
        }
        // On an SDR screen the HDR output would only be tone-mapped back: a wasted GPU pass.
        if (trueHdr && vendor == "nvidia" && !s.hdr && mon && DisplayIsHdr(mon))
            args += std::string(args.empty() ? "" : ":") + "nvidia-true-hdr";
        if (!args.empty()) want = "@lsr:d3d11vpp=" + args;
    }
    if (want == srFilter_) return;
    if (!srFilter_.empty()) Mpv().Command({"vf", "remove", "@lsr"});
    if (!want.empty()) Mpv().Command({"vf", "add", want});
    srFilter_ = want;
}

std::wstring LiveState::UpscalerStatus() const {
    const Config& c = Cfg();
    const bool sr = c.GetBool("video.superResolution"), trueHdr = c.GetBool("video.trueHdr");
    if ((!sr && !trueHdr) || !srFilter_.empty()) return {};
    const PlayerState& s = Mpv().State();
    if (!s.fileLoaded || !s.hasVideo) return {};
    const std::string vendor = SuperResVendor();
    if (vendor.empty() || (!sr && vendor != "nvidia")) return Tr(L"Not supported by this graphics card");
    if (s.hwdec != L"d3d11va") return Tr(L"Needs Direct3D 11 hardware decoding");
    if (CpuFilters()) return Tr(L"Off while CPU filters are on");
    if (!sr && s.hdr) return Tr(L"The video is already HDR");
    if (!sr && !DisplayIsHdr(VideoMonitor())) return Tr(L"Needs HDR turned on in Windows");
    return Tr(L"The video already matches the screen");
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
    Mpv().SetProperty("video-aspect-override", a == "auto" ? std::string("no") : a);
    Notify();
}

void LiveState::SetCrop(const std::string& c) {
    crop = c;
    ApplyCrop();
    Notify();
}

void LiveState::ApplyCrop() {
    if (!Mpv().Ready()) return;
    // Source pixels; ratios are of the stored frame (anamorphic video crops its stored shape).
    const double w = static_cast<double>(srcW_), h = static_cast<double>(srcH_);
    double cw = 0, ch = 0, x = 0, y = 0;  // cw == 0: no crop
    if (w > 0 && h > 0) {
        if (crop == "custom") {
            if ((cropL + cropR + cropT + cropB) > 0 && w - cropL - cropR > 16 && h - cropT - cropB > 16) {
                cw = w - cropL - cropR, ch = h - cropT - cropB;
                x = cropL, y = cropT;
            }
        } else if (crop != "none") {
            const double r = Ratio(crop);
            if (r > 0) {
                cw = w, ch = std::round(w / r);
                if (ch > h) {
                    ch = h;
                    cw = std::round(h * r);
                }
                x = std::round((w - cw) / 2), y = std::round((h - ch) / 2);
            }
        }
    }
    std::string value = "0x0+0+0";
    if (cw > 0) {
        // video-crop applies to the filter chain's output; the upscaler's d3d11vpp enlarges the
        // frame on the way, so map the rectangle onto the size that actually reaches the renderer.
        // mpv drops a rectangle that doesn't fit the current frame, and the out-params observer
        // brings it back once the new size arrives.
        const double ow = outW_ > 0 ? static_cast<double>(outW_) : w, oh = outH_ > 0 ? static_cast<double>(outH_) : h;
        const double sx = ow / w, sy = oh / h;
        x = std::round(x * sx), y = std::round(y * sy);
        cw = std::min(std::round(cw * sx), ow - x), ch = std::min(std::round(ch * sy), oh - y);
        value = Num(cw) + "x" + Num(ch) + "+" + Num(x) + "+" + Num(y);
    }
    if (value == appliedCrop_) return;
    appliedCrop_ = value;
    Mpv().SetProperty("video-crop", value);
}

void LiveState::SetDeinterlace(const std::string& d) {
    deinterlace = d;
    Mpv().SetProperty("deinterlace", d);
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
    TheApp().SyncNextInPlayer();  // no gapless move to the next item while this is on
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
    for (const char* name : {"audio-delay", "sub-delay", "brightness", "contrast", "saturation", "gamma", "hue"})
        p.SetProperty(name, v->GetNumber(name, 0));
    // A choice option: mpv rejects a double.
    p.SetProperty("video-rotate", std::to_string(((v->GetInt("video-rotate", 0) % 360) + 360) % 360));
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
    for (const char* name : {"audio-delay", "sub-delay"}) p.SetProperty(name, 0.0);
    p.SetProperty("video-rotate", std::string("0"));  // a choice option: mpv rejects a double
    p.SetProperty("sub-scale", 1.0);
    p.SetProperty("sub-pos", 100.0);
    ResetPicture();
    SetZoomMode("fit");
    SetAspect("auto");
    LoadCropSetting();
    SetCrop(crop);
    flip = 0;
    ApplyVideoFilters();
    SetStereoMode("stereo");
    activePreset = -1;
    Notify();
}

// --- per-file settings ------------------------------------------------------------------------

bool LiveState::FileSettingsOn() const {
    return Cfg().GetBool("playback.perFile");
}

bool LiveState::FileSettingsAll() const {
    return FileSettingsOn() && Cfg().GetBool("playback.perFileAll");
}

library::FileSettings& LiveState::Store() {
    if (loader_.joinable()) loader_.join();
    if (!store_.Loaded()) store_.Load();
    return store_;
}

library::FileOptions LiveState::FileSettingsStored() const {
    if (!fileOpen_) return {};
    if (FileSettingsAll()) return fileOpts_;
    library::FileOptions o;  // the rest waits for playback.perFileAll
    o.aid = fileOpts_.aid;
    o.sid = fileOpts_.sid;
    o.secondarySid = fileOpts_.secondarySid;
    o.audioFile = fileOpts_.audioFile;
    o.subFile = fileOpts_.subFile;
    o.audioDelay = fileOpts_.audioDelay;
    o.subDelay = fileOpts_.subDelay;
    return o;
}

// Player::Open / QueueNext. With playback.perFileAll every file also gets the speed, deinterlacing
// and subtitle position and size it should start with, stored or default: mpv keeps those from
// file to file otherwise, and one file's values would leak into the next.
std::string LiveState::FileOptionsFor(const std::wstring& path) {
    if (!FileSettingsOn()) return {};
    library::FileOptions o;
    if (!ExcludedFromMemory(path))
        if (const library::FileOptions* f = Store().Find(path)) o = *f;
    std::string out;
    auto add = [&out](const char* name, const std::string& value) {
        if (!out.empty()) out += ',';
        out += name;
        out += '=';
        out += value;
    };
    auto track = [](int64_t id) { return id > 0 ? std::to_string(id) : std::string("no"); };
    // The tracks always: a file without a stored choice gets the configured one, not the choice
    // mpv's watch-later put back for the previous file (it outlives that file on a gapless move).
    // A sidecar track is picked (or added) once the file has loaded, see FixTracks.
    add("aid", o.aid && !o.audioFile ? track(*o.aid) : ConfigOption("playback.audioTrack", "aid", "auto"));
    add("sid", o.sid && !o.subFile ? track(*o.sid) : ConfigOption("playback.subtitleTrack", "sid", "auto"));
    add("secondary-sid", o.secondarySid ? track(*o.secondarySid) : std::string("no"));
    if (o.audioDelay) add("audio-delay", Num(*o.audioDelay));
    if (o.subDelay) add("sub-delay", Num(*o.subDelay));
    if (!FileSettingsAll()) return out;
    add("speed", Num(o.speed.value_or(DefaultSpeed())));
    if (o.aspect) add("video-aspect-override", *o.aspect);
    if (o.zoomMode) {
        add("video-unscaled", "yes");
        add("video-zoom", Num(ModeZoom(*o.zoomMode)));
    } else if (o.zoom) {
        add("video-zoom", Num(*o.zoom));
    }
    if (o.panX) add("video-pan-x", Num(*o.panX));
    if (o.panY) add("video-pan-y", Num(*o.panY));
    if (o.rotate) add("video-rotate", Num(*o.rotate));
    add("deinterlace", o.deinterlace.value_or(DefaultDeinterlace()));
    if (o.abA) add("ab-loop-a", Num(*o.abA));
    if (o.abB) add("ab-loop-b", Num(*o.abB));
    add("sub-pos", Num(o.subPos.value_or(DefaultSubPos())));
    add("sub-scale", Num(o.subScale.value_or(1.0)));
    return out;
}

void LiveState::BeginFile(bool loaded) {
    Player& p = Mpv();
    const PlayerState& s = p.State();
    MainWindow* w = TheApp().main();
    if (captureTimer_ && w) w->CancelTimer(captureTimer_);
    captureTimer_ = 0;
    if (loaded) {
        fileSerial_ = s.loadSerial;
        filePath_ = p.LoadedPath();
    }
    const bool on = FileSettingsOn() && !filePath_.empty() && !ExcludedFromMemory(filePath_);
    const bool all = on && FileSettingsAll();
    fileOpen_ = on;
    fileOpts_ = {};
    if (on)
        if (const library::FileOptions* f = Store().Find(filePath_)) {
            fileOpts_ = *f;
            if (loaded) Store().Touch(filePath_);  // least recently used goes first; saved with the next write
        }
    for (int k = 0; k < 3; ++k) trackBase_[k] = p.TrackChoices(k);
    tracksTouched_[0] = fileOpts_.aid || fileOpts_.audioFile;
    tracksTouched_[1] = fileOpts_.sid || fileOpts_.subFile;
    tracksTouched_[2] = fileOpts_.secondarySid.has_value();
    tracksTrusted_ = !loaded;
    trackFixPending_ = loaded && on && fileOpts_.HasTracks();
    lastSpeed_ = s.speed;
    if (loaded) {
        // The transforms are per file (reset-on-next-file in Player.cpp; flip is our filter, which
        // mpv keeps): back to the defaults, or to what this file had, which mpv got with loadfile.
        zoomMode = all && fileOpts_.zoomMode ? *fileOpts_.zoomMode : "fit";
        aspect = all && fileOpts_.aspect ? *fileOpts_.aspect : "auto";
        const int wantFlip = all && fileOpts_.flip ? *fileOpts_.flip : 0;
        if (flip != wantFlip) {
            flip = wantFlip;
            ApplyVideoFilters();
        }
        if (trackFixPending_) FixTracks();
    }
    Repaint();
}

// The stored track ids must exist in this file (it may have been remuxed), and sidecar tracks are
// found by their file, or added again when mpv did not load them by itself.
void LiveState::FixTracks() {
    Player& p = Mpv();
    if (!p.TracksConfirmed() && !tracksTrusted_) return;
    trackFixPending_ = false;
    const std::vector<Track>& tracks = p.State().tracks;
    auto byId = [&tracks](const wchar_t* type, int64_t id) {
        return std::any_of(tracks.begin(), tracks.end(), [&](const Track& t) { return t.type == type && t.id == id; });
    };
    auto byFile = [&tracks](const wchar_t* type, const std::wstring& file) -> const Track* {
        const std::wstring key = library::PathKey(file);
        for (const Track& t : tracks)
            if (t.type == type && t.external && !t.file.empty() && library::PathKey(t.file) == key) return &t;
        return nullptr;
    };
    const library::FileOptions o = fileOpts_;
    if (o.audioFile) {
        if (const Track* t = byFile(L"audio", *o.audioFile)) {
            if (!t->selected) p.SetTrack("aid", t->id);
        } else {
            p.AddAudio(*o.audioFile);  // a file that is gone only fails inside mpv
        }
    } else if (o.aid && *o.aid > 0 && !byId(L"audio", *o.aid)) {
        p.SetProperty("aid", std::string("auto"));
    }
    if (o.subFile) {
        if (const Track* t = byFile(L"sub", *o.subFile)) {
            if (!t->selected) p.SetTrack("sid", t->id);
        } else {
            p.AddSubtitle(*o.subFile);
        }
    } else if (o.sid && *o.sid > 0 && !byId(L"sub", *o.sid)) {
        p.SetProperty("sid", std::string("auto"));
    }
    if (o.secondarySid && *o.secondarySid > 0 && !byId(L"sub", *o.secondarySid))
        p.SetProperty("secondary-sid", std::string("no"));
    // Our own picks are not the user's.
    for (int k = 0; k < 3; ++k) trackBase_[k] = p.TrackChoices(k);
}

void LiveState::ScheduleCapture() {
    if (!fileOpen_ || captureTimer_) return;
    MainWindow* w = TheApp().main();
    if (!w) return;
    // A moment later, so a slider drag is stored once, and values mpv puts back when it moves on
    // to the next file by itself (gapless) are never taken for this file's.
    captureTimer_ = w->StartTimer(300, [this] {
        captureTimer_ = 0;
        Capture();
    });
}

void LiveState::Capture(bool notify) {
    Player& p = Mpv();
    const PlayerState& s = p.State();
    if (!fileOpen_ || !FileSettingsOn() || !p.FileSettled() || s.loadSerial != fileSerial_) return;
    auto round3 = [](double v) { return std::round(v * 1000) / 1000; };
    auto num = [&round3](std::optional<double>& field, double v, double def, double eps) {
        if (std::isfinite(v) && std::abs(v - def) > eps) field = round3(v);
        else field.reset();
    };
    library::FileOptions o = fileOpts_;  // keeps what the current settings don't cover
    if ((p.TracksConfirmed() || tracksTrusted_) && !trackFixPending_) {
        for (int k = 0; k < 3; ++k)
            if (p.TrackChoices(k) != trackBase_[k]) tracksTouched_[k] = true;
        // The selected track of a kind: by id, a sidecar by its file, 0 when that kind is off.
        auto track = [&s](const wchar_t* type, int64_t skip, std::optional<int64_t>* id, std::optional<std::wstring>* file) {
            const Track* sel = nullptr;
            bool any = false;
            for (const Track& t : s.tracks) {
                if (t.type != type) continue;
                any = true;
                if (t.selected && t.id != skip && !sel) sel = &t;
            }
            if (!any) return;  // nothing to choose from (yet): keep what is stored
            id->reset();
            file->reset();
            if (!sel) *id = 0;
            else if (sel->external && !sel->file.empty()) *file = sel->file;
            else *id = sel->id;
        };
        if (tracksTouched_[0]) track(L"audio", 0, &o.aid, &o.audioFile);
        if (tracksTouched_[1]) track(L"sub", v_.secondarySid, &o.sid, &o.subFile);
        if (tracksTouched_[2]) {
            o.secondarySid.reset();
            for (const Track& t : s.tracks)
                if (t.type == L"sub" && t.id == v_.secondarySid && !t.external) o.secondarySid = t.id;
        }
    }
    num(o.audioDelay, v_.audioDelay, 0, 0.0005);
    num(o.subDelay, v_.subDelay, 0, 0.0005);
    if (FileSettingsAll()) {
        num(o.speed, s.speed, DefaultSpeed(), 0.001);
        o.aspect.reset();
        if (aspect != "auto") o.aspect = aspect;
        o.zoomMode.reset();
        o.zoom.reset();
        if (zoomMode != "fit") o.zoomMode = zoomMode;
        else num(o.zoom, v_.zoom, 0, 0.001);
        num(o.panX, v_.panX, 0, 0.001);
        num(o.panY, v_.panY, 0, 0.001);
        num(o.rotate, v_.rotate, 0, 0.5);
        o.flip.reset();
        if (flip != 0) o.flip = flip;
        o.deinterlace.reset();
        if (!deinterlace.empty() && deinterlace != DefaultDeinterlace()) o.deinterlace = deinterlace;
        o.abA.reset();
        o.abB.reset();
        if (!std::isnan(v_.abA)) o.abA = round3(v_.abA);
        if (!std::isnan(v_.abB)) o.abB = round3(v_.abB);
        num(o.subPos, v_.subPos, DefaultSubPos(), 0.5);
        num(o.subScale, v_.subScale, 1, 0.001);
    }
    if (o == fileOpts_) return;
    fileOpts_ = o;
    Store().Put(filePath_, o);
    ScheduleSave();
    if (notify) Repaint();
}

void LiveState::LeaveFile() {
    if (captureTimer_) {
        if (MainWindow* w = TheApp().main()) w->CancelTimer(captureTimer_);
        captureTimer_ = 0;
        Capture(false);  // the user's last change, made a moment ago
    }
    fileOpen_ = false;
}

void LiveState::ScheduleSave() {
    MainWindow* w = TheApp().main();
    if (saveTimer_ || !w) return;
    saveTimer_ = w->StartTimer(2000, [this] {
        saveTimer_ = 0;
        store_.Flush();
    });
}

void LiveState::ResetFileSettings() {
    if (filePath_.empty()) return;
    if (captureTimer_) {
        if (MainWindow* w = TheApp().main()) w->CancelTimer(captureTimer_);
        captureTimer_ = 0;
        Capture(false);
    }
    const library::FileOptions old = FileSettingsStored();
    if (Store().Remove(filePath_)) ScheduleSave();
    fileOpts_ = {};
    // Back to what the file opens with when nothing is stored.
    Player& p = Mpv();
    if (old.aid || old.audioFile) p.SetProperty("aid", std::string("auto"));
    if (old.sid || old.subFile) p.SetProperty("sid", std::string("auto"));
    if (old.secondarySid) p.SetProperty("secondary-sid", std::string("no"));
    if (old.audioDelay) p.SetProperty("audio-delay", 0.0);
    if (old.subDelay) p.SetProperty("sub-delay", 0.0);
    if (old.speed) p.SetSpeed(DefaultSpeed());
    if (old.aspect) SetAspect("auto");
    if (old.zoomMode || old.zoom) SetZoomMode("fit");
    if (old.panX) p.SetProperty("video-pan-x", 0.0);
    if (old.panY) p.SetProperty("video-pan-y", 0.0);
    if (old.rotate) p.SetProperty("video-rotate", std::string("0"));
    if (old.flip) SetFlip(0);
    if (old.deinterlace) p.SetProperty("deinterlace", DefaultDeinterlace());
    if (old.abA || old.abB) {
        p.SetProperty("ab-loop-a", std::string("no"));
        p.SetProperty("ab-loop-b", std::string("no"));
    }
    if (old.subPos) p.SetProperty("sub-pos", DefaultSubPos());
    if (old.subScale) p.SetProperty("sub-scale", 1.0);
    for (int k = 0; k < 3; ++k) {
        trackBase_[k] = p.TrackChoices(k);
        tracksTouched_[k] = false;
    }
    trackFixPending_ = false;
    Repaint();
}

// History > Clear (and Settings > Privacy > Clear history): per-file settings are history too.
// The playing file is not remembered again until it is opened anew.
void LiveState::ForgetAllFileSettings() {
    if (captureTimer_) {
        if (MainWindow* w = TheApp().main()) w->CancelTimer(captureTimer_);
        captureTimer_ = 0;
    }
    Store().Clear();
    store_.Flush();
    fileOpts_ = {};
    fileOpen_ = false;
    trackFixPending_ = false;
    Repaint();
}

}  // namespace app
