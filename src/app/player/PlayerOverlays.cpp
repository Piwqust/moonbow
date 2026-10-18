#include "app/player/PlayerOverlays.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/LibraryCommon.h"
#include "app/player/Episodes.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "gfx/Text.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace app {

using namespace ui;

namespace {

const PlayerState& PS() {
    return TheApp().player().State();
}

const mpv_node* NodeGet(const mpv_node* map, const char* key) {
    if (!map || map->format != MPV_FORMAT_NODE_MAP) return nullptr;
    for (int i = 0; i < map->u.list->num; ++i)
        if (std::strcmp(map->u.list->keys[i], key) == 0) return &map->u.list->values[i];
    return nullptr;
}

std::string NodeStr(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_STRING && n->u.string ? n->u.string : "";
}

double NodeNum(const mpv_node* n) {
    if (!n) return NAN;
    if (n->format == MPV_FORMAT_DOUBLE) return n->u.double_;
    if (n->format == MPV_FORMAT_INT64) return static_cast<double>(n->u.int64);
    return NAN;
}

std::wstring Num(double v, int decimals) {
    if (std::isnan(v)) return L"—";
    wchar_t buf[32];
    swprintf_s(buf, L"%.*f", decimals, v);
    return panel::LocalDecimal(buf);
}

std::wstring Rate(double bitsPerSecond) {
    if (std::isnan(bitsPerSecond) || bitsPerSecond <= 0) return L"—";
    if (bitsPerSecond >= 1e6) return Fmt(Tr(L"{0} Mb/s"), {Num(bitsPerSecond / 1e6, 1)});
    return Fmt(Tr(L"{0} kb/s"), {Num(bitsPerSecond / 1e3, 0)});
}

std::wstring W(const std::string& s) {
    return Utf8ToWide(s);
}

constexpr float kStatsPad = 12, kStatsLine = 18;

// "@lumen:lavfi=[bwdif=deint=all,deblock=...]" -> "bwdif, deblock".
std::wstring GraphFilters(const std::string& graph) {
    std::wstring out;
    size_t start = 0;
    while (start < graph.size()) {
        size_t end = graph.find_first_of(",;", start);
        if (end == std::string::npos) end = graph.size();
        std::string f = graph.substr(start, end - start);
        start = end + 1;
        while (!f.empty() && (f.front() == '[' || f.front() == ' ')) f.erase(0, 1);
        if (const size_t eq = f.find('='); eq != std::string::npos) {
            if (f.compare(0, eq, "hue") == 0 && f.find("s=0") != std::string::npos) f = "grayscale";
            else f.resize(eq);
        }
        while (!f.empty() && (f.back() == ']' || f.back() == ' ')) f.pop_back();
        if (!f.empty()) out += (out.empty() ? L"" : L", ") + Utf8ToWide(f);
    }
    return out;
}

// mpv's vf list as the user knows it: the CPU filters by name, the d3d11vpp pass by what it does.
std::wstring FilterList(const mpv_node* list) {
    std::wstring out;
    auto add = [&out](const std::wstring& s) {
        if (!s.empty()) out += (out.empty() ? L"" : L", ") + s;
    };
    if (!list || list->format != MPV_FORMAT_NODE_ARRAY) return out;
    for (int i = 0; i < list->u.list->num; ++i) {
        const mpv_node* f = &list->u.list->values[i];
        const mpv_node* enabled = NodeGet(f, "enabled");
        if (enabled && enabled->format == MPV_FORMAT_FLAG && !enabled->u.flag) continue;
        const std::string name = NodeStr(NodeGet(f, "name"));
        const mpv_node* params = NodeGet(f, "params");
        if (name == "lavfi") {
            add(GraphFilters(NodeStr(NodeGet(params, "graph"))));
        } else if (name == "d3d11vpp") {
            const std::string mode = NodeStr(NodeGet(params, "scaling-mode"));
            const std::string scale = NodeStr(NodeGet(params, "scale"));
            if (mode == "nvidia" || mode == "intel")
                add(std::wstring(mode == "nvidia" ? L"RTX Super Resolution" : L"Intel VSR") +
                    (scale.empty() ? L"" : L" ×" + panel::LocalDecimal(Utf8ToWide(scale))));
            const std::string hdr = NodeStr(NodeGet(params, "nvidia-true-hdr"));
            if (!hdr.empty() && hdr != "no") add(L"RTX Video HDR");
            const std::string deint = NodeStr(NodeGet(params, "deint"));
            if (!deint.empty() && deint != "no") add(Tr(L"GPU deinterlacing"));
        } else if (!name.empty()) {
            add(Utf8ToWide(name));
        }
    }
    return out;
}

}  // namespace

