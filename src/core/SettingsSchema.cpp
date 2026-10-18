#include "SettingsSchema.h"

#include "Config.h"
#include "Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <thread>
#include <tuple>
#include <unordered_map>

// Source of truth: design/ref/Settings-*.png (19 frames) and design/ref/components/*.png.
// English labels follow the design word for word, except for obvious typos and VLC branding:
//   "Only allow one VLC window"   -> "Only allow one Lumen window"
//   "Give VLC real-time priority" -> "Give Lumen real-time priority"
//   second "Last.fm username"     -> "Last.fm password" (copy-paste slip in the frame)
//   "include subtitles"           -> "Include subtitles"
//   "Full base"                   -> "Full bass" (EQ presets), "1 KHz" -> "1 kHz"
//   "FM5 / Peak"                  -> "RMS / Peak" (compressor)
//   accent color: 9 hues          -> 5 grey tones (monochrome redesign, DESIGN.md §1)
// Rows marked Inferred() sit below the visible part of a frame (or behind a collapsed
// "Advanced Options") and were chosen to complete the section sensibly.

namespace settings {

namespace {

using W = std::wstring;

// --- mpv value builders -------------------------------------------------------------------

std::string Num(double d) {
    return json::FormatNumber(d);
}

double DbToLinear(double db) {
    return std::pow(10.0, db / 20.0);
}

struct LangInfo {
    const char* id;
    const wchar_t* en;
    const wchar_t* ru;
    const char* mpv;  // alang / slang list
};

const LangInfo kLanguages[] = {
    {"ru", L"Russian", L"Русский", "ru,rus"},
    {"en", L"English", L"Английский", "en,eng"},
    {"uk", L"Ukrainian", L"Украинский", "uk,ukr"},
    {"kk", L"Kazakh", L"Казахский", "kk,kaz"},
    {"de", L"German", L"Немецкий", "de,ger,deu"},
    {"fr", L"French", L"Французский", "fr,fre,fra"},
    {"es", L"Spanish", L"Испанский", "es,spa"},
    {"it", L"Italian", L"Итальянский", "it,ita"},
    {"pt", L"Portuguese", L"Португальский", "pt,por"},
    {"pl", L"Polish", L"Польский", "pl,pol"},
    {"tr", L"Turkish", L"Турецкий", "tr,tur"},
    {"ar", L"Arabic", L"Арабский", "ar,ara"},
    {"id", L"Indonesian", L"Индонезийский", "id,ind"},
    {"ja", L"Japanese", L"Японский", "ja,jpn"},
    {"zh", L"Chinese", L"Китайский", "zh,chi,zho"},
    {"ko", L"Korean", L"Корейский", "ko,kor"},
};

const char* LangMpv(std::string_view id) {
    for (const LangInfo& l : kLanguages)
        if (id == l.id) return l.mpv;
    return nullptr;
}

// "system" -> the Windows UI language; "original" -> nothing (mpv picks the default track).
std::optional<std::string> LanguageList(const std::string& id) {
    if (id == "original" || id.empty()) return std::nullopt;
    if (id != "system") {
        const char* mpv = LangMpv(id);
        return std::string(mpv ? mpv : id);
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0)
        return std::nullopt;
    std::string code = json::ToUtf8(name);
    code = code.substr(0, code.find('-'));
    if (code.empty()) return std::nullopt;
    const char* mpv = LangMpv(code);
    return std::string(mpv ? mpv : code.c_str());
}

std::optional<std::string> BuildAlang(const Config& c) {
    return LanguageList(c.GetString("audio.language"));
}

std::optional<std::string> BuildSlang(const Config& c) {
    return LanguageList(c.GetString("subtitles.language"));
}

std::optional<std::string> BuildVid(const Config& c) {
    if (!c.GetBool("video.enabled")) return "no";
    return std::nullopt;
}

std::optional<std::string> BuildAid(const Config& c) {
    if (!c.GetBool("audio.enabled")) return "no";
    int track = c.GetInt("playback.audioTrack");
    if (track > 0) return std::to_string(track);
    return std::nullopt;
}

std::optional<std::string> BuildSid(const Config& c) {
    if (!c.GetBool("subtitles.enabled")) return "no";
    int track = c.GetInt("playback.subtitleTrack");
    if (track > 0) return std::to_string(track);
    return std::nullopt;
}

std::optional<std::string> BuildLoopFile(const Config& c) {
    return c.GetString("playback.repeat") == "one" ? "inf" : "no";
}

std::optional<std::string> BuildLoopPlaylist(const Config& c) {
    return c.GetString("playback.repeat") == "all" ? "inf" : "no";
}

std::optional<std::string> BuildLogFile(const Config& c) {
    if (!c.GetBool("general.saveLog")) return std::nullopt;
    std::string file = c.GetString("general.logFile");
    if (file.empty()) {
        std::wstring def = DataFilePath(L"lumen.log");
        if (def.empty()) return std::nullopt;
        file = json::ToUtf8(def);
    }
    return file;
}

// 0 = automatic. FFmpeg's own choice (cores + 1, up to 16) keeps a few full frames per thread:
// about 1 GB for 4K software decoding on a 32-thread CPU. Six threads still decode 4K H.264
// several times faster than real time.
std::optional<std::string> BuildDecodeThreads(const Config& c) {
    const double n = c.GetDouble("performance.decodeThreads");
    if (n >= 1) return Num(n);
    return std::to_string(AutoDecodeThreads());
}

std::optional<std::string> BuildStartupVolume(const Config& c) {
    double v = c.GetString("audio.startupVolumeMode") == "fixed" ? c.GetDouble("audio.startupVolume")
                                                                   : c.GetDouble("audio.lastVolume");
    return Num(std::clamp(v, 0.0, c.GetDouble("audio.maxVolume")));
}

std::optional<std::string> BuildPriority(const Config& c) {
    if (c.GetBool("performance.realtimePriority")) return "realtime";
    switch (c.GetInt("performance.processPriority")) {
        case -2: return "idle";
        case -1: return "belownormal";
        case 1: return "abovenormal";
        case 2: return "high";
        default: return std::nullopt;
    }
}

// "#RRGGBB" (validated) + opacity in percent -> mpv's "#AARRGGBB".
std::string ArgbColor(std::string rgb, double opacity, const char* fallback) {
    if (rgb.size() != 7) rgb = fallback;
    int alpha = static_cast<int>(std::lround(std::clamp(opacity, 0.0, 100.0) * 255.0 / 100.0));
    static const char kHex[] = "0123456789ABCDEF";
    std::string out = "#";
    out += kHex[alpha >> 4];
    out += kHex[alpha & 15];
    out += rgb.substr(1);
    return out;
}

std::optional<std::string> BuildSubColor(const Config& c) {
    return ArgbColor(c.GetString("subtitles.color"), c.GetDouble("subtitles.opacity"), "#FFFFFF");
}

// Box color for sub-border-style=background-box (and the shadow color in mpv's other styles).
std::optional<std::string> BuildSubBackColor(const Config& c) {
    return ArgbColor(c.GetString("subtitles.backColor"), c.GetDouble("subtitles.backOpacity"), "#000000");
}

std::optional<std::string> BuildSpdif(const Config& c) {
    if (!c.GetBool("audio.passthrough")) return std::nullopt;
    std::string codecs;
    for (auto [key, codec] : {std::pair{"audio.spdifAc3", "ac3"}, std::pair{"audio.spdifEac3", "eac3"},
                              std::pair{"audio.spdifDts", "dts"}, std::pair{"audio.spdifDtsHd", "dts-hd"},
                              std::pair{"audio.spdifTrueHd", "truehd"}})
        if (c.GetBool(key)) codecs += (codecs.empty() ? "" : ",") + std::string(codec);
    if (codecs.empty()) return std::nullopt;
    return codecs;
}

std::optional<std::string> BuildSubAuto(const Config& c) {
    if (!c.GetBool("subtitles.autoLoad")) return "no";
    std::string m = c.GetString("subtitles.matching");
    return m.empty() ? std::string("fuzzy") : m;
}

std::optional<std::string> BuildScreenshotDir(const Config& c) {
    std::string dir = c.GetString("video.screenshotDir");
    if (!dir.empty()) return dir;
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &base))) return std::nullopt;
    std::wstring path = base;
    CoTaskMemFree(base);
    return json::ToUtf8(path + L"\\Lumen");
}

// Buffering presets: {cache-secs, demuxer-max-bytes in MiB}.
std::optional<std::string> BuildCacheSecs(const Config& c) {
    std::string mode = c.GetString("network.buffering");
    if (mode == "low") return "10";
    if (mode == "high") return "300";
    if (mode == "custom") return Num(std::max(1.0, c.GetDouble("network.cacheNetwork") / 1000.0));
    return "60";
}

std::optional<std::string> BuildDemuxerMaxBytes(const Config& c) {
    std::string mode = c.GetString("network.buffering");
    double mib = 150;
    if (mode == "low") mib = 50;
    if (mode == "high") mib = 500;
    if (mode == "custom") mib = c.GetDouble("network.cacheSize");
    return Num(mib) + "MiB";
}

std::optional<std::string> BuildHttpProxy(const Config& c) {
    std::string proxy = c.GetString("network.httpProxy");
    if (proxy.empty()) return std::nullopt;
    std::string password = c.GetString("network.httpProxyPassword");
    if (password.empty()) return proxy;
    // "http://user@host:port" + password -> "http://user:password@host:port"
    size_t scheme = proxy.find("://");
    size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
    size_t at = proxy.find('@', hostStart);
    if (at == std::string::npos) return proxy;  // no user name to attach the password to
    std::string user = proxy.substr(hostStart, at - hostStart);
    if (user.find(':') != std::string::npos) return proxy;
    return proxy.substr(0, hostStart) + user + ":" + password + proxy.substr(at);
}

// Read-ahead of files mpv does not cache (local files and discs); streams use cache-secs.
std::optional<std::string> BuildReadahead(const Config& c) {
    if (c.GetString("network.buffering") != "custom") return std::nullopt;
    return Num(std::max(0.05, c.GetDouble("network.cacheLocal") / 1000.0));
}

std::optional<std::string> BuildYtdl(const Config& c) {
    return c.GetBool("network.ytdl") && !YtDlpPath().empty() ? "yes" : "no";
}

// mpv keeps one script-opts list: the yt-dlp Lumen found, then the user's own options (so they
// win). "%n%" quotes the path, which may contain commas.
std::optional<std::string> BuildScriptOpts(const Config& c) {
    std::string out;
    if (c.GetBool("network.ytdl") && !YtDlpPath().empty()) {
        const std::string path = json::ToUtf8(YtDlpPath());
        out = "ytdl_hook-ytdl_path=%" + std::to_string(path.size()) + "%" + path;
    }
    const std::string user = c.GetString("extensions.scriptOpts");
    if (!user.empty()) out += (out.empty() ? "" : ",") + user;
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<std::string> BuildDeinterlace(const Config& c) {
    std::string mode = c.GetString("video.deinterlace");
    if (mode == "off") return "no";
    // A specific method is inserted as a video filter (see BuildVf); mpv's own switch stays off.
    if (c.GetString("video.deinterlaceMethod") != "auto") return "no";
    return mode == "on" ? "yes" : "auto";
}

std::optional<std::string> BuildDeband(const Config& c) {
    return c.GetBool("video.deband") ? "yes" : "no";
}

std::optional<std::string> BuildDebandRange(const Config& c) {
    return c.GetBool("video.deband") ? std::optional<std::string>(Num(c.GetDouble("video.debandRange"))) : std::nullopt;
}

// --- image quality (video.quality presets, upscaler, tone mapping) ---
// "balanced" keeps mpv's defaults; "fast" and "high" follow mpv's [fast] and [high-quality]
// profiles, written as single options so that switching back restores each one (the `reset`
// values below are mpv 0.41's defaults).

bool QualityIs(const Config& c, const char* preset) {
    return c.GetString("video.quality") == preset;
}

std::optional<std::string> BuildScale(const Config& c) {
    const std::string up = c.GetString("video.upscaler");
    if (!up.empty() && up != "auto") return up;
    if (QualityIs(c, "fast")) return "bilinear";
    if (QualityIs(c, "high")) return "ewa_lanczossharp";
    return std::nullopt;
}

std::optional<std::string> BuildFastScaler(const Config& c) {  // dscale, cscale
    return QualityIs(c, "fast") ? std::optional<std::string>("bilinear") : std::nullopt;
}

std::optional<std::string> BuildFastOff(const Config& c) {  // dither, *-downscaling, sigmoid-upscaling
    return QualityIs(c, "fast") ? std::optional<std::string>("no") : std::nullopt;
}

std::optional<std::string> BuildAntiring(const Config& c) {
    return QualityIs(c, "high") ? std::optional<std::string>("0.6") : std::nullopt;
}

std::optional<std::string> BuildPeakPercentile(const Config& c) {
    return QualityIs(c, "high") ? std::optional<std::string>("99.995") : std::nullopt;
}

std::optional<std::string> BuildContrastRecovery(const Config& c) {
    return QualityIs(c, "high") ? std::optional<std::string>("0.3") : std::nullopt;
}

std::optional<std::string> BuildComputePeak(const Config& c) {
    const std::string v = c.GetString("video.hdrComputePeak");
    if (v == "yes" || v == "no") return v;
    return QualityIs(c, "fast") ? std::optional<std::string>("no") : std::nullopt;
}

std::optional<std::string> BuildTargetPeak(const Config& c) {
    const double nits = c.GetDouble("video.targetPeak");
    return nits > 0 ? std::optional<std::string>(Num(nits)) : std::nullopt;
}

// --- motion ---

std::optional<std::string> BuildVideoSync(const Config& c) {
    if (c.GetBool("video.smoothMotion")) return "display-resample";
    const std::string s = c.GetString("performance.clockSync");
    if (s == "resample") return "display-resample";
    if (s == "vdrop") return "display-vdrop";
    return std::nullopt;
}

std::optional<std::string> BuildTscale(const Config& c) {
    if (!c.GetBool("video.smoothMotion")) return std::nullopt;
    const std::string blend = c.GetString("video.motionBlend");
    return blend.empty() ? std::nullopt : std::optional<std::string>(blend);
}

// --- GPU shaders: built-in sharpening and the user's own (<data>\shaders) ---

constexpr const char* kCasParam = "lumen_cas_sharpness";

// vo=gpu (legacy) has no shader parameters; there sharpening stays a CPU filter (BuildVf).
bool GpuSharpen(const Config& c) {
    return c.GetDouble("video.sharpen") > 0 && c.GetString("performance.vo") != "gpu";
}

// "a.glsl|b.hook" -> enabled file names in <data>\shaders (plain names only, no paths).
std::vector<std::string> EnabledShaders(const Config& c) {
    std::vector<std::string> out;
    const std::string list = c.GetString("video.shaders");
    size_t start = 0;
    while (start <= list.size()) {
        size_t bar = list.find('|', start);
        if (bar == std::string::npos) bar = list.size();
        std::string name = list.substr(start, bar - start);
        if (!name.empty() && name.find_first_of("\\/:;") == std::string::npos && name != "." && name != "..")
            out.push_back(std::move(name));
        start = bar + 1;
    }
    return out;
}

std::optional<std::string> BuildGlslShaders(const Config& c) {
    std::string list;
    auto add = [&list](const std::wstring& path) { list += (list.empty() ? "" : ";") + json::ToUtf8(path); };
    if (GpuSharpen(c)) add(DataDir() + L"\\cache\\shaders\\lumen-cas.glsl");
    const std::wstring folder = DataDir() + L"\\shaders\\";
    for (const std::string& name : EnabledShaders(c)) {
        const std::wstring path = folder + json::ToWide(name);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) add(path);  // skip deleted files
    }
    return list.empty() ? std::nullopt : std::optional<std::string>(list);
}

std::optional<std::string> BuildShaderOpts(const Config& c) {
    if (!GpuSharpen(c)) return std::nullopt;
    return std::string(kCasParam) + "=" + Num(std::min(1.0, c.GetDouble("video.sharpen") / 10.0));
}

std::optional<std::string> BuildHdrHint(const Config& c) {
    return c.GetString("video.hdr") == "off" ? "no" : "yes";
}

std::optional<std::string> BuildHdrTrc(const Config& c) {
    std::string m = c.GetString("video.hdr");
    if (m == "hdr10" || m == "hdr10plus" || m == "dolbyvision") return "pq";
    if (m == "hlg") return "hlg";
    return std::nullopt;
}

std::optional<std::string> BuildHdrPrim(const Config& c) {
    std::string m = c.GetString("video.hdr");
    if (m == "auto" || m == "off" || m.empty()) return std::nullopt;
    return "bt.2020";
}