// --- StatsOverlay ---------------------------------------------------------------------------

struct StatsOverlay::Values {
    std::string format, hwdec, vo, pixfmt, hwPixfmt, matrix, levels, primaries, gamma, audioCodec;
    std::string outPixfmt, outHwPixfmt, videoSync, tscale, scale;
    std::wstring filters;
    bool interpolation = false, displaySync = false, deinterlacing = false;
    double w = NAN, h = NAN, dw = NAN, dh = NAN, maxLuma = NAN, maxCll = NAN, maxFall = NAN;
    double outDw = NAN, outDh = NAN, vsyncRatio = NAN, delayed = NAN;
    double containerFps = NAN, estimatedFps = NAN, displayFps = NAN;
    double drops = NAN, decoderDrops = NAN, videoRate = NAN, audioRate = NAN, cache = NAN, avsync = NAN;
    double sampleRate = NAN, channels = NAN;
};

StatsOverlay::StatsOverlay(MainWindow* window, std::function<void()> repaint)
    : window_(window), repaint_(std::move(repaint)), v_(std::make_unique<Values>()) {
    // Shown before mpv was ready: start observing once it is.
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned) {
        if (shown_ && observers_.empty() && TheApp().player().Ready()) Observe();
    });
}

StatsOverlay::~StatsOverlay() {
    Unobserve();
}

void StatsOverlay::SetShown(bool shown) {
    if (shown == shown_) return;
    shown_ = shown;
    if (shown) {
        *v_ = Values{};
        Observe();
        Rebuild();
    } else {
        Unobserve();
        lines_.clear();
    }
    repaint_();
}