// One lavfi graph for the CPU video filters the user explicitly picked: a deinterlacing method,
// deblocking, and sharpening on the legacy renderer. Any of them makes hardware decoding copy
// frames back to RAM, so everything that can run on the GPU stays out of here: crop is mpv's
// video-crop (app/player/LiveState), sharpening a GLSL shader, deinterlacing mpv's own switch.
std::optional<std::string> BuildVf(const Config& c) {
    std::vector<std::string> f;
    std::string method = c.GetString("video.deinterlaceMethod");
    if (c.GetString("video.deinterlace") != "off" && method != "auto") {
        std::string deint = c.GetString("video.deinterlace") == "on" ? "all" : "interlaced";
        f.push_back(method + "=deint=" + deint);
    }
    std::string pp = c.GetString("performance.postprocessing");
    if (pp == "low") f.push_back("deblock=filter=weak:block=4");
    if (pp == "medium") f.push_back("deblock=filter=weak:block=8");
    if (pp == "high") f.push_back("deblock=filter=strong:block=8");

    double sharpen = c.GetDouble("video.sharpen");
    if (sharpen > 0 && !GpuSharpen(c)) f.push_back("cas=strength=" + Num(std::min(1.0, sharpen / 10.0)));

    if (f.empty()) return std::nullopt;
    std::string graph;
    for (const std::string& s : f) graph += (graph.empty() ? "" : ",") + s;
    return "@lumen:lavfi=[" + graph + "]";
}

// One lavfi graph for every audio filter Lumen manages. Order: equalizer, compressor
// (night mode), loudness normalization, stereo widening, spatial audio.
std::optional<std::string> BuildAf(const Config& c) {
    std::vector<std::string> f;
    if (c.GetBool("audio.eq")) {
        double preamp = c.GetDouble(kEqPreampKey);
        std::vector<std::string> bands;
        for (int i = 0; i < kEqBands; ++i) {
            double g = c.GetDouble(kEqBandKeys[i]);
            if (g != 0)
                bands.push_back("equalizer=f=" + Num(kEqFrequencies[i]) + ":t=o:w=1:g=" + Num(g));
        }
        if (preamp != 0) f.push_back("volume=" + Num(preamp) + "dB");
        int passes = c.GetBool("audio.eqTwoPass") ? 2 : 1;
        for (int p = 0; p < passes; ++p) f.insert(f.end(), bands.begin(), bands.end());
    }
    if (c.GetBool("audio.nightMode")) {
        double knee = std::clamp(DbToLinear(c.GetDouble("audio.compKnee")), 1.0, 8.0);
        double makeup = std::clamp(DbToLinear(c.GetDouble("audio.compMakeup")), 1.0, 64.0);
        double threshold = std::clamp(DbToLinear(c.GetDouble("audio.compThreshold")), 0.000976563, 1.0);
        f.push_back("acompressor=threshold=" + Num(threshold) + ":ratio=" + Num(c.GetDouble("audio.compRatio")) +
                    ":attack=" + Num(c.GetDouble("audio.compAttack")) +
                    ":release=" + Num(c.GetDouble("audio.compRelease")) + ":makeup=" + Num(makeup) +
                    ":knee=" + Num(knee) + ":detection=" + (c.GetDouble("audio.compRmsPeak") >= 0.5 ? "peak" : "rms"));
    }
    // After the equalizer, so normalization evens out the level the EQ changed but keeps its tone.
    if (c.GetBool("audio.normalize")) {
        if (c.GetString("audio.normalizeMode") == "dynamic")
            f.push_back("dynaudnorm=f=150:g=15:p=0.95:m=10");  // ~1 s look-ahead: short frames, small window
        else
            f.push_back("loudnorm=I=" + Num(c.GetDouble("audio.targetLevel")) + ":TP=-1.5:LRA=11");
    }
    if (c.GetBool("audio.stereoWiden")) {
        f.push_back("stereowiden=delay=" + Num(c.GetDouble("audio.widenDelay")) +
                    ":feedback=" + Num(c.GetDouble("audio.widenFeedback")) +
                    ":crossfeed=" + Num(c.GetDouble("audio.widenCrossfeed")) +
                    ":drymix=" + Num(c.GetDouble("audio.widenDryMix")));
    }
    if (c.GetBool("audio.spatial")) {
        if (c.GetString("audio.spatialMode") == "room") {
            // VLC's spatializer (Freeverb) has no libavfilter twin; approximate the room with a
            // two-tap echo: size -> delay, damp -> decay, dry/wet -> gains, width -> extrastereo.
            double size = c.GetDouble("audio.spatialSize"), damp = c.GetDouble("audio.spatialDamp");
            double dry = std::clamp(c.GetDouble("audio.spatialDry") / 10.0, 0.05, 1.0);
            double wet = std::clamp(c.GetDouble("audio.spatialWet") / 10.0, 0.05, 1.0);
            double decay = std::clamp(0.6 * (1.0 - damp / 10.0), 0.05, 0.9);
            f.push_back("aecho=in_gain=" + Num(dry) + ":out_gain=" + Num(wet) + ":delays=" +
                        Num(std::round(10 + size * 6)) + "|" + Num(std::round(17 + size * 11)) +
                        ":decays=" + Num(decay) + "|" + Num(std::round(decay * 60) / 100));
            double width = c.GetDouble("audio.spatialWidth");
            if (width > 0) f.push_back("extrastereo=m=" + Num(1.0 + width / 20.0));
        } else {
            f.push_back("crossfeed=strength=0.3:range=0.5");
        }
    }
    if (f.empty()) return std::nullopt;
    std::string graph;
    for (const std::string& s : f) graph += (graph.empty() ? "" : ",") + s;
    return "@lumen:lavfi=[" + graph + "]";
}

// --- Schema builder -----------------------------------------------------------------------

struct ItemRef {
    Item& it;

    ItemRef& Desc(W en, W ru) {
        it.description = std::move(en);
        it.descriptionRu = std::move(ru);
        return *this;
    }
    ItemRef& Unit(W en, W ru) {
        it.unit = std::move(en);
        it.unitRu = std::move(ru);
        return *this;
    }
    ItemRef& Hint(W en, W ru) {
        it.placeholder = std::move(en);
        it.placeholderRu = std::move(ru);
        return *this;
    }
    ItemRef& Secret() {
        it.secret = true;
        return *this;
    }
    ItemRef& Hidden() {
        it.hidden = true;
        return *this;
    }
    ItemRef& Inferred() {
        it.inferred = true;
        return *this;
    }
    ItemRef& Group(std::string g) {
        it.group = std::move(g);
        return *this;
    }
    ItemRef& Dynamic() {
        it.dynamicChoices = true;
        return *this;
    }
    ItemRef& Kind(PathKind k) {
        it.pathKind = k;
        return *this;
    }
    ItemRef& When(std::string key, std::string value, bool negate = false) {
        it.enabledWhen = Condition{std::move(key), std::move(value), negate};
        return *this;
    }
    ItemRef& Browse() {  // Path rows: caption of the button next to the field
        it.actionLabel = L"Browse";
        it.actionLabelRu = L"Обзор";
        return *this;
    }
    // Direct mapping for values, Choice mapping for items with choices.
    ItemRef& Mpv(std::string option) {
        MpvMap m;
        m.option = std::move(option);
        m.conv = it.HasChoices() ? MpvConv::Choice : MpvConv::Direct;
        it.mpv.push_back(std::move(m));
        return *this;
    }
    ItemRef& MpvBool(std::string option, std::string on = "yes", std::string off = "no") {
        MpvMap m;
        m.option = std::move(option);
        m.conv = MpvConv::Bool;
        m.on = std::move(on);
        m.off = std::move(off);
        it.mpv.push_back(std::move(m));
        return *this;
    }
    ItemRef& MpvBuild(std::string option, MpvBuilder build, std::string reset = {}) {
        MpvMap m;
        m.option = std::move(option);
        m.conv = MpvConv::Builder;
        m.build = build;
        m.reset = std::move(reset);
        it.mpv.push_back(std::move(m));
        return *this;
    }
    ItemRef& MpvArgs() {
        MpvMap m;
        m.option = "options";  // placeholder name; the value expands into many options
        m.conv = MpvConv::ExtraArgs;
        m.startupOnly = true;
        it.mpv.push_back(std::move(m));
        return *this;
    }
    ItemRef& Reset(std::string value) {  // applies to the last mapping
        if (!it.mpv.empty()) it.mpv.back().reset = std::move(value);
        return *this;
    }
    ItemRef& Startup() {  // applies to the last mapping
        if (!it.mpv.empty()) it.mpv.back().startupOnly = true;
        return *this;
    }
};

class Builder {
public:
    std::vector<Group> groups;
    std::vector<Category> cats;
    bool advanced = false;  // rows added now sit under "Advanced Options"
    bool inferred = false;  // rows added now are not visible in the design

    void Cat(std::string id, std::string group, std::string icon, W label, W labelRu, W title, W titleRu) {
        Category c;
        c.id = std::move(id);
        c.group = std::move(group);
        c.icon = std::move(icon);
        c.label = std::move(label);
        c.labelRu = std::move(labelRu);
        c.title = std::move(title);
        c.titleRu = std::move(titleRu);
        cats.push_back(std::move(c));
        advanced = false;
        inferred = false;
    }

    void Sec(std::string id, W title, W titleRu) {
        Section s;
        s.id = std::move(id);
        s.title = std::move(title);
        s.titleRu = std::move(titleRu);
        cats.back().sections.push_back(std::move(s));
    }

    ItemRef Add(std::string key, ItemType type, W label, W labelRu, json::Value def) {
        Item it;
        it.key = std::move(key);
        it.type = type;
        it.label = std::move(label);
        it.labelRu = std::move(labelRu);
        it.def = std::move(def);
        it.advanced = advanced;
        it.inferred = inferred;
        auto& items = cats.back().sections.back().items;
        items.push_back(std::move(it));
        return ItemRef{items.back()};
    }

    ItemRef Toggle(std::string key, W en, W ru, bool def) {
        return Add(std::move(key), ItemType::Toggle, std::move(en), std::move(ru), json::Value(def));
    }

    ItemRef Pick(std::string key, W en, W ru, std::vector<Choice> choices, std::string def,
                 ItemType type = ItemType::Choice) {
        ItemRef r = Add(std::move(key), type, std::move(en), std::move(ru), json::Value(std::move(def)));
        r.it.choices = std::move(choices);
        return r;
    }

    ItemRef Seg(std::string key, W en, W ru, std::vector<Choice> choices, std::string def) {
        return Pick(std::move(key), std::move(en), std::move(ru), std::move(choices), std::move(def),
                    ItemType::Segmented);
    }

    ItemRef Slider(std::string key, W en, W ru, double mn, double mx, double step, double def) {
        ItemRef r = Add(std::move(key), ItemType::Slider, std::move(en), std::move(ru), json::Value(def));
        r.it.min = mn;
        r.it.max = mx;
        r.it.step = step;
        return r;
    }

    ItemRef Number(std::string key, W en, W ru, double mn, double mx, double step, double def) {
        ItemRef r = Slider(std::move(key), std::move(en), std::move(ru), mn, mx, step, def);
        r.it.type = ItemType::Number;
        return r;
    }

    ItemRef Text(std::string key, W en, W ru, std::string def = {}) {
        return Add(std::move(key), ItemType::Text, std::move(en), std::move(ru), json::Value(std::move(def)));
    }

    ItemRef Path(std::string key, W en, W ru, PathKind kind) {
        ItemRef r = Add(std::move(key), ItemType::Path, std::move(en), std::move(ru), json::Value(""));
        r.it.pathKind = kind;
        r.Browse();
        return r;
    }

    ItemRef Color(std::string key, W en, W ru, std::string def) {
        return Add(std::move(key), ItemType::Color, std::move(en), std::move(ru), json::Value(std::move(def)));
    }

    ItemRef Button(std::string key, W en, W ru, std::string action, W capEn, W capRu) {
        ItemRef r = Add(std::move(key), ItemType::Button, std::move(en), std::move(ru), json::Value());
        r.it.action = std::move(action);
        r.it.actionLabel = std::move(capEn);
        r.it.actionLabelRu = std::move(capRu);
        return r;
    }

    ItemRef Info(std::string key, W en, W ru, std::string widget) {
        ItemRef r = Add(std::move(key), ItemType::Info, std::move(en), std::move(ru), json::Value());
        r.it.action = std::move(widget);
        return r;
    }
};

std::vector<Choice> LanguageChoices(bool withOriginal) {
    std::vector<Choice> out;
    out.push_back({"system", L"System language", L"Язык системы"});
    if (withOriginal) out.push_back({"original", L"Original language", L"Язык оригинала"});
    for (const LangInfo& l : kLanguages) out.push_back({l.id, l.en, l.ru});
    return out;
}

// --- The schema ----------------------------------------------------------------------------