void StatsOverlay::Observe() {
    Player& p = TheApp().player();
    if (!p.Ready() || !observers_.empty()) return;
    auto str = [this, &p](const char* name, std::string Values::*field) {
        observers_.push_back(p.Observe(name, MPV_FORMAT_STRING, [this, field](const mpv_event_property& e) {
            const char* s = e.format == MPV_FORMAT_STRING && e.data ? *static_cast<char**>(e.data) : nullptr;
            (*v_).*field = s ? s : "";
            dirty_ = true;
        }));
    };
    auto num = [this, &p](const char* name, double Values::*field) {
        observers_.push_back(p.Observe(name, MPV_FORMAT_DOUBLE, [this, field](const mpv_event_property& e) {
            (*v_).*field = e.format == MPV_FORMAT_DOUBLE && e.data ? *static_cast<double*>(e.data) : NAN;
            dirty_ = true;
        }));
    };
    str("video-format", &Values::format);
    str("hwdec-current", &Values::hwdec);
    str("current-vo", &Values::vo);
    str("audio-codec-name", &Values::audioCodec);
    str("video-sync", &Values::videoSync);
    str("tscale", &Values::tscale);
    str("scale", &Values::scale);
    auto flag = [this, &p](const char* name, bool Values::*field) {
        observers_.push_back(p.Observe(name, MPV_FORMAT_FLAG, [this, field](const mpv_event_property& e) {
            (*v_).*field = e.format == MPV_FORMAT_FLAG && e.data && *static_cast<int*>(e.data) != 0;
            dirty_ = true;
        }));
    };
    flag("interpolation", &Values::interpolation);
    flag("display-sync-active", &Values::displaySync);
    flag("deinterlace-active", &Values::deinterlacing);
    num("vsync-ratio", &Values::vsyncRatio);
    num("vo-delayed-frame-count", &Values::delayed);
    observers_.push_back(p.Observe("vf", MPV_FORMAT_NODE, [this](const mpv_event_property& e) {
        v_->filters = FilterList(e.format == MPV_FORMAT_NODE ? static_cast<const mpv_node*>(e.data) : nullptr);
        dirty_ = true;
    }));
    // After the filter chain: what the renderer gets (the AI upscaler enlarges it).
    observers_.push_back(p.Observe("video-out-params", MPV_FORMAT_NODE, [this](const mpv_event_property& e) {
        const mpv_node* n = e.format == MPV_FORMAT_NODE ? static_cast<const mpv_node*>(e.data) : nullptr;
        v_->outPixfmt = NodeStr(NodeGet(n, "pixelformat"));
        v_->outHwPixfmt = NodeStr(NodeGet(n, "hw-pixelformat"));
        v_->outDw = NodeNum(NodeGet(n, "dw"));
        v_->outDh = NodeNum(NodeGet(n, "dh"));
        dirty_ = true;
    }));
    num("container-fps", &Values::containerFps);
    num("estimated-vf-fps", &Values::estimatedFps);
    num("display-fps", &Values::displayFps);
    num("frame-drop-count", &Values::drops);
    num("decoder-frame-drop-count", &Values::decoderDrops);
    num("video-bitrate", &Values::videoRate);
    num("audio-bitrate", &Values::audioRate);
    num("demuxer-cache-duration", &Values::cache);
    num("avsync", &Values::avsync);
    observers_.push_back(p.Observe("video-params", MPV_FORMAT_NODE, [this](const mpv_event_property& e) {
        const mpv_node* n = e.format == MPV_FORMAT_NODE ? static_cast<const mpv_node*>(e.data) : nullptr;
        Values& v = *v_;
        v.pixfmt = NodeStr(NodeGet(n, "pixelformat"));
        v.hwPixfmt = NodeStr(NodeGet(n, "hw-pixelformat"));
        v.matrix = NodeStr(NodeGet(n, "colormatrix"));
        v.levels = NodeStr(NodeGet(n, "colorlevels"));
        v.primaries = NodeStr(NodeGet(n, "primaries"));
        v.gamma = NodeStr(NodeGet(n, "gamma"));
        v.w = NodeNum(NodeGet(n, "w"));
        v.h = NodeNum(NodeGet(n, "h"));
        v.dw = NodeNum(NodeGet(n, "dw"));
        v.dh = NodeNum(NodeGet(n, "dh"));
        v.maxLuma = NodeNum(NodeGet(n, "max-luma"));
        v.maxCll = NodeNum(NodeGet(n, "max-cll"));
        v.maxFall = NodeNum(NodeGet(n, "max-fall"));
        dirty_ = true;
    }));
    // The whole map, not "audio-params/samplerate": a sub-property observer misses the new
    // value after an audio track switch and stays at "—".
    observers_.push_back(p.Observe("audio-params", MPV_FORMAT_NODE, [this](const mpv_event_property& e) {
        const mpv_node* n = e.format == MPV_FORMAT_NODE ? static_cast<const mpv_node*>(e.data) : nullptr;
        v_->sampleRate = NodeNum(NodeGet(n, "samplerate"));
        v_->channels = NodeNum(NodeGet(n, "channel-count"));
        dirty_ = true;
    }));
    // Values like avsync change every frame: repaint on a slow clock instead of per event.
    timer_ = window_->StartTimer(500, [this] {
        if (!dirty_) return;
        dirty_ = false;
        Rebuild();
        repaint_();
    }, true);
}

void StatsOverlay::Unobserve() {
    Player& p = TheApp().player();
    for (uint64_t id : observers_) p.Unobserve(id);
    observers_.clear();
    if (timer_) window_->CancelTimer(timer_);
    timer_ = 0;
}