void BuildApp(Builder& b) {
    // ===== General =====
    b.Cat("general", "app", "preferences", L"General", L"Общие", L"Global Settings", L"Общие настройки");

    b.Sec("appearance", L"Appearance", L"Оформление");
    b.Pick("general.language", L"Interface language", L"Язык интерфейса",
           {{"ru", L"Русский", L"Русский"}, {"en", L"English", L"English"}}, "ru");
    b.Seg("general.theme", L"Theme", L"Тема",
          {{"dark", L"Dark", L"Тёмная"}, {"light", L"Light", L"Светлая"}, {"system", L"System", L"Системная"}},
          "dark");
    b.Pick("general.accent", L"Accent color", L"Цвет акцента",
           {{"white", L"White", L"Белый", "", "#EDEDED"},
            {"pearl", L"Pearl", L"Жемчуг", "", "#D4D4D4"},
            {"silver", L"Silver", L"Серебро", "", "#B5B5B5"},
            {"steel", L"Steel", L"Сталь", "", "#8F8F8F"},
            {"graphite", L"Graphite", L"Графит", "", "#6B6B6B"}},
           "white", ItemType::AccentPicker);

    b.Sec("startup", L"Startup", L"Запуск");
    b.Toggle("general.showDialogs", L"Show confirmation and error dialogs", L"Показывать подтверждения и ошибки", true)
        .Desc(L"Ask before destructive actions and show a window when a file cannot be played. When off, "
              L"errors appear as short on-screen messages.",
              L"Спрашивать перед необратимыми действиями и показывать окно, если файл не открылся. "
              L"Если выключено, ошибки появляются короткими сообщениями поверх видео.");
    b.Toggle("general.checkUpdates", L"Check for updates automatically", L"Проверять обновления автоматически", false)
        .Desc(L"Look for a new version of Lumen once a day. Nothing else is sent.",
              L"Раз в день проверять, вышла ли новая версия Lumen. Больше ничего не отправляется.");
    b.Button("general.fileAssociations", L"Make Lumen the default player", L"Сделать Lumen плеером по умолчанию",
             "file-associations", L"Set up", L"Настроить")
        .Inferred()
        .Desc(L"Register Lumen for video and audio files, then pick it in Windows Default apps.",
              L"Зарегистрировать Lumen для видео- и аудиофайлов и выбрать его в разделе «Приложения по умолчанию».");

    b.Sec("diagnostics", L"Diagnostics", L"Диагностика");
    b.Toggle("general.developerMode", L"Playback statistics", L"Статистика воспроизведения", false)
        .Desc(L"Show technical details over the video: decoder, zero-copy, frame drops, bitrate, cache and A/V sync (Ctrl+Shift+I).",
              L"Показывать технические подробности поверх видео: декодер, работа без копирования, пропуски кадров, битрейт, кэш и синхронизацию (Ctrl+Shift+I).");
    b.Toggle("general.saveLog", L"Save a log", L"Сохранять журнал", false)
        .Desc(L"Write playback messages to a log file. Useful when reporting a problem.",
              L"Записывать сообщения воспроизведения в файл. Пригодится, если нужно сообщить о проблеме.")
        .MpvBuild("log-file", BuildLogFile);
    b.Path("general.logFile", L"Log file", L"Файл журнала", PathKind::SaveFile)
        .Desc(L"Where the log is written. Leave empty to use lumen.log in the Lumen data folder.",
              L"Куда записывать журнал. Если пусто — lumen.log в папке данных Lumen.")
        .Hint(DataFileDisplayPath(L"lumen.log"), DataFileDisplayPath(L"lumen.log"))
        .When("general.saveLog", "true")
        .MpvBuild("log-file", BuildLogFile);
    b.Pick("general.logLevel", L"Log details", L"Подробность журнала",
           {{"errors", L"Errors only", L"Только ошибки", "all=error"},
            {"warnings", L"Errors and warnings", L"Ошибки и предупреждения", "all=warn"},
            {"info", L"Information", L"Информация", "all=info"},
            {"debug", L"Everything (debug)", L"Всё (отладка)", "all=debug"}},
           "errors")
        .Desc(L"How much detail goes into the log. More detail makes the file grow faster.",
              L"Сколько подробностей писать в журнал. Чем больше, тем быстрее растёт файл.")
        .When("general.saveLog", "true")
        .Mpv("msg-level")
        .Reset("all=status");

    b.advanced = true;
    b.Toggle("general.minorWarnings", L"Show minor warnings", L"Показывать мелкие предупреждения", false)
        .Desc(L"Also report harmless problems, such as a damaged frame that playback recovered from.",
              L"Сообщать и о безобидных проблемах — например, о повреждённом кадре, после которого "
              L"воспроизведение восстановилось.")
        .Hidden();  // not implemented yet
    b.Text("general.extraOptions", L"Additional command line options", L"Дополнительные параметры mpv")
        .Desc(L"Extra mpv options, for example --deband=yes --sub-ass-override=force. Applied at the next start.",
              L"Дополнительные параметры mpv, например --deband=yes --sub-ass-override=force. "
              L"Применяются при следующем запуске.")
        .MpvArgs();
    b.Pick("general.logFormat", L"Log format", L"Формат журнала",
           {{"text", L"Plain text", L"Обычный текст"}, {"timestamps", L"Text with timestamps", L"Текст с метками времени"}},
           "text")
        .Desc(L"Layout of the lines in the log file.", L"Как оформлять строки в файле журнала.")
        .Hidden();  // not implemented yet
    b.Toggle("general.collectStats", L"Collect playback statistics", L"Собирать статистику воспроизведения", true)
        .Desc(L"Count plays and watch time on this computer to fill \"Frequent\" lists. Nothing leaves your device.",
              L"Считать запуски и время просмотра на этом компьютере для списка «Часто открываемые». "
              L"Данные никуда не отправляются.")
        .Hidden();  // not implemented yet

    // ===== Interface =====
    b.Cat("interface", "app", "interface", L"Interface", L"Интерфейс", L"Interface Settings", L"Настройки интерфейса");

    b.Sec("skin", L"Skin", L"Скин");
    b.Toggle("interface.customSkin", L"Use a custom skin", L"Использовать свой скин", false)
        .Desc(L"Replace the built-in look with colors and sizes from a skin file.",
              L"Заменить встроенное оформление цветами и размерами из файла скина.")
        .Hidden();  // not implemented yet
    b.Path("interface.skinFile", L"Skin file", L"Файл скина", PathKind::File)
        .Desc(L"A Lumen skin (.json) that overrides design tokens.",
              L"Скин Lumen (.json), переопределяющий токены оформления.")
        .When("interface.customSkin", "true")
        .Hidden();  // not implemented yet

    b.Sec("window", L"Window", L"Окно");
    b.Toggle("interface.embedVideo", L"Play video inside main window", L"Показывать видео в главном окне", true)
        .Desc(L"Show video in the library window instead of opening a separate player window.",
              L"Показывать видео в окне медиатеки, а не в отдельном окне плеера.")
        .Hidden();  // not implemented yet
    b.Pick("interface.autoResize", L"Resize window to fit video", L"Подгонять окно под видео",
           {{"never", L"Never", L"Никогда"},
            {"first", L"Only for the first video", L"Только для первого видео"},
            {"always", L"Always", L"Всегда"}},
           "always")
        .Desc(L"Change the window size to match the video when a file opens (up to 80% of the screen).",
              L"Менять размер окна под видео при открытии файла (не больше 80% экрана).");
    b.Toggle("interface.rememberWindow", L"Remember window size and position", L"Запоминать размер и положение окна", true);
    b.Toggle("interface.alwaysOnTop", L"Keep window above other windows", L"Поверх других окон", false)
        .Desc(L"Keep the player in front even when another app is active.",
              L"Плеер остаётся впереди, даже когда активно другое приложение.");
    b.Toggle("interface.decorations", L"Show window decorations", L"Показывать рамку окна", true)
        .Desc(L"Show the title bar and window buttons. When off, only the video and controls remain.",
              L"Показывать заголовок и кнопки окна. Если выключить, останутся только видео и панель.");
    b.Toggle("interface.titleFileName", L"Show the file name in the title bar", L"Имя файла в заголовке", true)
        .Desc(L"Show the name of the playing file in the title bar and on the taskbar.",
              L"Показывать имя текущего файла в заголовке окна и на панели задач.");
    b.Pick("interface.fullscreenScreen", L"Open fullscreen on", L"Полный экран на",
           {{"current", L"Current screen", L"Текущем экране"}, {"primary", L"Primary screen", L"Основном экране"}},
           "current")
        .Desc(L"Which monitor to use for fullscreen when you have several.",
              L"Какой монитор использовать для полного экрана, если их несколько.");

    b.Sec("controls", L"Playback Controls", L"Панель управления");
    b.Toggle("interface.compactMode", L"Start in compact mode", L"Запускать в компактном режиме", false)
        .Desc(L"Open with a small window and the minimal control bar.",
              L"Открываться в маленьком окне с минимальной панелью управления.")
        .Hidden();  // not implemented yet
    b.Pick("interface.autoHide", L"Auto-hide the controls", L"Скрывать панель управления",
           {{"never", L"Never", L"Никогда"},
            {"fullscreen", L"In fullscreen only", L"Только в полноэкранном режиме"},
            {"always", L"Always", L"Всегда"}},
           "fullscreen")
        .Desc(L"Hide the control bar and the mouse pointer when the mouse stops moving during playback.",
              L"Скрывать панель и указатель мыши, когда мышь не двигается во время воспроизведения.");
    b.Slider("interface.hideDelay", L"Hide after", L"Скрывать через", 0, 10, 0.5, 2)
        .Unit(L"s", L"с")
        .When("interface.autoHide", "never", true);
    b.Toggle("interface.controlsInFullscreen", L"Show controls in fullscreen", L"Панель в полноэкранном режиме", true)
        .Desc(L"Show the control bar when you move the mouse in fullscreen. When off, use the keyboard only.",
              L"Показывать панель при движении мыши в полноэкранном режиме. Если выключить — только клавиатура.");
    b.Pick("interface.thinProgress", L"Thin progress line", L"Тонкая полоска прогресса",
           {{"never", L"Never", L"Никогда"},
            {"fullscreen", L"In fullscreen", L"В полноэкранном режиме"},
            {"always", L"Always", L"Всегда"}},
           "fullscreen")
        .Desc(L"Show a thin progress line along the bottom of the video while the controls are hidden.",
              L"Показывать тонкую полоску прогресса внизу видео, пока панель управления скрыта.");

    b.Sec("notifications", L"Notifications and System Tray", L"Уведомления и системный трей");
    b.inferred = true;
    b.Toggle("interface.osdMessages", L"Show on-screen messages", L"Сообщения поверх видео", true)
        .Desc(L"Briefly show volume, speed and track changes over the video.",
              L"Ненадолго показывать громкость, скорость и смену дорожек поверх видео.");
    b.Toggle("interface.notifyTrackChange", L"Notify when the track changes", L"Уведомлять о смене трека", false)
        .Desc(L"Show a Windows notification with the title and cover when the next song starts.",
              L"Показывать уведомление Windows с названием и обложкой, когда начинается следующая песня.");
    b.Toggle("interface.trayIcon", L"Show an icon in the system tray", L"Значок в области уведомлений", false);
    b.Toggle("interface.minimizeToTray", L"Minimize to the system tray", L"Сворачивать в область уведомлений", false)
        .When("interface.trayIcon", "true");
    b.Toggle("interface.mediaControls", L"Use Windows media controls", L"Медиаклавиши и плашка Windows", true)
        .Desc(L"Respond to media keys and show the playing item in the Windows volume flyout.",
              L"Реагировать на медиаклавиши и показывать текущий файл в плашке громкости Windows.");
    b.Toggle("interface.taskbarProgress", L"Show progress on the taskbar button", L"Прогресс на кнопке в панели задач", true);

    // ===== Privacy =====
    b.Cat("privacy", "app", "privacy", L"Privacy", L"Конфиденциальность", L"Privacy Settings",
          L"Конфиденциальность");

    b.Sec("history", L"History", L"История");
    b.Toggle("privacy.saveHistory", L"Save recently played items", L"Сохранять недавние файлы", true)
        .Desc(L"Keep a list of played files for Recent, Continue watching and Listen again.",
              L"Вести список открытых файлов для разделов «Недавние», «Продолжить просмотр» и «Послушать снова».");
    b.Toggle("privacy.rememberPosition", L"Remember the playback position", L"Запоминать место остановки", true)
        .Desc(L"Save where you stopped and continue from there next time.",
              L"Сохранять, где вы остановились, и продолжать с этого места в следующий раз.");  // Player::rememberPosition
    b.Text("privacy.excludePatterns", L"Never remember items matching", L"Не запоминать файлы по маске")
        .Desc(L"Files matching these patterns stay out of the history. Separate patterns with ';', "
              L"for example *\\Private\\*;*.tmp.mkv",
              L"Файлы, подходящие под эти маски, не попадают в историю. Маски разделяются «;», "
              L"например *\\Private\\*;*.tmp.mkv")
        .When("privacy.saveHistory", "true");
    b.Button("privacy.clearHistory", L"Clear history now", L"Очистить историю", "clear-history", L"Clear", L"Очистить")
        .Desc(L"Forget all recently played items and saved positions. This cannot be undone.",
              L"Забыть все недавние файлы и сохранённые позиции. Отменить нельзя.");

    b.Sec("network", L"Network", L"Сеть");
    b.Toggle("privacy.fetchMetadata", L"Fetch titles and cover art from the internet",
             L"Загружать названия и обложки из интернета", false)
        .Desc(L"Look up missing titles and album art online. File names are sent to the metadata service.",
              L"Искать недостающие названия и обложки в интернете. Имена файлов передаются сервису метаданных.");
    b.Toggle("privacy.lastfmSubmit", L"Submit played tracks to last.fm", L"Отправлять прослушанное в last.fm", false)
        .Desc(L"Scrobble the songs you listen to your last.fm profile.",
              L"Отмечать прослушанные песни в вашем профиле last.fm (скробблинг).")
        .Hidden();  // not implemented yet
    b.Text("privacy.lastfmUser", L"Last.fm username", L"Имя пользователя Last.fm")
        .Desc(L"Your last.fm account name.", L"Имя вашей учётной записи last.fm.")
        .When("privacy.lastfmSubmit", "true")
        .Hidden();  // not implemented yet
    b.Text("privacy.lastfmPassword", L"Last.fm password", L"Пароль Last.fm")
        .Desc(L"Used once to obtain a session key; it is not sent anywhere else.",
              L"Нужен один раз для получения ключа сессии и больше никуда не передаётся.")
        .Secret()
        .When("privacy.lastfmSubmit", "true")
        .Hidden();  // not implemented yet
    b.Text("privacy.lastfmUrl", L"Last.fm submission URL", L"Адрес отправки Last.fm", "post.audioscrobbler.com")
        .Desc(L"Scrobbling server. Change it only for a compatible service such as Libre.fm.",
              L"Сервер скробблинга. Меняйте только для совместимого сервиса, например Libre.fm.")
        .When("privacy.lastfmSubmit", "true")
        .Hidden();  // not implemented yet

    // ===== Controls =====
    b.Cat("controls", "app", "hotkeys", L"Controls", L"Управление", L"Mouse, Keyboard & Remote Controls",
          L"Мышь, клавиатура и пульт");

    b.Sec("shortcuts", L"Keyboard Shortcuts", L"Сочетания клавиш");
    b.Button("controls.editShortcuts", L"Defaults", L"Сочетания по умолчанию", "edit-shortcuts", L"Edit Keyboard Shortcuts",
             L"Изменить сочетания клавиш");
    b.Button("controls.resetShortcuts", L"Reset all shortcuts", L"Сбросить все сочетания", "reset-shortcuts", L"Reset",
             L"Сбросить");
    b.Add("controls.keyBindings", ItemType::KeyBindings, L"Keyboard shortcuts", L"Сочетания клавиш",
          json::Value::MakeObject())
        .Hidden()
        .Desc(L"Overrides of the default shortcuts, edited in the Keyboard Shortcuts dialog.",
              L"Изменённые сочетания клавиш; редактируются в окне «Сочетания клавиш».");

    b.Sec("skip", L"Skip Amounts", L"Шаг перемотки");
    b.Number("controls.skipVeryShort", L"Very short skip", L"Очень короткий", 1, 3600, 1, 3).Unit(L"s", L"с");
    b.Number("controls.skipShort", L"Short skip", L"Короткий", 1, 3600, 1, 10).Unit(L"s", L"с");
    b.Number("controls.skipMedium", L"Medium skip", L"Средний", 1, 3600, 1, 60).Unit(L"s", L"с");
    b.Number("controls.skipLong", L"Long skip", L"Длинный", 1, 3600, 1, 300).Unit(L"s", L"с")
        .Hidden();  // not implemented yet: needs its own Action (app/Actions.h)

    b.Sec("mouse", L"Mouse", L"Мышь");
    b.Pick("controls.singleClick", L"Single click on the video", L"Одиночный щелчок по видео",
           {{"playPause", L"Play / Pause", L"Пауза / продолжить"},
            {"showControls", L"Show controls", L"Показать панель"},
            {"none", L"Do nothing", L"Ничего"}},
           "playPause");
    b.Pick("controls.doubleClick", L"Double click on the video", L"Двойной щелчок по видео",
           {{"fullscreen", L"Toggle fullscreen", L"Полный экран"},
            {"playPause", L"Play / Pause", L"Пауза / продолжить"},
            {"none", L"Do nothing", L"Ничего"}},
           "fullscreen");
    b.Pick("controls.wheel", L"Scroll wheel", L"Колесо мыши",
           {{"volume", L"Volume", L"Громкость"}, {"seek", L"Seek", L"Перемотка"}, {"none", L"Do nothing", L"Ничего"}},
           "volume");
    b.Pick("controls.hwheel", L"Horizontal scroll", L"Горизонтальная прокрутка",
           {{"seek", L"Seek", L"Перемотка"}, {"volume", L"Volume", L"Громкость"}, {"none", L"Do nothing", L"Ничего"}},
           "seek");
    b.Toggle("controls.reverseScroll", L"Reverse scroll direction", L"Обратное направление прокрутки", false);

    b.Sec("remotes", L"Remotes", L"Удалённое управление");
    b.Toggle("controls.webRemote", L"Web remote control", L"Веб-пульт", false)
        .Hidden();  // not implemented yet
    b.Number("controls.webRemotePort", L"Port", L"Порт", 1024, 65535, 1, 8080).When("controls.webRemote", "true")
        .Hidden();  // not implemented yet
    b.inferred = true;
    b.Text("controls.webRemotePassword", L"Password", L"Пароль")
        .Secret()
        .Desc(L"Required to control Lumen from a browser on another device.",
              L"Нужен, чтобы управлять Lumen из браузера на другом устройстве.")
        .When("controls.webRemote", "true")
        .Hidden();  // not implemented yet
}