void StatsOverlay::Rebuild() {
    const Values& v = *v_;
    lines_.clear();
    // Decoder: format and hardware decoding; "-copy" modes bring every frame back to RAM.
    std::wstring dec = v.format.empty() ? L"—" : W(v.format);
    if (v.hwdec.empty() || v.hwdec == "no")
        dec += std::wstring(L" · ") + Tr(L"software");
    else if (v.hwdec.size() > 5 && v.hwdec.compare(v.hwdec.size() - 5, 5, "-copy") == 0)
        dec += L" · " + W(v.hwdec) + L" (" + Tr(L"copies frames to RAM") + L")";
    else
        dec += L" · " + W(v.hwdec) + L" (" + Tr(L"zero-copy") + L")";
    lines_.push_back({Tr(L"Decoder"), dec});

    // Decoded size, then the size after the filters and the aspect ratio, as the renderer gets it.
    std::wstring out = v.vo.empty() ? L"—" : W(v.vo);
    if (!std::isnan(v.w) && !std::isnan(v.h)) {
        out += L" · " + Num(v.w, 0) + L"×" + Num(v.h, 0);
        const double ow = std::isnan(v.outDw) ? v.dw : v.outDw, oh = std::isnan(v.outDh) ? v.dh : v.outDh;
        if (!std::isnan(ow) && !std::isnan(oh) && (ow != v.w || oh != v.h))
            out += L" → " + Num(ow, 0) + L"×" + Num(oh, 0);
    }
    const std::string& pix = v.outPixfmt.empty() ? v.pixfmt : v.outPixfmt;
    const std::string& hwPix = v.outPixfmt.empty() ? v.hwPixfmt : v.outHwPixfmt;
    if (!pix.empty()) out += L" · " + W(pix);
    if (!hwPix.empty()) out += L" (" + W(hwPix) + L")";
    lines_.push_back({Tr(L"Output"), out});

    std::wstring filters = v.filters;
    if (v.deinterlacing && filters.find(Tr(L"GPU deinterlacing")) == std::wstring::npos &&
        filters.find(L"bwdif") == std::wstring::npos && filters.find(L"yadif") == std::wstring::npos)
        filters += (filters.empty() ? L"" : L", ") + std::wstring(Tr(L"GPU deinterlacing"));
    lines_.push_back({Tr(L"Filters"), filters.empty() ? std::wstring(Tr(L"none")) : filters});
    if (!v.scale.empty()) lines_.push_back({Tr(L"Scaler"), W(v.scale)});
    if (!v.videoSync.empty()) {
        std::wstring sync = W(v.videoSync);
        if (v.interpolation) sync += L" · " + Fmt(Tr(L"interpolation {0}"), {W(v.tscale)});
        if (v.displaySync && !std::isnan(v.vsyncRatio)) sync += L" · " + Fmt(Tr(L"vsync ×{0}"), {Num(v.vsyncRatio, 2)});
        lines_.push_back({Tr(L"Timing"), sync});
    }

    if (!v.matrix.empty() || !v.primaries.empty()) {
        std::wstring c;
        for (const std::string* s : {&v.matrix, &v.primaries, &v.gamma, &v.levels})
            if (!s->empty()) c += (c.empty() ? L"" : L" · ") + W(*s);
        lines_.push_back({Tr(L"Color"), c});
    }
    const bool hdr = v.gamma == "pq" || v.gamma == "hlg" || (!std::isnan(v.maxCll) && v.maxCll > 0);
    if (hdr) {
        std::wstring h = v.gamma == "hlg" ? L"HLG" : L"PQ";
        if (!std::isnan(v.maxLuma) && v.maxLuma > 0) h += L" · " + Fmt(Tr(L"mastering {0} nits"), {Num(v.maxLuma, 0)});
        if (!std::isnan(v.maxCll) && v.maxCll > 0) h += L" · MaxCLL " + Num(v.maxCll, 0);
        if (!std::isnan(v.maxFall) && v.maxFall > 0) h += L" · MaxFALL " + Num(v.maxFall, 0);
        lines_.push_back({L"HDR", h});
    }
    lines_.push_back({Tr(L"Frame rate"), Fmt(Tr(L"{0} file · {1} estimated · {2} display"),
                                             {Num(v.containerFps, 3), Num(v.estimatedFps, 2), Num(v.displayFps, 2)})});
    lines_.push_back({Tr(L"Dropped frames"),
                      Fmt(Tr(L"{0} output · {1} decoder · {2} late"),
                          {Num(v.drops, 0), Num(v.decoderDrops, 0), Num(v.delayed, 0)})});
    lines_.push_back({Tr(L"Bitrate"), Fmt(Tr(L"video {0} · audio {1}"), {Rate(v.videoRate), Rate(v.audioRate)})});
    lines_.push_back({Tr(L"Cache"), std::isnan(v.cache) ? L"—" : Fmt(Tr(L"{0} s ahead"), {Num(v.cache, 1)})});
    std::wstring sync = L"—";
    if (!std::isnan(v.avsync)) sync = (v.avsync >= 0 ? L"+" : L"") + Fmt(Tr(L"{0} ms"), {Num(v.avsync * 1000, 1)});
    lines_.push_back({Tr(L"A/V sync"), sync});
    if (!v.audioCodec.empty())
        lines_.push_back({Tr(L"Audio"), Fmt(Tr(L"{0} · {1} kHz · {2} ch"),
                                            {W(v.audioCodec), Num(v.sampleRate / 1000, 1), Num(v.channels, 0)})});

    gfx::Text& t = gfx::Text::Get();
    labelW_ = 0;
    float valueW = t.Measure(Tr(L"Statistics"), TextStyle::SmallMedium).width + 90;
    for (const auto& [label, value] : lines_) {
        labelW_ = std::max(labelW_, t.Measure(label, TextStyle::Small).width);
        valueW = std::max(valueW, t.Measure(value, TextStyle::MonoSmall).width);
    }
    width_ = kStatsPad * 2 + labelW_ + 16 + valueW;
}