void BuildMedia(Builder& b) {
    // ===== Playback =====
    b.Cat("playback", "media", "playback", L"Playback", L"Воспроизведение", L"Playback Preferences",
          L"Параметры воспроизведения");

    b.Sec("startup", L"Startup", L"Запуск");
    b.Pick("playback.resume", L"Continue where you left off", L"Продолжать с места остановки",
           {{"always", L"Always", L"Всегда", "yes"}, {"ask", L"Ask", L"Спрашивать", "no"},
            {"never", L"Never", L"Никогда", "no"}},
           "always")
        .Desc(L"Resume a file from the saved position. \"Ask\" offers to resume in a message instead.",
              L"Продолжать файл с сохранённого места. «Спрашивать» — предложить это в сообщении.")
        .Mpv("resume-playback");
    b.Toggle("playback.autoPlay", L"Start playing as soon as a file opens", L"Начинать воспроизведение сразу", true)
        .Desc(L"When off, files open paused on the first frame.",
              L"Если выключено, файл открывается на паузе на первом кадре.")
        .MpvBool("pause", "no", "yes")
        .Startup();
    b.Slider("playback.speed", L"Default playback speed", L"Скорость по умолчанию", 0.25, 2, 0.25, 1)
        .Unit(L"×", L"×")
        .Mpv("speed")
        .Startup();
    b.Toggle("playback.pauseOnAudioFocus", L"Pause when another app uses audio",
             L"Пауза, когда звук нужен другому приложению", false)
        .Desc(L"Pause during calls and other communication sessions, then continue afterwards.",
              L"Ставить на паузу во время звонков и других сеансов связи и продолжать после них.")
        .Hidden();  // not implemented yet

    b.Sec("tracks", L"Tracks", L"Дорожки");
    b.Pick("playback.videoQuality", L"Preferred video quality", L"Предпочтительное качество",
           {{"best", L"Best available", L"Лучшее доступное", "bestvideo+bestaudio/best"},
            {"2160", L"Up to 2160p", L"До 2160p", "bestvideo[height<=?2160]+bestaudio/best"},
            {"1440", L"Up to 1440p", L"До 1440p", "bestvideo[height<=?1440]+bestaudio/best"},
            {"1080", L"Up to 1080p", L"До 1080p", "bestvideo[height<=?1080]+bestaudio/best"},
            {"720", L"Up to 720p", L"До 720p", "bestvideo[height<=?720]+bestaudio/best"},
            {"480", L"Up to 480p", L"До 480p", "bestvideo[height<=?480]+bestaudio/best"}},
           "best")
        .Desc(L"Highest resolution to request from online video sites.",
              L"Максимальное разрешение при открытии видео с сайтов.")
        .Mpv("ytdl-format");
    b.Pick("playback.menuLanguage", L"Preferred menu language", L"Язык меню диска", LanguageChoices(false), "system")
        .Desc(L"Language for DVD and Blu-ray menus when the disc offers several.",
              L"Язык меню DVD и Blu-ray, если на диске их несколько.")
        .Hidden();  // not implemented yet
    b.Number("playback.audioTrack", L"Default audio track", L"Аудиодорожка по умолчанию", 0, 99, 1, 0)
        .Desc(L"Track number to start with. 0 picks automatically by the preferred language.",
              L"С какой дорожки начинать. 0 — выбрать автоматически по предпочтительному языку.")
        .MpvBuild("aid", BuildAid, "auto");
    b.Number("playback.subtitleTrack", L"Default subtitle track", L"Дорожка субтитров по умолчанию", 0, 99, 1, 0)
        .Desc(L"Subtitle track number to start with. 0 picks automatically by the preferred language.",
              L"С какой дорожки субтитров начинать. 0 — выбрать автоматически по предпочтительному языку.")
        .MpvBuild("sid", BuildSid, "auto");

    b.Sec("order", L"Order", L"Порядок");
    b.Pick("playback.repeat", L"Repeat", L"Повтор",
           {{"off", L"Off", L"Выкл."}, {"one", L"Current item", L"Текущий файл"}, {"all", L"All items", L"Все файлы"}},
           "off")
        .Desc(L"Play the current file or the whole playlist again when it ends.",
              L"Повторять текущий файл или весь плейлист после окончания.")
        .MpvBuild("loop-file", BuildLoopFile, "no")
        .MpvBuild("loop-playlist", BuildLoopPlaylist, "no");
    b.Toggle("playback.shuffle", L"Shuffle", L"Перемешать", false)
        .Desc(L"Play the playlist in random order.", L"Воспроизводить плейлист в случайном порядке.")
        .MpvBool("shuffle");
    b.Toggle("playback.folderAsPlaylist", L"Play the folder's other files next", L"Играть остальные файлы папки",
             true)
        .Desc(L"Opening one file queues the files next to it, like a series. Off: only that file plays.",
              L"Открытый файл ставит в очередь соседние файлы, как серии. Если выключено, играет только он.");
    b.Toggle("playback.stopAfterEach", L"Stop after each item", L"Останавливаться после каждого файла", false)
        .Desc(L"Pause at the end of every file instead of moving to the next one.",
              L"Ставить на паузу в конце каждого файла, а не переходить к следующему.");
    b.Pick("playback.whenFinished", L"When playback finishes", L"По окончании воспроизведения",
           {{"stop", L"Stop", L"Остановить"},
            {"next", L"Play the next file in the folder", L"Следующий файл в папке"},
            {"close", L"Close the player", L"Закрыть плеер"}},
           "stop")
        .Desc(L"What to do after the last item of the playlist.", L"Что делать после последнего файла плейлиста.");

    b.Sec("seeking", L"Seeking", L"Перемотка");
    b.Toggle("playback.seekPrecise", L"Seek precisely", L"Точная перемотка", true)
        .Desc(L"Jump to the exact frame. Off is faster but lands on the nearest keyframe.",
              L"Переходить точно на нужный кадр. Без этого быстрее, но переход идёт к ближайшему ключевому кадру.")
        .MpvBool("hr-seek", "yes", "default");
    b.inferred = true;
    b.Toggle("playback.seekPreview", L"Show a preview over the seek bar", L"Превью кадра над полосой перемотки", true)
        .Desc(L"Show a small frame preview while hovering the seek bar.",
              L"Показывать маленький кадр при наведении на полосу перемотки.");

    // ===== Video =====
    b.Cat("video", "media", "video", L"Video", L"Видео", L"Video Defaults", L"Видео по умолчанию");

    b.Sec("output", L"Output", L"Вывод");
    b.Toggle("video.enabled", L"Enable video", L"Показывать видео", true)
        .Desc(L"Turn off to play only the sound of video files.", L"Выключите, чтобы у видеофайлов играл только звук.")
        .MpvBuild("vid", BuildVid, "auto");
    b.Toggle("video.startFullscreen", L"Start videos in fullscreen", L"Открывать видео на весь экран", false)
        .Desc(L"Switch to fullscreen as soon as a video starts.", L"Переходить в полноэкранный режим при запуске видео.");
    b.Toggle("video.stopScreensaver", L"Stop the screensaver during playback", L"Отключать заставку при просмотре", true)
        .Desc(L"Keep the screen on while a video is playing.", L"Не давать экрану гаснуть, пока идёт видео.")
        .MpvBool("stop-screensaver");
    b.Toggle("video.wallpaper", L"Play video as desktop wallpaper", L"Видео вместо обоев рабочего стола", false)
        .Desc(L"Draw the video behind the desktop icons. Controls stay in the tray menu.",
              L"Показывать видео под значками рабочего стола. Управление — через меню в трее.")
        .Hidden();  // not implemented yet
    b.inferred = true;
    b.Pick("video.hdr", L"HDR output", L"Вывод HDR",
           {{"auto", L"Auto", L"Авто"},
            {"off", L"Off", L"Выкл."},
            {"hdr10", L"HDR10", L"HDR10"},
            {"hdr10plus", L"HDR10+", L"HDR10+"},
            {"dolbyvision", L"Dolby Vision", L"Dolby Vision"},
            {"hlg", L"HLG", L"HLG"}},
           "auto")
        .Desc(L"Auto sends HDR to the display when Windows HDR is on and tone-maps to SDR otherwise.",
              L"«Авто» передаёт HDR на дисплей, если в Windows включён HDR, иначе приводит картинку к SDR.")
        .MpvBuild("target-colorspace-hint", BuildHdrHint, "yes")
        .MpvBuild("target-trc", BuildHdrTrc, "auto")
        .MpvBuild("target-prim", BuildHdrPrim, "auto");
    b.inferred = false;

    b.inferred = true;
    b.Sec("quality", L"Image Quality", L"Качество изображения");
    {
        auto q = b.Pick("video.quality", L"Image quality", L"Качество изображения",
                        {{"fast", L"Fast", L"Быстро"},
                         {"balanced", L"Balanced", L"Сбалансированно"},
                         {"high", L"High quality", L"Максимум"}},
                        "balanced");
        q.Desc(L"How the picture is scaled and processed on the graphics card. Fast suits weak or battery-powered "
               L"laptops; High quality needs a dedicated graphics card for 4K.",
               L"Как видеокарта масштабирует и обрабатывает картинку. «Быстро» подходит слабым ноутбукам и работе "
               L"от батареи; для «Максимума» в 4K нужна дискретная видеокарта.")
            .MpvBuild("scale", BuildScale, "lanczos")
            .MpvBuild("dscale", BuildFastScaler, "hermite")
            .MpvBuild("cscale", BuildFastScaler, "")
            .MpvBuild("dither", BuildFastOff, "fruit")
            .MpvBuild("correct-downscaling", BuildFastOff, "yes")
            .MpvBuild("linear-downscaling", BuildFastOff, "yes")
            .MpvBuild("sigmoid-upscaling", BuildFastOff, "yes")
            .MpvBuild("scale-antiring", BuildAntiring, "0")
            .MpvBuild("hdr-peak-percentile", BuildPeakPercentile, "0")
            .MpvBuild("hdr-contrast-recovery", BuildContrastRecovery, "0")
            .MpvBuild("hdr-compute-peak", BuildComputePeak, "auto");
    }
    b.Pick("video.upscaler", L"Upscaler", L"Алгоритм увеличения",
           {{"auto", L"From the quality setting", L"По настройке качества"},
            {"bilinear", L"Bilinear (fastest)", L"Билинейный (самый быстрый)"},
            {"spline36", L"Spline36", L"Spline36"},
            {"lanczos", L"Lanczos", L"Lanczos"},
            {"ewa_lanczos", L"EWA Lanczos", L"EWA Lanczos"},
            {"ewa_lanczossharp", L"EWA Lanczos Sharp (best)", L"EWA Lanczos Sharp (лучший)"}},
           "auto")
        .Desc(L"Filter used to enlarge videos smaller than the window.",
              L"Фильтр, которым увеличиваются видео меньше окна.")
        .MpvBuild("scale", BuildScale, "lanczos");
    b.Toggle("video.superResolution", L"AI upscaling (NVIDIA RTX / Intel)", L"ИИ-апскейл (NVIDIA RTX / Intel)", false)
        .Desc(L"Enlarge low-resolution video with the graphics card's Video Super Resolution. Needs an NVIDIA RTX or "
              L"Intel Arc / Core Ultra GPU and Direct3D 11 hardware decoding.",
              L"Увеличивать видео низкого разрешения через Video Super Resolution видеокарты. Нужна NVIDIA RTX или "
              L"Intel Arc / Core Ultra и аппаратное декодирование Direct3D 11.");
    b.Toggle("video.trueHdr", L"NVIDIA RTX Video HDR", L"HDR из SDR (NVIDIA RTX Video HDR)", false)
        .Desc(L"Turn SDR video into HDR on NVIDIA RTX cards. Needs Windows HDR on and Direct3D 11 hardware decoding.",
              L"Превращать SDR-видео в HDR на видеокартах NVIDIA RTX. Нужны включённый HDR в Windows и аппаратное "
              L"декодирование Direct3D 11.");
    b.Toggle("video.deband", L"Debanding", L"Устранение полос", false)
        .Desc(L"Smooth out color banding in gradients (skies, dark scenes).",
              L"Сглаживает ступеньки в градиентах (небо, тёмные сцены).")
        .MpvBuild("deband", BuildDeband, "no")
        .MpvBuild("deband-range", BuildDebandRange, "16");
    b.Slider("video.debandRange", L"Banding removal radius", L"Радиус устранения полос", 1, 64, 1, 16)
        .Desc(L"How far debanding looks for steps. 16 suits most videos.",
              L"Как далеко искать ступеньки. Для большинства видео подходит 16.")
        .When("video.deband", "true")
        .MpvBuild("deband-range", BuildDebandRange, "16");
    b.Slider("video.filmGrain", L"Film grain variance", L"Плёночное зерно", 0, 256, 1, 48)
        .Desc(L"Grain added after banding removal to hide remaining steps.",
              L"Зерно, которое добавляется после устранения полос, чтобы скрыть остатки ступенек.")
        .When("video.deband", "true")
        .Mpv("deband-grain");

    b.Sec("tonemapping", L"HDR Tone Mapping", L"Тональная компрессия HDR");
    b.Pick("video.toneMapping", L"Tone mapping", L"Тональная компрессия",
           {{"auto", L"Automatic", L"Автоматически", "auto"},
            {"bt.2390", L"BT.2390", L"BT.2390", "bt.2390"},
            {"bt.2446a", L"BT.2446a", L"BT.2446a", "bt.2446a"},
            {"spline", L"Spline", L"Spline", "spline"},
            {"st2094-40", L"ST 2094-40 (HDR10+)", L"ST 2094-40 (HDR10+)", "st2094-40"},
            {"hable", L"Hable (filmic)", L"Hable (кинематографичная)", "hable"},
            {"mobius", L"Mobius", L"Mobius", "mobius"},
            {"reinhard", L"Reinhard", L"Reinhard", "reinhard"},
            {"clip", L"Clip", L"Обрезка", "clip"}},
           "auto")
        .Desc(L"How HDR brightness is fitted to a screen that can't show all of it.",
              L"Как яркость HDR укладывается в возможности экрана.")
        .Mpv("tone-mapping");
    b.Pick("video.gamutMapping", L"Gamut mapping", L"Преобразование цветового охвата",
           {{"auto", L"Automatic", L"Автоматически", "auto"},
            {"perceptual", L"Perceptual", L"Перцептивное", "perceptual"},
            {"relative", L"Relative colorimetric", L"Относительное колориметрическое", "relative"},
            {"saturation", L"Saturation", L"Насыщенность", "saturation"},
            {"desaturate", L"Desaturate", L"Обесцвечивание", "desaturate"},
            {"clip", L"Clip", L"Обрезка", "clip"}},
           "auto")
        .Desc(L"How wide-gamut colors are fitted to the screen.", L"Как широкий цветовой охват укладывается в экран.")
        .Mpv("gamut-mapping-mode");
    b.Pick("video.hdrComputePeak", L"Measure scene brightness", L"Измерять яркость сцены",
           {{"auto", L"Automatic", L"Автоматически"}, {"yes", L"On", L"Вкл."}, {"no", L"Off", L"Выкл."}}, "auto")
        .Desc(L"Analyze each frame's brightness for more accurate tone mapping. Costs some GPU time.",
              L"Анализировать яркость каждого кадра для более точной тональной компрессии. Немного нагружает видеокарту.")
        .MpvBuild("hdr-compute-peak", BuildComputePeak, "auto");
    b.Number("video.targetPeak", L"Display peak brightness", L"Пиковая яркость экрана", 0, 10000, 10, 0)
        .Unit(L"nits", L"нит")
        .Desc(L"Brightest white your screen can show. 0 detects it automatically.",
              L"Самый яркий белый, который может показать экран. 0 — определить автоматически.")
        .MpvBuild("target-peak", BuildTargetPeak, "auto");

    b.Sec("motion", L"Motion", L"Движение");
    b.Toggle("video.smoothMotion", L"Smooth motion", L"Плавное движение", false)
        .Desc(L"Sync video to the display and blend frames, so 24 fps films pan without judder on 60 Hz screens.",
              L"Подстраивать видео под частоту экрана и смешивать кадры, чтобы фильмы 24 кадр/с без рывков шли на "
              L"экранах 60 Гц.")
        .MpvBuild("video-sync", BuildVideoSync, "audio")
        .MpvBool("interpolation")
        .MpvBuild("tscale", BuildTscale, "oversample");
    b.Pick("video.motionBlend", L"Frame blending", L"Смешивание кадров",
           {{"oversample", L"Sharp", L"Чёткое"}, {"linear", L"Soft", L"Мягкое"}}, "oversample")
        .Desc(L"Sharp keeps frames crisp and blends only at their edges; Soft blends more and looks smoother.",
              L"«Чёткое» сохраняет резкость и смешивает кадры только на стыках; «Мягкое» смешивает сильнее и "
              L"выглядит плавнее.")
        .When("video.smoothMotion", "true")
        .MpvBuild("tscale", BuildTscale, "oversample");
    // Enabled user shaders ("a.glsl|b.hook" in <data>\shaders); toggled in the player's quick settings.
    b.Sec("shaders", L"Shaders", L"Шейдеры");
    b.Text("video.shaders", L"Custom shaders", L"Свои шейдеры")
        .Desc(L"Shader files (.glsl, .hook) from the shaders folder in %APPDATA%\\Lumen, separated by |. Easier to "
              L"turn on one by one in the player: Settings > Video > Shaders.",
              L"Файлы шейдеров (.glsl, .hook) из папки shaders в %APPDATA%\\Lumen через |. Проще включать по одному "
              L"в плеере: «Настройки > Видео > Шейдеры».")
        .MpvBuild("glsl-shaders", BuildGlslShaders, "");
    b.inferred = false;

    b.Sec("picture", L"Picture", L"Изображение");
    b.Slider("video.hue", L"Hue", L"Оттенок", -100, 100, 1, 0).Mpv("hue");
    b.Slider("video.brightness", L"Brightness", L"Яркость", -100, 100, 1, 0).Mpv("brightness");
    b.Slider("video.contrast", L"Contrast", L"Контраст", -100, 100, 1, 0).Mpv("contrast");
    b.Slider("video.saturation", L"Saturation", L"Насыщенность", -100, 100, 1, 0).Mpv("saturation");
    b.Slider("video.gamma", L"Gamma", L"Гамма", -100, 100, 1, 0).Mpv("gamma");
    // From components/Video-Essential (debanding and film grain moved to Image Quality).
    b.inferred = true;
    b.Slider("video.sharpen", L"Sharpen", L"Резкость", 0, 10, 0.5, 0)
        .Desc(L"Contrast-adaptive sharpening on the graphics card. 0 turns it off.",
              L"Адаптивное повышение резкости на видеокарте. 0 — выключено.")
        .MpvBuild("glsl-shaders", BuildGlslShaders, "")
        .MpvBuild("glsl-shader-opts", BuildShaderOpts, "")
        .MpvBuild("vf", BuildVf);
    b.inferred = false;
    b.Button("video.resetPicture", L"Reset picture", L"Сбросить изображение", "reset-picture", L"Reset", L"Сбросить");

    b.Sec("deinterlacing", L"Deinterlacing", L"Устранение чересстрочности");
    b.Pick("video.deinterlace", L"Deinterlacing", L"Устранение чересстрочности",
           {{"auto", L"Automatic", L"Автоматически"}, {"on", L"On", L"Вкл."}, {"off", L"Off", L"Выкл."}}, "auto")
        .Desc(L"Remove comb lines from interlaced video (TV recordings, DVDs). Automatic applies it only to "
              L"interlaced frames.",
              L"Убирает «гребёнку» в чересстрочном видео (записи ТВ, DVD). «Автоматически» — только для "
              L"чересстрочных кадров.")
        .MpvBuild("deinterlace", BuildDeinterlace, "no");
    // Automatic is mpv's switch: the graphics card's video processor (d3d11vpp) with hardware decoding.
    b.Pick("video.deinterlaceMethod", L"Method", L"Метод",
           {{"auto", L"Graphics card (recommended)", L"Видеокарта (рекомендуется)"},
            {"bwdif", L"Bwdif (CPU-heavy)", L"Bwdif (нагружает процессор)"},
            {"yadif", L"Yadif (CPU-heavy)", L"Yadif (нагружает процессор)"}},
           "auto")
        .Desc(L"The graphics card deinterlaces without extra CPU load. Bwdif and Yadif run on the processor and copy "
              L"every frame to memory.",
              L"Видеокарта устраняет чересстрочность без нагрузки на процессор. Bwdif и Yadif работают на процессоре и "
              L"копируют каждый кадр в память.")
        .When("video.deinterlace", "off", true)
        .MpvBuild("deinterlace", BuildDeinterlace, "no")
        .MpvBuild("vf", BuildVf);

    b.Sec("framing", L"Framing", L"Кадрирование");
    b.Pick("video.aspect", L"Aspect ratio", L"Соотношение сторон",
           {{"auto", L"Automatic", L"Автоматически", "no"},
            {"16:9", L"16:9", L"16:9", "16:9"},
            {"4:3", L"4:3", L"4:3", "4:3"},
            {"21:9", L"21:9", L"21:9", "21:9"},
            {"2.35:1", L"2.35:1", L"2.35:1", "2.35:1"},
            {"16:10", L"16:10", L"16:10", "16:10"},
            {"5:4", L"5:4", L"5:4", "5:4"},
            {"1:1", L"1:1", L"1:1", "1:1"}},
           "auto")
        .Desc(L"Force a picture shape when a file reports the wrong one.",
              L"Принудительная форма кадра, если файл сообщает неправильную.")
        .Mpv("video-aspect-override");
    b.Pick("video.crop", L"Crop", L"Обрезка",
           {{"none", L"None", L"Нет"},
            {"16:9", L"16:9", L"16:9"},
            {"4:3", L"4:3", L"4:3"},
            {"21:9", L"21:9", L"21:9"},
            {"2.35:1", L"2.35:1", L"2.35:1"},
            {"custom", L"Custom", L"Своя"}},
           "none")
        .Desc(L"Cut black bars or crop the picture to a shape.", L"Убрать чёрные полосы или обрезать кадр до нужной формы.");
    b.inferred = true;
    // components/Video-Crop. Crop is mpv's video-crop, set per file by app/player/LiveState (it needs
    // the video size); the GPU cuts the picture, frames stay in video memory.
    for (auto [key, en, ru] : {std::tuple{"video.cropTop", L"Top", L"Сверху"},
                               std::tuple{"video.cropBottom", L"Bottom", L"Снизу"},
                               std::tuple{"video.cropLeft", L"Left", L"Слева"},
                               std::tuple{"video.cropRight", L"Right", L"Справа"}}) {
        b.Number(key, en, ru, 0, 4000, 1, 0)
            .Unit(L"px", L"пкс")
            .Group("crop")
            .When("video.crop", "custom");
    }
    b.Toggle("video.keepAspect", L"Keep aspect ratio when resizing", L"Сохранять пропорции при изменении окна", true)
        .MpvBool("keepaspect");

    b.Sec("screenshots", L"Screenshots", L"Скриншоты");
    b.Path("video.screenshotDir", L"Save screenshots to", L"Папка для скриншотов", PathKind::Folder)
        .Hint(L"Pictures\\Lumen", L"Изображения\\Lumen")
        .MpvBuild("screenshot-dir", BuildScreenshotDir);
    b.Pick("video.screenshotFormat", L"Screenshot format", L"Формат скриншотов",
           {{"png", L"PNG", L"PNG", "png"}, {"jpg", L"JPEG", L"JPEG", "jpg"}, {"webp", L"WebP", L"WebP", "webp"}},
           "png")
        .Mpv("screenshot-format");
    b.Slider("video.screenshotQuality", L"JPEG quality", L"Качество JPEG", 1, 100, 1, 90)
        .When("video.screenshotFormat", "jpg")
        .Mpv("screenshot-jpeg-quality");
    b.Toggle("video.screenshotSubtitles", L"Include subtitles", L"Вместе с субтитрами", true);
    b.advanced = true;
    b.Text("video.screenshotTemplate", L"File name template", L"Шаблон имени файла", "%F-%P")
        .Desc(L"mpv template: %F is the file name, %P the time with milliseconds, %n a counter.",
              L"Шаблон mpv: %F — имя файла, %P — время с миллисекундами, %n — счётчик.")
        .Mpv("screenshot-template")
        .Reset("mpv-shot%n");

    // ===== Audio =====
    b.Cat("audio", "media", "audio", L"Audio", L"Аудио", L"Audio Defaults", L"Аудио по умолчанию");

    b.Sec("output", L"Output", L"Вывод");
    b.Toggle("audio.enabled", L"Enable audio", L"Включить звук", true)
        .Desc(L"Turn off to play video without sound.", L"Выключите, чтобы смотреть видео без звука.")
        .MpvBuild("aid", BuildAid, "auto");
    b.Pick("audio.device", L"Output device", L"Устройство вывода", {{"auto", L"System default", L"Системное по умолчанию"}},
           "auto")
        .Dynamic()
        .Desc(L"Speakers or headphones to play through. System default follows the Windows setting.",
              L"Колонки или наушники для воспроизведения. «Системное» следует настройке Windows.")
        .Mpv("audio-device");
    b.Pick("audio.channels", L"Speaker setup", L"Конфигурация колонок",
           {{"auto", L"Automatic", L"Автоматически", "auto-safe"},
            {"stereo", L"Stereo", L"Стерео", "stereo"},
            {"5.1", L"5.1 surround", L"Объёмный 5.1", "5.1"},
            {"7.1", L"7.1 surround", L"Объёмный 7.1", "7.1"},
            {"mono", L"Mono", L"Моно", "mono"}},
           "auto")
        .Desc(L"Number of speakers. Automatic matches the output device; other values downmix or upmix.",
              L"Количество колонок. «Автоматически» — как у устройства, иначе звук сводится или расширяется.")
        .Mpv("audio-channels");
    b.inferred = true;
    b.Toggle("audio.exclusive", L"Exclusive mode", L"Монопольный режим", false)
        .Desc(L"Use the output device for Lumen alone (WASAPI exclusive): no Windows mixer, the file's own sample "
              L"rate. Other apps are silent while Lumen plays.",
              L"Занимать устройство вывода только для Lumen (монопольный WASAPI): без микшера Windows, с частотой "
              L"самого файла. Другие приложения в это время не слышны.")
        .MpvBool("audio-exclusive");
    b.Toggle("audio.gapless", L"Gapless playback", L"Воспроизведение без пауз", true)
        .Desc(L"Keep the sound device open between files, so the next song starts without a pause or a click.",
              L"Не закрывать звуковое устройство между файлами, чтобы следующая песня начиналась без паузы и щелчка.")
        .MpvBool("gapless-audio", "weak", "no")
        .MpvBool("prefetch-playlist");  // opens the next queued file before this one ends
    b.Toggle("audio.passthrough", L"Send surround sound to the receiver", L"Передавать объёмный звук на ресивер", false)
        .Desc(L"Pass Dolby and DTS tracks undecoded over HDMI or S/PDIF and let the receiver decode them. Volume, the "
              L"equalizer and effects do not apply to these tracks.",
              L"Передавать дорожки Dolby и DTS по HDMI или S/PDIF без декодирования, их декодирует ресивер. "
              L"Громкость, эквалайзер и эффекты к таким дорожкам не применяются.")
        .MpvBuild("audio-spdif", BuildSpdif, "");
    for (auto [key, en, def] : {std::tuple{"audio.spdifAc3", L"Dolby Digital (AC3)", true},
                                std::tuple{"audio.spdifEac3", L"Dolby Digital Plus (E-AC3)", false},
                                std::tuple{"audio.spdifDts", L"DTS", true},
                                std::tuple{"audio.spdifDtsHd", L"DTS-HD", false},
                                std::tuple{"audio.spdifTrueHd", L"Dolby TrueHD", false}}) {
        b.Toggle(key, en, en, def)
            .Desc(L"Turn on only the formats your receiver decodes; S/PDIF carries AC3 and DTS only.",
                  L"Включайте только форматы, которые декодирует ресивер; по S/PDIF передаются только AC3 и DTS.")
            .When("audio.passthrough", "true")
            .MpvBuild("audio-spdif", BuildSpdif, "");
    }
    b.inferred = false;

    b.Sec("volume", L"Volume", L"Громкость");
    b.Slider("audio.maxVolume", L"Maximum volume", L"Максимальная громкость", 100, 200, 25, 100)
        .Unit(L"%", L"%")
        .Desc(L"Allow the volume above 100% for quiet files. Loud parts may distort.",
              L"Разрешить громкость выше 100% для тихих файлов. Громкие места могут искажаться.")
        .Mpv("volume-max");
    b.Pick("audio.startupVolumeMode", L"Startup volume", L"Громкость при запуске",
           {{"remember", L"Remember last volume", L"Запоминать последнюю"},
            {"fixed", L"Always starts at:", L"Всегда начинать с:"}},
           "remember")
        .Group("startupVolume")
        .Desc(L"Volume to use when Lumen starts.", L"С какой громкостью запускается Lumen.")
        .MpvBuild("volume", BuildStartupVolume)
        .Startup();
    b.Number("audio.startupVolume", L"Always starts at:", L"Всегда начинать с:", 0, 200, 1, 100)
        .Unit(L"%", L"%")
        .Group("startupVolume")
        .When("audio.startupVolumeMode", "fixed")
        .MpvBuild("volume", BuildStartupVolume)
        .Startup();
    b.Number("audio.lastVolume", L"Last volume", L"Последняя громкость", 0, 200, 1, 100)
        .Hidden()
        .MpvBuild("volume", BuildStartupVolume)
        .Startup();
    b.Number("audio.volumeStep", L"Volume step", L"Шаг громкости", 1, 25, 1, 5)
        .Unit(L"%", L"%")
        .Desc(L"How much the wheel and the arrow keys change the volume.",
              L"На сколько меняют громкость колесо мыши и стрелки.");

    b.Sec("loudness", L"Loudness", L"Выравнивание громкости");
    b.Toggle("audio.normalize", L"Normalize loudness", L"Выравнивать громкость", false)
        .Desc(L"Keep quiet and loud files at a similar level. Works together with the equalizer.",
              L"Держать тихие и громкие файлы на одном уровне. Работает вместе с эквалайзером.")
        .MpvBuild("af", BuildAf);
    b.Seg("audio.normalizeMode", L"Normalization", L"Способ выравнивания",
          {{"ebu", L"EBU R128", L"EBU R128"}, {"dynamic", L"Dynamic", L"Динамический"}}, "ebu")
        .Inferred()
        .Desc(L"EBU R128 aims at a target loudness. Dynamic also lifts quiet passages within a file, like "
              L"dialogue between loud scenes.",
              L"EBU R128 приводит звук к целевой громкости. Динамический ещё и поднимает тихие места внутри файла, "
              L"например диалоги между громкими сценами.")
        .When("audio.normalize", "true")
        .MpvBuild("af", BuildAf);
    b.Number("audio.targetLevel", L"Target level", L"Целевой уровень", -40, -5, 1, -23)
        .Unit(L"LUFS", L"LUFS")
        .Desc(L"Loudness to aim for in EBU R128 mode. -23 is the broadcast standard, -16 is louder.",
              L"К какой громкости стремиться в режиме EBU R128. -23 — телевизионный стандарт, -16 — громче.")
        .When("audio.normalize", "true")
        .MpvBuild("af", BuildAf);
    b.Toggle("audio.nightMode", L"Night mode", L"Ночной режим", false)
        .Desc(L"Make explosions quieter and dialogue louder, so you can watch at low volume.",
              L"Делает взрывы тише, а диалоги громче, чтобы можно было смотреть на малой громкости.")
        .MpvBuild("af", BuildAf);
    b.Pick("audio.replayGain", L"Replay gain", L"Replay Gain",
           {{"off", L"Off", L"Выкл.", "no"}, {"track", L"Track", L"По треку", "track"}, {"album", L"Album", L"По альбому", "album"}},
           "off")
        .Desc(L"Use loudness tags stored in music files. Album keeps the volume steps between songs of an album.",
              L"Использовать теги громкости из музыкальных файлов. «По альбому» сохраняет разницу между песнями альбома.")
        .Mpv("replaygain");
    b.Number("audio.replayGainPreamp", L"Replay gain preamp", L"Предусиление Replay Gain", -15, 15, 0.1, 0)
        .Unit(L"dB", L"дБ")
        .Desc(L"Extra gain added on top of replay gain.", L"Дополнительное усиление поверх Replay Gain.")
        .When("audio.replayGain", "off", true)
        .Mpv("replaygain-preamp");

    b.Sec("effects", L"Effects", L"Эффекты");
    b.Toggle("audio.eq", L"Equalizer", L"Эквалайзер", false)
        .Desc(L"Adjust the sound with a 10-band equalizer.", L"Настройка звука 10-полосным эквалайзером.")
        .MpvBuild("af", BuildAf);
    {
        std::vector<Choice> presets = {{"none", L"None", L"Нет"}};
        const std::tuple<const char*, const wchar_t*, const wchar_t*, const char*> kPresetNames[] = {
            {"flat", L"Flat", L"Ровный", ""},
            {"classical", L"Classical", L"Классика", "eq_eq-classical"},
            {"club", L"Club", L"Клуб", "eq_eq-club"},
            {"dance", L"Dance", L"Танцевальный", "eq_eq-dance"},
            {"fullbass", L"Full bass", L"Мощный бас", "eq_eq-full-bass"},
            {"fullbasstreble", L"Full bass & treble", L"Бас и высокие", "eq_eq-full-bass-and-treble"},
            {"fulltreble", L"Full treble", L"Высокие частоты", "eq_eq-full-treble"},
            {"headphones", L"Headphones", L"Наушники", "eq_eq-headphones"},
            {"largehall", L"Large hall", L"Большой зал", "eq_eq-large-hall"},
            {"live", L"Live", L"Концерт", "eq_eq-live"},
            {"party", L"Party", L"Вечеринка", "eq_eq-party"},
            {"pop", L"Pop", L"Поп", "eq_eq-pop"},
            {"reggae", L"Reggae", L"Регги", "eq_eq-reggae"},
            {"rock", L"Rock", L"Рок", "eq_eq-rock"},
            {"ska", L"Ska", L"Ска", "eq_eq-ska"},
            {"soft", L"Soft", L"Мягкий", "eq_eq-soft"},
            {"softrock", L"Soft rock", L"Софт-рок", "eq_eq-soft-rock"},
            {"techno", L"Techno", L"Техно", "eq_eq-techno"},
        };
        for (auto& [id, en, ru, icon] : kPresetNames) presets.push_back({id, en, ru, "", icon});
        b.Pick("audio.eqPreset", L"Equalizer preset", L"Предустановка эквалайзера", std::move(presets), "none")
            .Desc(L"Load band values from a preset. Moving any band afterwards keeps your changes.",
                  L"Загрузить значения полос из предустановки. Дальше полосы можно двигать вручную.")
            .When("audio.eq", "true");
    }
    b.Slider(kEqPreampKey, L"Preamp", L"Предусиление", -20, 20, 0.1, 0)
        .Unit(L"dB", L"дБ")
        .Group("equalizer")
        .When("audio.eq", "true")
        .MpvBuild("af", BuildAf);
    {
        const wchar_t* kBandEn[kEqBands] = {L"60 Hz", L"170 Hz", L"310 Hz", L"600 Hz", L"1 kHz",
                                            L"3 kHz", L"6 kHz",  L"12 kHz", L"14 kHz", L"16 kHz"};
        const wchar_t* kBandRu[kEqBands] = {L"60 Гц", L"170 Гц", L"310 Гц", L"600 Гц", L"1 кГц",
                                            L"3 кГц", L"6 кГц",  L"12 кГц", L"14 кГц", L"16 кГц"};
        for (int i = 0; i < kEqBands; ++i) {
            b.Slider(kEqBandKeys[i], kBandEn[i], kBandRu[i], -20, 20, 0.1, 0)
                .Unit(L"dB", L"дБ")
                .Group("equalizer")
                .When("audio.eq", "true")
                .MpvBuild("af", BuildAf);
        }
    }
    b.Toggle("audio.eqTwoPass", L"Two-pass", L"Двойной проход", false)
        .Desc(L"Apply the equalizer twice for a stronger effect.", L"Применять эквалайзер дважды для более сильного эффекта.")
        .When("audio.eq", "true")
        .MpvBuild("af", BuildAf);
    b.Toggle("audio.spatial", L"Spatial audio", L"Объёмный звук", false)
        .Desc(L"Make stereo sound wider and more natural, especially on headphones.",
              L"Делает стереозвук шире и естественнее, особенно в наушниках.")
        .MpvBuild("af", BuildAf);
    b.Pick("audio.spatialMode", L"Spatial audio mode", L"Режим объёмного звука",
           {{"headphones", L"Headphone virtualization", L"Виртуализация для наушников"},
            {"room", L"Room simulation", L"Эффект помещения"}},
           "headphones")
        .Desc(L"Headphone virtualization mixes channels like speakers do; room simulation adds reflections.",
              L"Виртуализация смешивает каналы, как это происходит с колонками; эффект помещения добавляет отражения.")
        .When("audio.spatial", "true")
        .MpvBuild("af", BuildAf);
    b.Toggle("audio.stereoWiden", L"Stereo widening", L"Расширение стереобазы", false)
        .Desc(L"Push the stereo image beyond the speakers.", L"Раздвигает стереопанораму шире колонок.")
        .MpvBuild("af", BuildAf);

    // Effect details from components/Audio-Compressor, -Spatializer and -Stereo_Widener.
    b.advanced = true;
    b.inferred = true;
    struct Param {
        const char* key;
        const wchar_t *en, *ru;
        double mn, mx, step, def;
        const wchar_t *unit, *unitRu;
    };
    const Param kCompressor[] = {
        {"audio.compRmsPeak", L"RMS / Peak", L"RMS / пик", 0, 1, 0.1, 0.2, L"", L""},
        {"audio.compAttack", L"Attack", L"Атака", 1.5, 400, 0.5, 25, L"ms", L"мс"},
        {"audio.compRelease", L"Release", L"Восстановление", 2, 800, 1, 100, L"ms", L"мс"},
        {"audio.compThreshold", L"Threshold", L"Порог", -30, 0, 0.5, -11, L"dB", L"дБ"},
        {"audio.compRatio", L"Ratio", L"Степень сжатия", 1, 20, 0.5, 4, L": 1", L": 1"},
        {"audio.compKnee", L"Knee radius", L"Радиус колена", 1, 10, 0.5, 5, L"dB", L"дБ"},
        {"audio.compMakeup", L"Makeup gain", L"Компенсация усиления", 0, 24, 0.5, 7, L"dB", L"дБ"},
    };
    for (const Param& p : kCompressor)
        b.Slider(p.key, p.en, p.ru, p.mn, p.mx, p.step, p.def)
            .Unit(p.unit, p.unitRu)
            .Group("compressor")
            .When("audio.nightMode", "true")
            .MpvBuild("af", BuildAf);
    const Param kSpatializer[] = {
        {"audio.spatialSize", L"Size", L"Размер", 0, 10, 0.1, 8, L"", L""},
        {"audio.spatialWidth", L"Width", L"Ширина", 0, 10, 0.1, 10, L"", L""},
        {"audio.spatialWet", L"Wet", L"Обработанный", 0, 10, 0.1, 4, L"", L""},
        {"audio.spatialDry", L"Dry", L"Исходный", 0, 10, 0.1, 5, L"", L""},
        {"audio.spatialDamp", L"Damp", L"Затухание", 0, 10, 0.1, 5, L"", L""},
    };
    for (const Param& p : kSpatializer)
        b.Slider(p.key, p.en, p.ru, p.mn, p.mx, p.step, p.def)
            .Group("spatializer")
            .When("audio.spatialMode", "room")
            .MpvBuild("af", BuildAf);
    const Param kWidener[] = {
        {"audio.widenDelay", L"Delay time", L"Задержка", 1, 100, 0.5, 20, L"ms", L"мс"},
        {"audio.widenFeedback", L"Feedback gain", L"Обратная связь", 0, 0.9, 0.05, 0.3, L"", L""},
        {"audio.widenCrossfeed", L"Crossfeed", L"Перекрёстное смешивание", 0, 0.8, 0.05, 0.3, L"", L""},
        {"audio.widenDryMix", L"Dry mix", L"Доля исходного звука", 0, 1, 0.05, 0.8, L"", L""},
    };
    for (const Param& p : kWidener)
        b.Slider(p.key, p.en, p.ru, p.mn, p.mx, p.step, p.def)
            .Unit(p.unit, p.unitRu)
            .Group("stereoWidener")
            .When("audio.stereoWiden", "true")
            .MpvBuild("af", BuildAf);
    b.advanced = false;
    b.Pick("audio.visualizer", L"Visualization", L"Визуализация",
           {{"disabled", L"Disabled", L"Отключена", "", "close"},
            {"spectrometer", L"Spectrometer", L"Спектрометр", "", "spectometer_twotone"},
            {"scope", L"Scope", L"Осциллограф", "", "scope_twotone"},
            {"spectrum", L"Spectrum", L"Спектр", "", "spectrum_twotone"},
            {"waveform", L"Waveform", L"Волна", "", "waveform_twotone"}},
           "disabled")
        .Desc(L"Animation shown instead of the cover while music plays (components/Audio_Visualizer).",
              L"Анимация вместо обложки во время воспроизведения музыки.")
        .Hidden();  // not implemented yet
    b.Toggle("audio.pitchCorrection", L"Keep pitch when changing speed", L"Сохранять тон при смене скорости", true)
        .MpvBool("audio-pitch-correction");

    b.Sec("languages", L"Languages", L"Языки");
    b.Pick("audio.language", L"Preferred audio language", L"Предпочтительный язык звука", LanguageChoices(true),
           "system")
        .Desc(L"Pick the audio track in this language when a file has several.",
              L"Если в файле несколько дорожек, выбирать дорожку на этом языке.")
        .MpvBuild("alang", BuildAlang);
    b.Toggle("audio.autoLoadFiles", L"Load audio tracks found next to the video",
             L"Подключать звуковые дорожки рядом с видео", true)
        .Inferred()
        .Desc(L"Add audio files whose name starts like the video's, e.g. \"Movie.en.mka\" for \"Movie.mkv\", as "
              L"extra audio tracks.",
              L"Добавлять звуковые файлы, имя которых начинается как у видео, например «Фильм.ru.mka» для «Фильм.mkv», "
              L"как дополнительные дорожки.")
        .MpvBool("audio-file-auto", "fuzzy", "no");

    // ===== Subtitles =====
    b.Cat("subtitles", "media", "subtitles", L"Subtitles", L"Субтитры", L"Subtitle Defaults", L"Субтитры по умолчанию");

    b.Sec("tracks", L"Tracks & Languages", L"Дорожки и языки");
    b.Toggle("subtitles.enabled", L"Enable subtitles", L"Показывать субтитры", true)
        .Desc(L"Show subtitles when a file has them. You can still turn them on from the player.",
              L"Показывать субтитры, если они есть. Их всегда можно включить в плеере.")
        .MpvBuild("sid", BuildSid, "auto");
    b.Pick("subtitles.language", L"Preferred language", L"Предпочтительный язык", LanguageChoices(false), "system")
        .Desc(L"Pick the subtitle track in this language when there are several.",
              L"Если субтитров несколько, выбирать дорожку на этом языке.")
        .MpvBuild("slang", BuildSlang);
    b.Pick("subtitles.encoding", L"Text encoding", L"Кодировка текста",
           {{"auto", L"Automatic", L"Автоматически", "auto"},
            {"utf-8", L"Unicode (UTF-8)", L"Юникод (UTF-8)", "utf-8"},
            {"cp1251", L"Cyrillic (Windows-1251)", L"Кириллица (Windows-1251)", "cp1251"},
            {"koi8-r", L"Cyrillic (KOI8-R)", L"Кириллица (KOI8-R)", "koi8-r"},
            {"cp866", L"Cyrillic (DOS 866)", L"Кириллица (DOS 866)", "cp866"},
            {"cp1252", L"Western (Windows-1252)", L"Западноевропейская (Windows-1252)", "cp1252"},
            {"cp1250", L"Central European (Windows-1250)", L"Центральноевропейская (Windows-1250)", "cp1250"},
            {"cp1253", L"Greek (Windows-1253)", L"Греческая (Windows-1253)", "cp1253"},
            {"cp1254", L"Turkish (Windows-1254)", L"Турецкая (Windows-1254)", "cp1254"},
            {"cp1256", L"Arabic (Windows-1256)", L"Арабская (Windows-1256)", "cp1256"},
            {"gbk", L"Chinese Simplified (GBK)", L"Китайская упрощённая (GBK)", "gbk"},
            {"big5", L"Chinese Traditional (Big5)", L"Китайская традиционная (Big5)", "big5"},
            {"shift_jis", L"Japanese (Shift JIS)", L"Японская (Shift JIS)", "shift_jis"},
            {"euc-kr", L"Korean (EUC-KR)", L"Корейская (EUC-KR)", "euc-kr"}},
           "auto")
        .Desc(L"Character set of text subtitle files. Change it if letters look garbled.",
              L"Кодировка текстовых файлов субтитров. Смените, если вместо букв «кракозябры».")
        .Mpv("sub-codepage");

    b.Sec("loading", L"Loading Subtitles", L"Загрузка субтитров");
    b.Toggle("subtitles.autoLoad", L"Load subtitle files automatically", L"Подключать файлы субтитров автоматически", true)
        .Desc(L"Load .srt, .ass and other subtitle files found next to the video.",
              L"Подключать файлы .srt, .ass и другие, лежащие рядом с видео.")
        .MpvBuild("sub-auto", BuildSubAuto, "exact");
    b.Pick("subtitles.matching", L"File name matching", L"Сопоставление имён",
           {{"exact", L"Exact title", L"Точное совпадение"},
            {"fuzzy", L"Title with extra characters", L"Название с дополнительными символами"},
            {"all", L"Any subtitle in the folder", L"Любые субтитры в папке"}},
           "fuzzy")
        .Desc(L"How closely a subtitle file name must match the video name, e.g. \"Movie.en.srt\" for \"Movie.mkv\".",
              L"Насколько имя файла субтитров должно совпадать с именем видео, например «Фильм.ru.srt» для «Фильм.mkv».")
        .When("subtitles.autoLoad", "true")
        .MpvBuild("sub-auto", BuildSubAuto, "exact");
    b.inferred = true;
    b.advanced = true;
    b.Text("subtitles.searchPaths", L"Also look in subfolders", L"Искать также в папках", "subs;subtitles;Subs")
        .Desc(L"Folders next to the video that may hold its subtitles, separated by ';'.",
              L"Папки рядом с видео, где могут лежать субтитры, через «;».")
        .When("subtitles.autoLoad", "true")
        .Mpv("sub-file-paths");
    b.advanced = false;
    b.inferred = false;

    b.Sec("appearance", L"Appearance", L"Оформление");
    b.Text("subtitles.font", L"Font", L"Шрифт")
        .Hint(L"System, sans-serif", L"Системный, без засечек")
        .Desc(L"Font for text subtitles. Leave empty for the default sans-serif font.",
              L"Шрифт текстовых субтитров. Если пусто — стандартный шрифт без засечек.")
        .Mpv("sub-font")
        .Reset("sans-serif");
    b.Seg("subtitles.sizeMode", L"Size values", L"Размер",
          {{"relative", L"Relative", L"Относительный", "yes"}, {"pixels", L"Pixels", L"В пикселях", "no"}}, "relative")
        .Desc(L"Relative sizes follow the window; pixel sizes stay the same at any window size.",
              L"Относительный размер меняется вместе с окном, в пикселях — остаётся постоянным.")
        .Mpv("sub-scale-with-window");
    b.Pick("subtitles.size", L"Font size", L"Размер шрифта",
           {{"auto", L"Automatic", L"Автоматически", ""},
            {"smaller", L"Smaller", L"Мельче", "28"},
            {"small", L"Small", L"Мелкий", "33"},
            {"large", L"Large", L"Крупный", "46"},
            {"larger", L"Larger", L"Крупнее", "54"},
            {"huge", L"Huge", L"Огромный", "64"}},
           "auto")
        .Mpv("sub-font-size")
        .Reset("38");
    b.Toggle("subtitles.bold", L"Bold", L"Жирный", false).MpvBool("sub-bold");
    b.Color("subtitles.color", L"Text color", L"Цвет текста", "#FFFFFF").MpvBuild("sub-color", BuildSubColor);
    b.Slider("subtitles.opacity", L"Text opacity", L"Непрозрачность текста", 0, 100, 5, 100)
        .Unit(L"%", L"%")
        .MpvBuild("sub-color", BuildSubColor);
    b.Pick("subtitles.outline", L"Outline thickness", L"Толщина обводки",
           {{"none", L"None", L"Нет", "0"},
            {"thin", L"Thin", L"Тонкая", "1.5"},
            {"normal", L"Normal", L"Обычная", "3"},
            {"thick", L"Thick", L"Толстая", "5"}},
           "normal")
        .Mpv("sub-outline-size");
    b.Color("subtitles.outlineColor", L"Outline color", L"Цвет обводки", "#000000").Mpv("sub-outline-color");
    b.Toggle("subtitles.shadow", L"Shadow", L"Тень", true).MpvBool("sub-shadow-offset", "2", "0");
    b.inferred = true;
    b.Pick("subtitles.background", L"Background", L"Подложка",
           {{"none", L"None", L"Нет", "outline-and-shadow"},
            {"lines", L"Box behind each line", L"Плашка под каждой строкой", "opaque-box"},
            {"box", L"Box behind the text", L"Плашка под всем текстом", "background-box"}},
           "none")
        .Desc(L"Draw text on a box, like TV captions; easier to read on bright scenes. Each-line boxes use the "
              L"outline color.",
              L"Рисовать текст на плашке, как телевизионные титры: так его легче читать на светлом фоне. Плашки "
              L"под строками берут цвет обводки.")
        .Mpv("sub-border-style");
    b.Color("subtitles.backColor", L"Box color", L"Цвет плашки", "#000000")
        .When("subtitles.background", "box")
        .MpvBuild("sub-back-color", BuildSubBackColor);
    b.Slider("subtitles.backOpacity", L"Box opacity", L"Непрозрачность плашки", 0, 100, 5, 60)
        .Unit(L"%", L"%")
        .When("subtitles.background", "box")
        .MpvBuild("sub-back-color", BuildSubBackColor);
    b.Slider("subtitles.blur", L"Edge softness", L"Размытие краёв", 0, 20, 0.5, 0)
        .Desc(L"Blur the edges of the text and its outline. 0 keeps them sharp.",
              L"Размывать края текста и обводки. 0 — чёткие края.")
        .Mpv("sub-blur");
    b.Slider("subtitles.position", L"Vertical position", L"Положение по вертикали", 0, 100, 1, 100)
        .Unit(L"%", L"%")
        .Desc(L"100% puts subtitles at the bottom edge, lower values move them up.",
              L"100% — у нижнего края, меньшие значения поднимают субтитры выше.")
        .Mpv("sub-pos");
    b.Slider("subtitles.marginBottom", L"Distance from the bottom", L"Отступ снизу", 0, 150, 1, 22)
        .Unit(L"px", L"пкс")
        .Desc(L"Gap between subtitles and the bottom edge, in pixels of a 720p picture.",
              L"Расстояние от субтитров до нижнего края в пикселях кадра 720p.")
        .Mpv("sub-margin-y");
    b.Toggle("subtitles.useBlackBars", L"Show subtitles in the black bars", L"Субтитры в чёрных полосах", true)
        .Desc(L"When the video has black bars around it on screen, put subtitles there instead of over the picture.",
              L"Если на экране вокруг видео есть чёрные полосы, показывать субтитры в них, а не поверх картинки.")
        .MpvBool("sub-use-margins")
        .MpvBool("sub-ass-force-margins");
    b.Slider("subtitles.secondaryPosition", L"Secondary subtitles position", L"Положение вторых субтитров", 0, 100,
             1, 0)
        .Unit(L"%", L"%")
        .Desc(L"Where the second subtitle track (chosen in the player) appears. 0% is the top edge.",
              L"Где показывать вторые субтитры (выбираются в плеере). 0% — у верхнего края.")
        .Mpv("secondary-sub-pos");
    b.Toggle("subtitles.assOverride", L"Apply this style to styled subtitles", L"Применять стиль к оформленным субтитрам",
             false)
        .Desc(L"Override fonts and colors of .ass subtitles. May break karaoke and signs.",
              L"Заменять шрифты и цвета субтитров .ass. Может испортить караоке и надписи.")
        .MpvBool("sub-ass-override", "force", "scale");

    // ===== Library =====
    b.Cat("library", "media", "folder", L"Library", L"Медиатека", L"Playlists, Folders & Files",
          L"Плейлисты, папки и файлы");

    b.Sec("opening", L"Opening Files", L"Открытие файлов");
    b.Toggle("library.singleInstance", L"Only allow one Lumen window", L"Только одно окно Lumen", true)
        .Desc(L"Open new files in the window that is already running.",
              L"Открывать новые файлы в уже запущенном окне.");
    b.Toggle("library.singleInstanceFileManager", L"Apply to files opened from the file manager",
             L"Для файлов из Проводника", true)
        .Desc(L"Also reuse the window for files opened from Explorer.",
              L"Использовать то же окно и для файлов, открытых из Проводника.")
        .When("library.singleInstance", "true")
        .Hidden();  // not implemented yet: Explorer launches look like any other
    b.Toggle("library.enqueue", L"Add to the playlist instead of replacing it", L"Добавлять в плейлист, а не заменять",
             false)
        .Desc(L"Files opened in a running window are appended to the playlist.",
              L"Файлы, открытые в запущенном окне, добавляются в конец плейлиста.")
        .When("library.singleInstance", "true");
    b.Pick("library.openFolder", L"When opening a folder", L"При открытии папки",
           {{"subfolders", L"Include subfolders", L"Вместе с вложенными папками"},
            {"folder", L"This folder only", L"Только эта папка"}},
           "subfolders")
        .Desc(L"Which files go to the playlist when you open or drop a folder.",
              L"Какие файлы попадают в плейлист, когда вы открываете или перетаскиваете папку.");
    b.Toggle("library.showHidden", L"Show hidden files", L"Показывать скрытые файлы", false)
        .Desc(L"Include hidden and system files in the library and folder playlists.",
              L"Включать скрытые и системные файлы в медиатеку и плейлисты папок.");

    b.Sec("metadata", L"Metadata & Artwork", L"Метаданные и обложки");
    b.Pick("library.metadata", L"Fetch titles and artwork", L"Названия и обложки",
           {{"file", L"From the file only", L"Только из файла"},
            {"internet", L"From the file and the internet", L"Из файла и из интернета"},
            {"none", L"Don't fetch", L"Не загружать"}},
           "file")
        .Desc(L"Where titles, artists and covers come from. Internet lookups follow the Privacy settings.",
              L"Откуда брать названия, исполнителей и обложки. Поиск в интернете подчиняется настройкам "
              L"конфиденциальности.");
    b.Toggle("library.enabled", L"Use the media library", L"Использовать медиатеку", true)
        .Desc(L"Index your media folders for the Home, Video and Music pages.",
              L"Индексировать папки с медиа для разделов «Главная», «Видео» и «Музыка».")
        .Hidden();  // not implemented yet

    b.advanced = true;
    b.inferred = true;
    b.Toggle("library.scanOnStartup", L"Scan library folders on startup", L"Сканировать папки при запуске", true);
    b.Toggle("library.readTags", L"Read duration and tags", L"Читать длительность и теги", true)
        .Desc(L"Read duration, artist and album from files. Slower on large network folders.",
              L"Читать длительность, исполнителя и альбом из файлов. На больших сетевых папках медленнее.");
    b.Toggle("library.thumbnails", L"Show thumbnails", L"Показывать миниатюры", true)
        .Desc(L"Use Windows thumbnails for videos and embedded covers for music.",
              L"Брать миниатюры видео из Windows, а обложки — из музыкальных файлов.");
}