void StatsOverlay::Paint(Painter& p, const Rect& video) {
    if (!shown_ || lines_.empty() || video.Empty()) return;
    const float h = kStatsPad * 2 + kStatsLine * static_cast<float>(lines_.size() + 1) + 4;
    const Rect box{video.x + 12, video.y + 12, std::min(width_, video.w - 24), std::min(h, video.h - 24)};
    p.PushClip(box);
    // Nearly opaque: over a bright frame a see-through panel leaves the labels unreadable.
    p.Fill(box, P().bgPanel.Fade(0.94f), 6);
    p.Stroke(box, P().line, 6);
    float y = box.y + kStatsPad;
    p.Text(Tr(L"Statistics"), {box.x + kStatsPad, y, box.w - 2 * kStatsPad, kStatsLine}, TextStyle::SmallMedium,
           P().textPrimary);
    p.Text(ShortcutText(Action::ToggleStats), {box.x + kStatsPad, y, box.w - 2 * kStatsPad, kStatsLine},
           TextStyle::Small, P().textSecondary, HAlign::Trailing);
    y += kStatsLine + 4;
    for (const auto& [label, value] : lines_) {
        p.Text(label, {box.x + kStatsPad, y, labelW_, kStatsLine}, TextStyle::Small, P().textSecondary);
        const float vx = box.x + kStatsPad + labelW_ + 16;
        p.Text(value, {vx, y, box.Right() - kStatsPad - vx, kStatsLine}, TextStyle::MonoSmall, P().textPrimary);
        y += kStatsLine;
    }
    p.PopClip();
}

// --- NextEpisodeCard ------------------------------------------------------------------------

// Lists the folder off the UI thread when the queue doesn't hold the next episode.
struct NextEpisodeCard::FolderJob {
    std::mutex mutex;
    bool done = false;
    std::wstring result;
};

NextEpisodeCard::NextEpisodeCard(MainWindow* window) : window_(window) {
    dir = Dir::Column;
    gap = 10;
    pad = Insets::All(16);
    width = Length::Px(340);
    interactive = true;  // clicks on the card don't reach the video
    visible = false;
    title_ = Add<Label>(L"", TextStyle::BodyMedium, Tone::Primary);
    title_->width = Length::Grow();
    name_ = Add<Label>(L"", TextStyle::Small, Tone::Secondary);
    name_->width = Length::Grow();
    auto* row = Add<Widget>();
    row->dir = Dir::Row;
    row->gap = 8;
    auto* play = row->Add<Button>(Tr(L"Play now"), ButtonKind::Primary, "play");
    play->id = "play-next";
    play->onClick = [this] { PlayNext(); };
    auto* cancel = row->Add<Button>(Tr(L"Cancel"), ButtonKind::Secondary);
    cancel->id = "cancel-next";
    cancel->onClick = [this] {
        // Cancel = stay on this episode: no automatic switch at its end.
        dismissed_ = true;
        if (fromQueue_ || TheApp().config().GetString("playback.whenFinished") == "next") Live().SetStopAfter(true);
        SetWanted(false);
    };
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) { OnPlayer(m); });
    queueConn_ = Connect(TheApp().queue().changed, [this] {
        if (serial_) Resolve();
        Update();
    });
}

NextEpisodeCard::~NextEpisodeCard() = default;

void NextEpisodeCard::Paint(Painter& p) {
    p.Shadow(rect, 12, 15, 8);
    p.Fill(rect, P().bgMenu.Fade(0.96f), 12);
    p.Stroke(rect, P().line, 12);
}

void NextEpisodeCard::SetWanted(bool wanted) {
    if (wanted == wanted_) return;
    wanted_ = wanted;
    shownSecs_ = -1;
    if (Parent()) Parent()->InvalidateLayout();
}

void NextEpisodeCard::OnPlayer(unsigned m) {
    const PlayerState& s = PS();
    if (s.fileLoaded && s.loadSerial != serial_) OnNewFile();
    if (m & kChangeTracks) CarryLanguages();
    if (m & (kChangeTime | kChangeChapters | kChangeState | kChangeFile | kChangeError)) Update();
    if (m & kChangeEnd) OnEnded();
}

void NextEpisodeCard::OnNewFile() {
    const PlayerState& s = PS();
    serial_ = s.loadSerial;
    const QueueItem* item = TheApp().queue().CurrentItem();
    curPath_ = item ? item->path : s.path;
    dismissed_ = false;
    job_.reset();
    Resolve();
}

void NextEpisodeCard::Resolve() {
    nextPath_.clear();
    fromQueue_ = false;
    if (curPath_.empty() || lib::IsUrl(curPath_) || !episodes::Parse(curPath_).Valid()) return;
    const PlayQueue& q = TheApp().queue();
    const int ni = q.NextIndex(false);
    // With a next item in the queue (or repeating this one) the folder search is moot: a result
    // it already found must not come back through Update() and replace the queue the user built.
    if (ni >= 0) job_.reset();
    if (ni >= 0 && ni != q.Current()) {
        const std::wstring& next = q.Items()[static_cast<size_t>(ni)].path;
        if (episodes::IsNext(curPath_, next)) {
            nextPath_ = next;
            fromQueue_ = true;
        }
        // Something else plays next (a playlist the user made): leave it alone.
        return;
    }
    if (ni >= 0) return;  // repeating this one
    if (!job_) {
        job_ = std::make_shared<FolderJob>();
        std::thread([job = job_, path = curPath_] {
            const std::vector<std::wstring> siblings = ListSiblingMedia(path);
            const int i = episodes::FindNext(path, siblings);
            std::lock_guard<std::mutex> lock(job->mutex);
            if (i >= 0) job->result = siblings[static_cast<size_t>(i)];
            job->done = true;
        }).detach();
    }
}

void NextEpisodeCard::Update() {
    const PlayerState& s = PS();
    if (job_ && nextPath_.empty()) {
        std::lock_guard<std::mutex> lock(job_->mutex);
        if (job_->done) nextPath_ = job_->result;
    }
    bool want = false;
    double left = 0;
    if (s.fileLoaded && !nextPath_.empty() && !dismissed_ && s.duration > 60 && !s.eof) {
        left = std::max(0.0, s.duration - s.position);
        bool credits = false;
        if (s.chapter >= 0 && s.chapter < static_cast<int64_t>(s.chapters.size())) {
            const Chapter& c = s.chapters[static_cast<size_t>(s.chapter)];
            credits = c.time > s.duration * 0.5 && episodes::KindOf(c.title) == episodes::ChapterKind::Credits;
        }
        want = left <= 30 || credits;
    }
    SetWanted(want);
    if (!want) return;
    const long long secs = static_cast<long long>(std::ceil(left - 1e-3));
    if (secs == shownSecs_) return;
    shownSecs_ = secs;
    const episodes::Episode e = episodes::Parse(nextPath_);
    title_->SetText(Fmt(Tr(L"Next episode: {0} · in {1} s"), {episodes::Label(e), std::to_wstring(secs)}));
    const std::wstring name = episodes::DisplayName(nextPath_);
    name_->SetText(name);
    if (name_->visible != !name.empty()) {
        name_->visible = !name.empty();
        InvalidateLayout();
    }
}

void NextEpisodeCard::PlayNext() {
    if (nextPath_.empty()) return;
    const std::wstring next = nextPath_;
    SetWanted(false);
    dismissed_ = true;
    App& app = TheApp();
    const int ni = app.queue().NextIndex(false);
    if (ni >= 0 && app.queue().Items()[static_cast<size_t>(ni)].path == next)
        app.PlayIndex(ni, false);
    else
        app.Open({next});  // a single local file brings its folder along as the queue
}