void BuildSystem(Builder& b) {
    // ===== Network =====
    b.Cat("network", "system", "streaming", L"Network", L"Сеть", L"Network Connections & Security",
          L"Сеть и безопасность");

    b.Sec("buffering", L"Buffering", L"Буферизация");
    b.Pick("network.buffering", L"Buffering", L"Буферизация",
           {{"low", L"Low", L"Низкая"}, {"normal", L"Normal", L"Обычная"}, {"high", L"High", L"Высокая"},
            {"custom", L"Custom", L"Своя"}},
           "normal")
        .Desc(L"How much of a stream to load ahead. More buffering survives a slow connection but starts later.",
              L"Сколько потока загружать заранее. Большой буфер переживает медленную сеть, но старт дольше.")
        .MpvBuild("cache-secs", BuildCacheSecs)
        .MpvBuild("demuxer-max-bytes", BuildDemuxerMaxBytes);
    b.Number("network.cacheLocal", L"Local files", L"Локальные файлы", 0, 60000, 50, 300)
        .Unit(L"ms", L"мс")
        .Desc(L"How far ahead to read files on this computer. Less uses less memory; 1000 is mpv's default.",
              L"Насколько вперёд читать файлы с этого компьютера. Меньше — меньше памяти; 1000 — как в mpv.")
        .When("network.buffering", "custom")
        .MpvBuild("demuxer-readahead-secs", BuildReadahead, "1");
    b.Number("network.cacheDisc", L"Discs", L"Диски", 0, 60000, 50, 300)
        .Unit(L"ms", L"мс")
        .When("network.buffering", "custom")
        .Hidden();  // not implemented yet
    b.Number("network.cacheNetwork", L"Network streams", L"Сетевые потоки", 1000, 3600000, 500, 60000)
        .Unit(L"ms", L"мс")
        .When("network.buffering", "custom")
        .MpvBuild("cache-secs", BuildCacheSecs);
    b.Number("network.cacheLive", L"Live capture", L"Захват с устройств", 0, 60000, 50, 300)
        .Unit(L"ms", L"мс")
        .When("network.buffering", "custom")
        .Hidden();  // not implemented yet

    b.Sec("proxy", L"Proxy", L"Прокси");
    b.Text("network.httpProxy", L"HTTP proxy", L"HTTP-прокси")
        .Hint(L"System proxy", L"Системный прокси")
        .Desc(L"Address like http://user@host:8080. Leave empty to connect directly.",
              L"Адрес вида http://user@host:8080. Если пусто — подключение напрямую.")
        .MpvBuild("http-proxy", BuildHttpProxy);
    b.Text("network.httpProxyPassword", L"HTTP proxy password", L"Пароль HTTP-прокси")
        .Secret()
        .Desc(L"Password for the user name given in the proxy address.",
              L"Пароль для имени пользователя, указанного в адресе прокси.")
        .MpvBuild("http-proxy", BuildHttpProxy);

    b.Sec("security", L"Security", L"Безопасность");
    b.Toggle("network.verifyCerts", L"Verify certificates against the system trust store",
             L"Проверять сертификаты по хранилищу Windows", true)
        .Desc(L"Refuse HTTPS streams whose certificate Windows does not trust.",
              L"Не открывать HTTPS-потоки с сертификатом, которому не доверяет Windows.")
        .MpvBool("tls-verify");
    b.Pick("network.cipher", L"Cipher preference", L"Предпочтение шифров",
           {{"balanced", L"Balanced", L"Сбалансированное"},
            {"secure", L"Most secure", L"Максимальная защита"},
            {"compatible", L"Most compatible", L"Максимальная совместимость"}},
           "balanced")
        .Desc(L"Which TLS ciphers to offer. Most compatible also allows older servers.",
              L"Какие шифры TLS предлагать. «Совместимость» подходит и для старых серверов.")
        .Hidden();  // not implemented yet

    b.advanced = true;
    b.inferred = true;
    b.Number("network.cacheSize", L"Maximum cache size", L"Максимальный размер кэша", 16, 4096, 1, 150)
        .Unit(L"MB", L"МБ")
        .When("network.buffering", "custom")
        .MpvBuild("demuxer-max-bytes", BuildDemuxerMaxBytes);
    b.Number("network.timeout", L"Connection timeout", L"Тайм-аут подключения", 1, 600, 1, 60)
        .Unit(L"s", L"с")
        .Mpv("network-timeout");
    b.Text("network.userAgent", L"User agent", L"User agent")
        .Hint(L"libmpv", L"libmpv")
        .Mpv("user-agent")
        .Reset("libmpv");
    {
        const std::wstring& ytdlp = YtDlpPath();
        b.Toggle("network.ytdl", L"Open web pages with yt-dlp", L"Открывать веб-страницы через yt-dlp", true)
            .Desc(ytdlp.empty() ? W(L"Play YouTube and similar links. Requires yt-dlp.exe next to Lumen or on PATH.")
                                : L"Play YouTube and similar links with " + ytdlp,
                  ytdlp.empty() ? W(L"Воспроизводить ссылки YouTube и похожих сайтов. Нужен yt-dlp.exe рядом с Lumen "
                                    L"или в PATH.")
                                : L"Воспроизводить ссылки YouTube и похожих сайтов через " + ytdlp)
            .MpvBuild("ytdl", BuildYtdl, "no")
            .MpvBuild("script-opts", BuildScriptOpts);
        // Lumen never downloads yt-dlp; without it, say where to put it.
        ItemRef missing = b.Info("network.ytdlMissing", L"For YouTube links, put yt-dlp.exe next to Lumen.exe",
                                 L"Для ссылок YouTube положите yt-dlp.exe рядом с Lumen.exe", "hint")
                              .Desc(L"yt-dlp.exe was not found next to Lumen.exe or on PATH. Download it from the "
                                    L"yt-dlp project yourself, put it there and restart Lumen.",
                                    L"yt-dlp.exe не найден ни рядом с Lumen.exe, ни в PATH. Скачайте его с сайта "
                                    L"проекта yt-dlp, положите туда и перезапустите Lumen.")
                              .When("network.ytdl", "true");
        if (!ytdlp.empty()) missing.Hidden();
    }

    // ===== Performance =====
    b.Cat("performance", "system", "preferences", L"Performance", L"Производительность", L"Performance Settings",
          L"Производительность");

    b.Sec("decoding", L"Decoding", L"Декодирование");
    b.Pick("performance.hwdec", L"Hardware-accelerated decoding", L"Аппаратное декодирование",
           // Not auto-safe: after d3d11va fails (4:4:4 or 10-bit H.264) it creates a CUDA context
           // for nvdec (and dxva2 a D3D9 device), about 250 MB, and fails on the same formats.
           {{"auto", L"Automatic", L"Автоматически", kAutoHwdec},
            {"d3d11", L"Direct3D 11", L"Direct3D 11", "d3d11va"},
            {"d3d11copy", L"Direct3D 11 (copy back)", L"Direct3D 11 (с копированием)", "d3d11va-copy"},
            {"nvdec", L"NVIDIA NVDEC", L"NVIDIA NVDEC", "nvdec"},
            {"dxva2", L"DXVA2 (copy back)", L"DXVA2 (с копированием)", "dxva2-copy"},
            {"off", L"Off", L"Выкл.", "no"}},
           "auto")
        .Desc(L"Decode video on the graphics card to save power. Turn off if you see artifacts.",
              L"Декодировать видео видеокартой, чтобы экономить энергию. Выключите, если видите артефакты.")
        .Mpv("hwdec");
    b.Pick("performance.postprocessing", L"Video post-processing", L"Постобработка видео",
           {{"off", L"Off", L"Выкл."}, {"low", L"Low", L"Слабая"}, {"medium", L"Medium", L"Средняя"},
            {"high", L"High", L"Сильная"}},
           "off")
        .Desc(L"Smooth blocky artifacts in low-bitrate video. Uses the CPU.",
              L"Сглаживать блочные артефакты в видео с низким битрейтом. Нагружает процессор.")
        .MpvBuild("vf", BuildVf);
    b.Pick("performance.skipLoopFilter", L"Skip the H.264 loop filter", L"Пропускать фильтр H.264",
           {{"never", L"Never", L"Никогда", "default"},
            {"nonref", L"Non-reference frames", L"Неопорные кадры", "nonref"},
            {"bidir", L"B-frames", L"B-кадры", "bidir"},
            {"nonkey", L"All but keyframes", L"Все, кроме ключевых", "nonkey"},
            {"all", L"All frames", L"Все кадры", "all"}},
           "never")
        .Desc(L"Speeds up software decoding on slow computers at the cost of picture quality.",
              L"Ускоряет программное декодирование на слабых компьютерах ценой качества картинки.")
        .Mpv("vd-lavc-skiploopfilter");
    b.Number("performance.decodeThreads", L"Decoding threads", L"Потоки декодирования", 0, 64, 1, 0)
        .Desc(L"CPU threads for software decoding. 0 picks automatically.",
              L"Потоки процессора для программного декодирования. 0 — автоматически.")
        .MpvBuild("vd-lavc-threads", BuildDecodeThreads);

    b.Sec("smooth", L"Smooth Playback", L"Плавное воспроизведение");
    b.Toggle("performance.frameDrop", L"Drop frames when the system can't keep up", L"Пропускать кадры при нехватке ресурсов",
             true)
        .MpvBool("framedrop", "vo", "no");
    b.Toggle("performance.realtimePriority", L"Give Lumen real-time priority", L"Приоритет реального времени для Lumen",
             false)
        .MpvBuild("priority", BuildPriority)
        .Startup();
    b.Number("performance.processPriority", L"Process priority", L"Приоритет процесса", -2, 2, 1, 0)
        .Desc(L"-2 lowest, 0 normal, 2 highest. Higher priority helps on busy systems.",
              L"-2 — самый низкий, 0 — обычный, 2 — самый высокий. Высокий приоритет помогает на загруженной системе.")
        .When("performance.realtimePriority", "false")
        .MpvBuild("priority", BuildPriority)
        .Startup();
    b.Button("performance.rescanPlugins", L"Rescan plugins", L"Пересканировать плагины", "rescan-plugins", L"Rescan",
             L"Пересканировать");

    b.Sec("developer", L"Developer", L"Для разработчиков");
    b.advanced = true;
    b.Pick("performance.encoder", L"Encoder", L"Кодировщик",
           {{"auto", L"Automatic", L"Автоматически"},
            {"x264", L"H.264 (x264)", L"H.264 (x264)"},
            {"x265", L"H.265 (x265)", L"H.265 (x265)"},
            {"nvenc", L"NVIDIA NVENC", L"NVIDIA NVENC"},
            {"qsv", L"Intel Quick Sync", L"Intel Quick Sync"},
            {"amf", L"AMD AMF", L"AMD AMF"}},
           "auto")
        .Desc(L"Video encoder for recording and streaming out.", L"Видеокодировщик для записи и трансляции.")
        .Hidden();  // not implemented yet
    b.Pick("performance.decoder", L"Decoder", L"Декодер",
           {{"auto", L"Automatic", L"Автоматически"}, {"software", L"Software only", L"Только программный"}}, "auto")
        .Desc(L"Software only ignores hardware decoding for troubleshooting.",
              L"«Только программный» отключает аппаратное декодирование для диагностики.")
        .Hidden();  // not implemented yet
    b.Pick("performance.clockSync", L"Clock synchronisation", L"Синхронизация",
           {{"default", L"Default", L"По умолчанию", "audio"},
            {"resample", L"Resample audio to the display", L"Подстройка звука под дисплей", "display-resample"},
            {"vdrop", L"Drop or repeat video frames", L"Пропуск и повтор кадров", "display-vdrop"}},
           "default")
        .Desc(L"Syncing to the display removes judder; the default syncs video to the audio clock. Video > Smooth "
              L"motion always resamples to the display.",
              L"Синхронизация с дисплеем убирает подёргивания; по умолчанию видео подстраивается под звук. «Видео > "
              L"Плавное движение» всегда подстраивает звук под дисплей.")
        .MpvBuild("video-sync", BuildVideoSync, "audio");
    b.Number("performance.clockJitter", L"Clock jitter", L"Допустимый джиттер", 0, 60000, 100, 5000)
        .Unit(L"ms", L"мс")
        .Desc(L"Largest clock deviation tolerated for live streams before resynchronizing.",
              L"Наибольшее отклонение часов в прямых трансляциях до пересинхронизации.")
        .Hidden();  // not implemented yet
    b.Number("performance.clockAveraging", L"Clock reference averaging", L"Усреднение опорных часов", 1, 1000, 1, 40)
        .Desc(L"Number of clock samples averaged for live streams.",
              L"Сколько отсчётов часов усреднять для прямых трансляций.")
        .Hidden();  // not implemented yet
    b.inferred = true;
    b.Pick("performance.vo", L"Video output", L"Вывод видео",
           {{"gpu-next", L"GPU (libplacebo)", L"GPU (libplacebo)", "gpu-next"}, {"gpu", L"GPU (legacy)", L"GPU (старый)", "gpu"}},
           "gpu-next")
        .Desc(L"Rendering backend. Applied at the next start.", L"Способ отрисовки. Применяется при следующем запуске.")
        .Mpv("vo")
        .Startup();
    b.Pick("performance.gpuApi", L"Graphics API", L"Графический API",
           {{"auto", L"Automatic", L"Автоматически", "auto"},
            {"d3d11", L"Direct3D 11", L"Direct3D 11", "d3d11"},
            {"vulkan", L"Vulkan", L"Vulkan", "vulkan"}},
           "auto")
        .Desc(L"Applied at the next start.", L"Применяется при следующем запуске.")
        .Mpv("gpu-api")
        .Startup();

    // ===== Recording =====
    b.Cat("recording", "system", "record", L"Recording", L"Запись", L"Recording, Casting & Streaming Out",
          L"Запись, трансляция и стриминг");

    b.Sec("recording", L"Recording", L"Запись");
    b.Path("recording.folder", L"Save recordings to", L"Папка для записей", PathKind::Folder)
        .Hint(L"Videos\\Lumen", L"Видео\\Lumen")
        .Desc(L"Folder for recorded streams and clips.", L"Папка для записанных потоков и фрагментов.");
    b.Toggle("recording.noReencode", L"Record without re-encoding when possible", L"Записывать без перекодирования", true)
        .Hidden();  // not implemented yet

    b.Sec("casting", L"Casting", L"Трансляция");
    b.Toggle("recording.discoverCast", L"Discover cast devices", L"Искать устройства для трансляции", true)
        .Hidden();  // not implemented yet
    b.Pick("recording.castQuality", L"Cast quality", L"Качество трансляции",
           {{"auto", L"Automatic", L"Автоматически"}, {"high", L"High", L"Высокое"}, {"medium", L"Medium", L"Среднее"},
            {"low", L"Low", L"Низкое"}},
           "auto")
        .Desc(L"Lower quality needs less bandwidth on a slow Wi-Fi.",
              L"Низкое качество требует меньше трафика при медленном Wi-Fi.")
        .Hidden();  // not implemented yet

    b.Sec("streaming", L"Streaming", L"Стриминг");
    b.Toggle("recording.playLocally", L"Play locally while streaming", L"Воспроизводить локально во время стриминга", true)
        .Hidden();  // not implemented yet
    b.Toggle("recording.includeAudio", L"Include audio", L"Передавать звук", true)
        .Hidden();  // not implemented yet
    b.Toggle("recording.includeVideo", L"Include video", L"Передавать видео", true)
        .Hidden();  // not implemented yet
    b.Toggle("recording.includeSubtitles", L"Include subtitles", L"Передавать субтитры", true)
        .Hidden();  // not implemented yet
    b.Pick("recording.preset", L"Encoding preset", L"Пресет кодирования",
           {{"ultrafast", L"Ultra fast", L"Сверхбыстрый"},
            {"superfast", L"Super fast", L"Очень быстрый"},
            {"veryfast", L"Very fast", L"Быстрее"},
            {"faster", L"Faster", L"Быстрый"},
            {"fast", L"Fast", L"Ускоренный"},
            {"medium", L"Medium", L"Средний"},
            {"slow", L"Slow", L"Медленный"},
            {"slower", L"Slower", L"Медленнее"},
            {"veryslow", L"Very slow", L"Очень медленный"}},
           "medium")
        .Desc(L"Slower presets give better quality at the same bitrate but load the CPU more.",
              L"Медленные пресеты дают лучшее качество при том же битрейте, но сильнее нагружают процессор.")
        .Hidden();  // not implemented yet
    b.Pick("recording.tune", L"Tuning", L"Оптимизация",
           {{"none", L"None", L"Нет"},
            {"film", L"Film", L"Фильм"},
            {"animation", L"Animation", L"Анимация"},
            {"grain", L"Grain", L"Зернистое видео"},
            {"stillimage", L"Still image", L"Статичная картинка"},
            {"fastdecode", L"Fast decode", L"Быстрое декодирование"},
            {"zerolatency", L"Zero latency", L"Минимальная задержка"}},
           "none")
        .Desc(L"Adjust the encoder for a kind of content.", L"Подстроить кодировщик под тип содержимого.")
        .Hidden();  // not implemented yet

    b.advanced = true;
    b.inferred = true;
    b.Pick("recording.container", L"Container", L"Контейнер",
           {{"mkv", L"Matroska (.mkv)", L"Matroska (.mkv)"}, {"mp4", L"MPEG-4 (.mp4)", L"MPEG-4 (.mp4)"},
            {"ts", L"MPEG-TS (.ts)", L"MPEG-TS (.ts)"}},
           "mkv")
        .Hidden();  // not implemented yet
    b.Number("recording.videoBitrate", L"Video bitrate", L"Битрейт видео", 0, 100000, 100, 0)
        .Unit(L"kbit/s", L"кбит/с")
        .Desc(L"0 lets the encoder choose from the preset.", L"0 — кодировщик выбирает сам по пресету.")
        .Hidden();  // not implemented yet

    // ===== Extensions =====
    b.Cat("extensions", "system", "plugins", L"Extensions", L"Расширения", L"Add-ons & Scripts", L"Дополнения и скрипты");

    b.Sec("addons", L"Add-ons", L"Дополнения");
    b.Info("extensions.list", L"Add-ons", L"Дополнения", "addons-list")
        .Desc(L"Lua and JavaScript mpv scripts from %APPDATA%\\Lumen\\scripts.",
              L"Скрипты mpv на Lua и JavaScript из %APPDATA%\\Lumen\\scripts.");
    b.Button("extensions.findMore", L"Find more extensions", L"Найти расширения", "find-extensions",
             L"Find more extensions", L"Найти расширения")
        .Group("addonsToolbar");
    b.Button("extensions.add", L"Add", L"Добавить", "add-extension", L"Add", L"Добавить").Group("addonsToolbar");
    b.Button("extensions.remove", L"Remove", L"Удалить", "remove-extension", L"Remove", L"Удалить").Group("addonsToolbar");
    b.Toggle("extensions.loadOnStartup", L"Load add-ons on startup", L"Загружать дополнения при запуске", true)
        .MpvBool("load-scripts")
        .Startup();

    b.advanced = true;
    b.inferred = true;
    b.Text("extensions.scriptOpts", L"Script options", L"Параметры скриптов")
        .Desc(L"Options passed to scripts, e.g. osc-visibility=always,thumbfast-network=yes",
              L"Параметры для скриптов, например osc-visibility=always,thumbfast-network=yes")
        .When("extensions.loadOnStartup", "true")
        .MpvBuild("script-opts", BuildScriptOpts);  // shares the list with network.ytdl
    b.Button("extensions.openFolder", L"Scripts folder", L"Папка скриптов", "open-scripts-folder", L"Open", L"Открыть");
}

struct Schema {
    std::vector<Group> groups;
    std::vector<Category> categories;
    std::unordered_map<std::string_view, const Item*> index;  // keys point into the items
    std::unordered_map<std::string_view, const Category*> catIndex;

    Schema() {
        Builder b;
        BuildApp(b);
        BuildMedia(b);
        BuildSystem(b);
        categories = std::move(b.cats);
        groups = {{"app", L"App", L"Приложение"}, {"media", L"Media", L"Медиа"}, {"system", L"System", L"Система"}};
        for (Category& c : categories) {
            catIndex.emplace(c.id, &c);
            for (Section& s : c.sections)
                for (Item& it : s.items) {
                    it.category = c.id;
                    it.section = s.id;
                }
        }
        for (const Category& c : categories)
            for (const Section& s : c.sections)
                for (const Item& it : s.items) index.emplace(it.key, &it);
    }
};

const Schema& TheSchema() {
    static const Schema schema;
    return schema;
}

// Lowercase + "ё" -> "е", so Russian search finds both spellings.
std::wstring Fold(std::wstring_view s) {
    if (s.empty()) return {};
    std::wstring out(s.size(), L'\0');
    int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), static_cast<int>(s.size()), out.data(),
                          static_cast<int>(out.size()), nullptr, nullptr, 0);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    for (wchar_t& c : out)
        if (c == L'ё') c = L'е';
    return out;
}

}  // namespace