void NextEpisodeCard::OnEnded() {
    // The queue moves on by itself; a next episode found only in the folder is ours to start,
    // unless the app already did (End of queue > Play the next file in the folder).
    if (fromQueue_ || nextPath_.empty() || dismissed_ || TheApp().config().GetBool("playback.stopAfterEach")) return;
    const std::wstring ended = curPath_;
    window_->Post([this, ended] {
        const QueueItem* item = TheApp().queue().CurrentItem();
        if (item && item->path == ended && curPath_ == ended && !dismissed_) PlayNext();
    });
}

void NextEpisodeCard::CarryLanguages() {
    const PlayerState& s = PS();
    if (!s.fileLoaded || s.tracks.empty() || s.path.empty()) return;
    if (s.path != carryPath_) {
        const bool next = !carryPath_.empty() && episodes::IsNext(carryPath_, s.path);
        carryPath_ = s.path;
        if (next) {
            Player& p = TheApp().player();
            auto pick = [&s](const wchar_t* type, const std::wstring& lang, const std::wstring& title) -> const Track* {
                const Track* best = nullptr;
                for (const Track& t : s.tracks) {
                    if (t.type != type || t.lang.empty() || _wcsicmp(t.lang.c_str(), lang.c_str()) != 0) continue;
                    if (t.title == title) return &t;
                    if (!best) best = &t;
                }
                return best;
            };
            if (!audioLang_.empty())
                if (const Track* t = pick(L"audio", audioLang_, audioTitle_); t && !t->selected) p.SetTrack("aid", t->id);
            bool anySub = false, subSelected = false;
            for (const Track& t : s.tracks)
                if (t.type == L"sub") {
                    anySub = true;
                    subSelected |= t.selected;
                }
            if (hadSubs_ && anySub) {
                if (subsOff_) {
                    if (subSelected) p.SetTrack("sid", 0);
                } else if (!subLang_.empty()) {
                    if (const Track* t = pick(L"sub", subLang_, subTitle_); t && !t->selected) p.SetTrack("sid", t->id);
                }
            }
            return;  // the choice we just made comes back as the next track-list change
        }
    }
    // Remember this file's choice.
    bool anySub = false;
    std::wstring sLang, sTitle;
    bool sOn = false;
    for (const Track& t : s.tracks) {
        if (t.type == L"audio" && t.selected) {
            audioLang_ = t.lang;
            audioTitle_ = t.title;
        } else if (t.type == L"sub") {
            anySub = true;
            if (t.selected && !sOn) {
                sOn = true;
                sLang = t.lang;
                sTitle = t.title;
            }
        }
    }
    hadSubs_ = anySub;
    subsOff_ = anySub && !sOn;
    subLang_ = sLang;
    subTitle_ = sTitle;
}

// --- SkipChapterButton ----------------------------------------------------------------------

// Primary (white): the one action the viewer wants right now, like Netflix's and Plex's.
SkipChapterButton::SkipChapterButton(NextEpisodeCard* card) : Button(L"", ButtonKind::Primary, "next"), card_(card) {
    visible = false;
    id = "skip-chapter";
    onClick = [this] {
        const PlayerState& s = PS();
        if (s.chapter < 0 || s.chapter >= static_cast<int64_t>(s.chapters.size())) return;
        const size_t next = static_cast<size_t>(s.chapter) + 1;
        if (next < s.chapters.size())
            TheApp().player().SeekAbsolute(s.chapters[next].time, true);
        else if (card_ && card_->HasNext())
            card_->PlayNext();
        else
            TheApp().player().SeekAbsolute(std::max(0.0, s.duration - 0.5), true);
    };
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeTime | kChangeChapters | kChangeFile | kChangeState)) Update();
    });
}

void SkipChapterButton::Update() {
    const PlayerState& s = PS();
    int k = 0;
    if (s.fileLoaded && !s.eof && s.chapter >= 0 && s.chapter < static_cast<int64_t>(s.chapters.size()))
        k = static_cast<int>(episodes::KindOf(s.chapters[static_cast<size_t>(s.chapter)].title));
    const bool want = k != 0 && !(card_ && card_->Wanted());
    if (k != kind_) {
        kind_ = k;
        if (k == static_cast<int>(episodes::ChapterKind::Intro)) SetText(Tr(L"Skip intro"));
        else if (k == static_cast<int>(episodes::ChapterKind::Credits)) SetText(Tr(L"Skip credits"));
    }
    if (want == wanted_) return;
    wanted_ = want;
    if (Parent()) Parent()->InvalidateLayout();
}

}  // namespace app