const char* const kEqBandKeys[kEqBands] = {"audio.eq60", "audio.eq170", "audio.eq310", "audio.eq600", "audio.eq1k",
                                           "audio.eq3k", "audio.eq6k",  "audio.eq12k", "audio.eq14k", "audio.eq16k"};
const double kEqFrequencies[kEqBands] = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};

const Choice* Item::FindChoice(std::string_view value) const {
    for (const Choice& c : choices)
        if (c.value == value) return &c;
    return nullptr;
}

const std::vector<Group>& Groups() {
    return TheSchema().groups;
}

const std::vector<Category>& Categories() {
    return TheSchema().categories;
}

const Category* FindCategory(std::string_view id) {
    const auto& idx = TheSchema().catIndex;
    auto it = idx.find(id);
    return it == idx.end() ? nullptr : it->second;
}

const Item* FindItem(std::string_view key) {
    const auto& idx = TheSchema().index;
    auto it = idx.find(key);
    return it == idx.end() ? nullptr : it->second;
}

std::vector<const Item*> AllItems() {
    std::vector<const Item*> out;
    for (const Category& c : Categories())
        for (const Section& s : c.sections)
            for (const Item& it : s.items) out.push_back(&it);
    return out;
}

std::vector<const Item*> Search(std::wstring_view query) {
    std::vector<const Item*> out;
    std::wstring q = Fold(query);
    if (q.empty()) return out;
    for (const Item* it : AllItems()) {
        if (it->hidden) continue;
        for (const std::wstring* text : {&it->label, &it->labelRu, &it->description, &it->descriptionRu}) {
            if (Fold(*text).find(q) != std::wstring::npos) {
                out.push_back(it);
                break;
            }
        }
    }
    return out;
}

const std::wstring& AdvancedOptionsLabel(Lang lang) {
    static const std::wstring en = L"Advanced Options", ru = L"Дополнительные параметры";
    return lang == Lang::Ru ? ru : en;
}

const std::vector<EqPreset>& EqPresets() {
    // VLC modules/audio_filter/equalizer_presets.h; preamp is VLC's value minus 12 dB.
    static const std::vector<EqPreset> presets = {
        {"flat", 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {"classical", 0, {0, 0, 0, 0, 0, 0, -7.2, -7.2, -7.2, -9.6}},
        {"club", -6, {0, 0, 8, 5.6, 5.6, 5.6, 3.2, 0, 0, 0}},
        {"dance", -7, {9.6, 7.2, 2.4, 0, 0, -5.6, -7.2, -7.2, 0, 0}},
        {"fullbass", -7, {-8, 9.6, 9.6, 5.6, 1.6, -4, -8, -10.4, -11.2, -11.2}},
        {"fullbasstreble", -8, {7.2, 5.6, 0, -7.2, -4.8, 1.6, 8, 11.2, 12, 12}},
        {"fulltreble", -9, {-9.6, -9.6, -9.6, -4, 2.4, 11.2, 16, 16, 16, 16.8}},
        {"headphones", -8, {4.8, 11.2, 5.6, -3.2, -2.4, 1.6, 4.8, 9.6, 12.8, 14.4}},
        {"largehall", -7, {10.4, 10.4, 5.6, 5.6, 0, -4.8, -4.8, -4.8, 0, 0}},
        {"live", -5, {-4.8, 0, 4, 5.6, 5.6, 5.6, 4, 2.4, 2.4, 2.4}},
        {"party", -6, {7.2, 7.2, 0, 0, 0, 0, 0, 0, 7.2, 7.2}},
        {"pop", -6, {-1.6, 4.8, 7.2, 8, 5.6, 0, -2.4, -2.4, -1.6, -1.6}},
        {"reggae", -4, {0, 0, 0, -5.6, 0, 6.4, 6.4, 0, 0, 0}},
        {"rock", -7, {8, 4.8, -5.6, -8, -3.2, 4, 8.8, 11.2, 11.2, 11.2}},
        {"ska", -6, {-2.4, -4.8, -4, 0, 4, 5.6, 8.8, 9.6, 11.2, 9.6}},
        {"soft", -7, {4.8, 1.6, 0, -2.4, 0, 4, 8, 9.6, 11.2, 12}},
        {"softrock", -5, {4, 4, 2.4, 0, -4, -5.6, -3.2, 0, 2.4, 8.8}},
        {"techno", -7, {8, 5.6, 0, -5.6, -4.8, 0, 8, 9.6, 9.6, 8.8}},
    };
    return presets;
}

const EqPreset* FindEqPreset(std::string_view id) {
    for (const EqPreset& p : EqPresets())
        if (p.id == id) return &p;
    return nullptr;
}

const std::vector<KeyActionGroup>& KeyActionGroups() {
    static const std::vector<KeyActionGroup> groups = {
        {"basic", L"Basic", L"Основные"},
        {"playback", L"Playback", L"Воспроизведение"},
        {"audio", L"Audio", L"Звук"},
        {"subtitles", L"Subtitles", L"Субтитры"},
    };
    return groups;
}

const std::vector<KeyAction>& KeyActions() {
    // Defaults keep the bindings Lumen already ships with (README.md) and take the rest from
    // the Keyboard_Shortcuts frame. "Stop" is unassigned because S cycles subtitles in Lumen.
    static const std::vector<KeyAction> actions = {
        {"fullscreen", "basic", L"Toggle fullscreen", L"Полный экран", L"F"},
        {"exitFullscreen", "basic", L"Exit fullscreen / close dialog", L"Выйти из полного экрана / закрыть окно", L"Esc"},
        {"showTime", "basic", L"Show playback position and time", L"Показать позицию и время", L"T"},
        {"showControls", "basic", L"Show interface controls in fullscreen", L"Показать панель в полноэкранном режиме", L"I"},
        {"openFile", "basic", L"Open file", L"Открыть файл", L"Ctrl+O"},
        {"closeFile", "basic", L"Close file", L"Закрыть файл", L"Ctrl+W"},
        {"settings", "basic", L"Settings", L"Настройки", L"Ctrl+P"},
        {"screenshot", "basic", L"Take a screenshot", L"Сделать скриншот", L"Ctrl+S"},

        {"playPause", "playback", L"Play / pause", L"Пауза / продолжить", L"Space"},
        {"stop", "playback", L"Stop playback", L"Остановить", L""},
        {"next", "playback", L"Next track", L"Следующий файл", L"PageDown"},
        {"previous", "playback", L"Previous track", L"Предыдущий файл", L"PageUp"},
        {"nextProgram", "playback", L"Next program / Service ID", L"Следующая программа / Service ID", L"X"},
        {"seekForward", "playback", L"Short skip forward", L"Короткий шаг вперёд", L"Right"},
        {"seekBackward", "playback", L"Short skip backward", L"Короткий шаг назад", L"Left"},
        {"seekForwardVeryShort", "playback", L"Very short skip forward", L"Очень короткий шаг вперёд", L"Shift+Right"},
        {"seekBackwardVeryShort", "playback", L"Very short skip backward", L"Очень короткий шаг назад", L"Shift+Left"},
        {"seekForwardMedium", "playback", L"Medium skip forward", L"Средний шаг вперёд", L"Ctrl+Right"},
        {"seekBackwardMedium", "playback", L"Medium skip backward", L"Средний шаг назад", L"Ctrl+Left"},
        {"seekForwardLong", "playback", L"Long skip forward", L"Длинный шаг вперёд", L"Ctrl+Shift+Right"},
        {"seekBackwardLong", "playback", L"Long skip backward", L"Длинный шаг назад", L"Ctrl+Shift+Left"},
        {"frameStep", "playback", L"Next frame", L"Следующий кадр", L"."},
        {"frameBackStep", "playback", L"Previous frame", L"Предыдущий кадр", L","},
        {"speedUp", "playback", L"Faster", L"Быстрее", L"]"},
        {"speedDown", "playback", L"Slower", L"Медленнее", L"["},
        {"speedReset", "playback", L"Normal speed", L"Обычная скорость", L"Backspace"},

        {"volumeUp", "audio", L"Volume up", L"Громче", L"Up"},
        {"volumeDown", "audio", L"Volume down", L"Тише", L"Down"},
        {"mute", "audio", L"Mute", L"Без звука", L"M"},
        {"cycleAudio", "audio", L"Next audio track", L"Следующая аудиодорожка", L"A"},

        {"cycleSubtitles", "subtitles", L"Next subtitle track", L"Следующие субтитры", L"S"},
        {"subtitleDelayUp", "subtitles", L"Subtitle delay +100 ms", L"Задержка субтитров +100 мс", L"Ctrl+Shift+Up"},
        {"subtitleDelayDown", "subtitles", L"Subtitle delay −100 ms", L"Задержка субтитров −100 мс", L"Ctrl+Shift+Down"},
    };
    return actions;
}

const KeyAction* FindKeyAction(std::string_view id) {
    for (const KeyAction& a : KeyActions())
        if (a.id == id) return &a;
    return nullptr;
}

int AutoDecodeThreads() {
    const unsigned n = std::thread::hardware_concurrency();
    return static_cast<int>(std::clamp(n, 2u, 6u));
}

}  // namespace settings
