// Music view shown instead of the video for audio files (design/ref/Player-Music_*.png):
// a large cover over a blurred, darkened copy of itself, and "title - artist" below.
// Files without artwork get generated monochrome art (concentric grooves).
// With lyrics (a .lrc next to the track or an embedded tag) a wide view puts the cover on the
// left and the lyrics on the right: synced lines follow playback with the sung line highlighted
// and a click on a line seeks to it; unsynced text stays put and scrolls with the wheel.
// With Settings > Audio > Visualization on, a playing song shows the visualizer where the cover
// is (spectrum bars, a radial spectrometer, an oscilloscope, a scrolling waveform or a pair of
// analog VU meters), fed by
// audio/LoopbackCapture from Moonbow's own sound; a pause brings the cover back.
#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/MainWindow.h"
#include "app/StandInArt.h"
#include "app/library/LibraryCommon.h"
#include "app/player/AudioFeed.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/Lyrics.h"
#include "app/player/NowPlaying.h"
#include "app/player/PlayerView.h"
#include "app/player/VizPaint.h"
#include "audio/LoopbackCapture.h"
#include "audio/LoopbackSupport.h"
#include "gfx/Gpu.h"
#include "app/I18n.h"
#include "gfx/Text.h"
#include "library/MediaTypes.h"
#include "net/Http.h"
#include "net/Online.h"
#include "ui/Anim.h"

#include <d2d1_1.h>
#include <d2d1effects.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace app {

namespace panel {
std::vector<std::wstring> PickFiles(int kind);  // TracksPanel.cpp
}

using namespace ui;

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// A .lrc read on a worker thread (the file may sit on a slow network share); the view takes
// the result on its next paint or time update.
struct LyricsLoad {
    std::mutex mu;
    bool done = false;
    Lyrics lyrics;
};

constexpr float kWideLyrics = 720;  // narrower views show only the sung line under the title
// Lines light up this much early: an exact seek to a stamp may land a frame before it.
constexpr double kLead = 0.1;
constexpr float kEdgeFade = 72;  // lyrics fade out over this much at the column's top and bottom

enum class VizStyle { Off, Spectrometer, Scope, Spectrum, Waveform, Vu };

VizStyle VizStyleOf(const std::string& v) {
    if (v == "spectrometer") return VizStyle::Spectrometer;
    if (v == "scope") return VizStyle::Scope;
    if (v == "spectrum") return VizStyle::Spectrum;
    if (v == "waveform") return VizStyle::Waveform;
    if (v == "vu") return VizStyle::Vu;
    return VizStyle::Off;
}

// audio.visualizerLayout: where a bar-like visualizer goes (the spectrometer always replaces the cover).
enum class VizLayout { Replace, Wings, Side, Below };

VizLayout VizLayoutOf(const std::string& v) {
    if (v == "wings") return VizLayout::Wings;
    if (v == "side") return VizLayout::Side;
    if (v == "below") return VizLayout::Below;
    return VizLayout::Replace;
}

// How the lyrics column looks: audio.lyricsStyle picks a preset, the other audio.lyrics* settings
// replace parts of it.
struct LyricsLook {
    std::wstring font;  // empty: Inter
    float size = 20;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    bool center = false;
    float anchor = 0.42f;      // where the sung line sits, as a share of the column's height
    int before = -1, after = -1;  // lines shown above / below the sung one; -1 = all
    bool fill = false;         // karaoke: the sung line lights up as it is sung
    float grow = 1;            // scale of the sung line
    float sung = 0.96f, ahead = 0.62f, past = 0.46f;  // brightness of the sung line, lines to come, sung lines
    float gap = 14;
    bool only = false;  // audio.lyricsOnly: the lyrics take the whole view, larger
};

LyricsLook ReadLyricsLook(const Config& c) {
    LyricsLook k;
    const std::string style = c.GetString("audio.lyricsStyle");
    if (style == "spotify") {  // big bold lines; what is sung stays lit, what comes is dark
        k.size = 28, k.weight = DWRITE_FONT_WEIGHT_BOLD, k.anchor = 0.32f;
        k.sung = 1, k.ahead = 0.34f, k.past = 0.82f, k.gap = 18;
    } else if (style == "apple") {  // big bold lines, the sung one fills word by word
        k.size = 30, k.weight = DWRITE_FONT_WEIGHT_BOLD, k.anchor = 0.32f, k.fill = true;
        k.sung = 1, k.ahead = 0.32f, k.past = 0.28f, k.gap = 24;
    } else if (style == "yandex") {  // centered, the sung line larger than the rest
        k.size = 24, k.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, k.center = true, k.anchor = 0.45f, k.grow = 1.2f;
        k.sung = 1, k.ahead = 0.44f, k.past = 0.30f, k.gap = 18;
    } else if (style == "karaoke") {  // the sung line and the next one, low on the screen
        k.size = 34, k.weight = DWRITE_FONT_WEIGHT_BOLD, k.center = true, k.anchor = 0.6f, k.fill = true;
        k.before = 0, k.after = 1, k.sung = 1, k.ahead = 0.5f, k.past = 0.3f, k.gap = 22;
    }
    k.only = c.GetBool("audio.lyricsOnly");
    if (k.only) {  // read from across a room: larger, and centered unless the user says otherwise
        k.size *= 1.5f;
        k.gap *= 1.5f;
        k.center = true;
    }
    k.font = Utf8ToWide(c.GetString("audio.lyricsFont"));
    const std::string size = c.GetString("audio.lyricsSize");
    k.size *= size == "small" ? 0.8f : size == "large" ? 1.25f : size == "huge" ? 1.6f : 1.0f;
    k.gap *= size == "huge" ? 1.3f : 1.0f;
    const std::string pos = c.GetString("audio.lyricsPosition");
    if (pos == "top") k.anchor = 0.22f;
    else if (pos == "center") k.anchor = 0.45f;
    else if (pos == "bottom") k.anchor = 0.7f;
    const std::string lines = c.GetString("audio.lyricsLines");
    if (lines == "all") k.before = k.after = -1;
    else if (lines == "near") k.before = k.after = 2;
    else if (lines == "next") k.before = 0, k.after = 1;
    else if (lines == "none") k.before = k.after = 0;
    const std::string align = c.GetString("audio.lyricsAlign");
    if (align != "auto") k.center = align == "center";
    const std::string fill = c.GetString("audio.lyricsKaraoke");
    if (fill != "auto") k.fill = fill == "on";
    return k;
}

// Under lyrics that came from LRCLIB: the source, and a click saves them as .lrc next to the song
// (the only way they reach the song's folder). Light on the always-dark music view.
class LyricsSourceButton : public Widget {
public:
    LyricsSourceButton() {
        interactive = true;
        focusable = true;
    }
    void Paint(Painter& p) override {
        tooltip = Tr(L"Lyrics from LRCLIB. Save them as an .lrc file next to the song");  // after a language switch too
        const Color white = Color::Hex(0xFFFFFF);
        const bool hot = IsHot() || IsPressed();
        if (hot) p.Fill(rect, white.WithAlpha(IsPressed() ? 0.14f : 0.08f), 4);
        const Color c = white.Fade(hot ? 0.9f : 0.55f);
        p.Icon("save", {rect.x + 6, rect.Cy() - 10, 20, 20}, c);
        p.Text(Tr(L"LRCLIB · Save .lrc"), {rect.x + 32, rect.y, rect.w - 38, rect.h}, TextStyle::Small, c);
        PaintFocusRing(p, rect, 4);
    }
    float Width() const { return std::ceil(gfx::Text::Get().Measure(Tr(L"LRCLIB · Save .lrc"), TextStyle::Small).width) + 44; }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key != VK_RETURN && e.key != VK_SPACE) return false;
        OnClick();
        return true;
    }
};

// "Lyrics" and "Queue" under the title: the music view's own way to them, larger than the bar's
// icons. Translucent white over the blurred cover (qa/visual-rules.md, rule 1); solid white with
// dark text while what it opens is shown, so on and off differ at a glance; faint when there is
// nothing to show.
class MusicPill : public Widget {
public:
    MusicPill(const char* icon, const wchar_t* label) : icon_(icon), label_(label) {
        interactive = true;
        focusable = true;
    }
    bool active = false;
    bool dim = false;
    void Paint(Painter& p) override {
        const Color white = Color::Hex(0xFFFFFF);
        const bool hot = IsHot() || IsPressed();
        if (active) {
            p.Fill(rect, white.Fade(IsPressed() ? 0.8f : hot ? 0.86f : 0.94f), rect.h / 2);
        } else {
            p.Fill(rect, white.Fade(IsPressed() ? 0.26f : hot ? 0.16f : dim ? 0.06f : 0.1f), rect.h / 2);
        }
        const Color c = active ? Color::Hex(0x000000, 0.88f) : white.Fade(hot ? 0.95f : dim ? 0.42f : 0.72f);
        p.Icon(icon_, {rect.x + 12, rect.Cy() - 9, 18, 18}, c, c);  // two-tone parts too: accentMid is opaque
        p.Text(Tr(label_), {rect.x + 36, rect.y, rect.w - 46, rect.h}, TextStyle::BodyMedium, c);
        PaintFocusRing(p, rect, rect.h / 2);
    }
    float Width() const { return std::ceil(gfx::Text::Get().Measure(Tr(label_), TextStyle::BodyMedium).width) + 50; }
    void Describe(A11y& a) override { a.toggle = active ? 1 : 0; }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key != VK_RETURN && e.key != VK_SPACE) return false;
        OnClick();
        return true;
    }

private:
    const char* icon_;
    const wchar_t* label_;
};

class MusicView;
MusicView* g_musicView = nullptr;  // the one music view (ShowMusicLyrics)

class MusicView : public Widget {
public:
    MusicView() {
        g_musicView = this;
        // Always takes the mouse: a double click toggles fullscreen and a move brings the bar back
        // there, as over a video (with lyrics their lines take clicks too).
        interactive = true;
        // The view covers the video area: without this a right click on it opened nothing (the
        // lyrics keep their own menu).
        onContextMenu = [this](const MouseEvent& e) { ShowPlayerContextMenu(GetHost(), {e.x, e.y}); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) RefreshLyrics();
            if (m & kChangeTime) FollowTime();
            if (m & (kChangeMeta | kChangeFile | kChangeState)) Invalidate();
        });
        vizToken_ = TheApp().config().Subscribe("audio.visualizer", [this](const std::string&) {
            ReadVisualizerSetting();
            Invalidate();
        });
        ReadVisualizerSetting();
        look_ = base_ = ReadLyricsLook(TheApp().config());
        lyricsToken_ = TheApp().config().Subscribe("audio.lyrics", [this](const std::string&) {
            look_ = base_ = ReadLyricsLook(TheApp().config());
            fit_ = 1;
            format_.Reset();
            layoutW_ = -1;
            Invalidate();
        });
        imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
        // Hover target for the full title when it is cut (placed by Paint, where the text goes).
        titleTip_ = Add<Widget>();
        titleTip_->interactive = true;
        titleTip_->visible = false;
        source_ = Add<LyricsSourceButton>();
        source_->visible = false;
        source_->onClick = [this] { SaveOnlineLyrics(); };
        lyricsBtn_ = Add<MusicPill>("subtitle-2", L"Lyrics");
        lyricsBtn_->id = "music-lyrics";
        lyricsBtn_->onClick = [this] { LyricsClicked(); };
        queueBtn_ = Add<MusicPill>("list", L"Queue");
        queueBtn_->id = "music-queue";
        // No anchor: the panel opens at the right edge, where Place() makes room for it. Anchored
        // to this pill it covered the cover (or the meters) and the title, and the view still
        // moved them aside for a panel on the right.
        queueBtn_->onClick = [] { TogglePlayerPanel(PanelKind::Queue, {}); };
    }
    ~MusicView() override {
        if (g_musicView == this) g_musicView = nullptr;
        TheApp().config().Unsubscribe(vizToken_);
        TheApp().config().Unsubscribe(lyricsToken_);
        AudioFeed::Get().Detach(this);
        *alive_ = false;
        net::Cancel(onlineReq_);
    }
    void OnLayout() override {}  // titleTip_, source_ and the pills are placed by Paint
    // From the mini player's Lyrics button: a song without lyrics opens the "no lyrics" card
    // once the pill has a place (the next paint), so the click never looks like it did nothing.
    void ShowLyrics() {
        lyricsOn_ = true;
        noLyricsCardPending_ = true;
        Invalidate();
    }

    void DebugInfo(std::string& out) const override {
        Widget::DebugInfo(out);
        if (style_ == VizStyle::Off) return;
        float top = 0;
        for (float b : frame_.bands) top = std::max(top, b);
        char text[160];
        snprintf(text, sizeof text, " viz=%d frames=%llu captured=%llu cpu=%.0fms level=%.2f top=%.2f%s%s",
                 static_cast<int>(style_), static_cast<unsigned long long>(frame_.serial),
                 static_cast<unsigned long long>(AudioFeed::Get().Captured()), AudioFeed::Get().CpuMs(), frame_.level,
                 top, AudioFeed::Get().Failed() ? " failed" : "", AudioFeed::Get().Stalled() ? " parked" : "");
        out += text;
    }

    // Where the cover and its text go: the view's rect less an open popover, the left half with
    // wide lyrics.
    struct Geometry {
        Rect area, left, cover;
        Rect title;             // the title line; the artist line and the pills follow it
        Rect viz, viz2;         // the visualizer next to the cover (viz2: the right wing)
        Rect vu;                // the VU pair in place of the cover (empty: another style)
        bool beside = false;    // the cover stays and the visualizer has viz (and viz2)
        bool textLeft = false;  // the text starts at title.x instead of centering under the cover
        bool shown = false, wide = false, oneLine = false;
        bool full = false;      // lyrics only: no cover, the lyrics across the view
        // The text under the cover grows with a tall view (1 up to 900 DIP, 1.5 from 1350): on a
        // large screen the title would look lost under the big cover or meters. The pills keep
        // their size: they are controls, like the bar's.
        float ts = 1;
    };
    Geometry Place() const {
        // An open popover (queue, tracks, settings) takes the right side: center the cover and
        // the text in what is left, when that is still wide enough.
        Rect area = rect;
        const bool panel = PlayerPanelOpen(PanelKind::Queue) || PlayerPanelOpen(PanelKind::Tracks) ||
                           PlayerPanelOpen(PanelKind::Subtitles) || PlayerPanelOpen(PanelKind::Settings) ||
                           PlayerPanelOpen(PanelKind::Video);
        // 320 leaves a usable cover next to the compact library too (844 - 420 DIP at 1280 wide).
        if (panel && rect.w - (panel::kPanelW + panel::kGap) >= 320) area.w -= panel::kPanelW + panel::kGap;
        Geometry g;
        g.ts = std::clamp(rect.h / 900.0f, 1.0f, 1.5f);
        const float ts = g.ts;
        // With lyrics in a wide view the cover and its text take the left half.
        g.shown = lyricsOn_ && !lyrics_.Empty();
        g.wide = g.shown && area.w >= kWideLyrics;
        g.oneLine = g.shown && !g.wide && lyrics_.synced;
        if (g.shown && look_.only) {
            g.full = true;
            g.wide = g.oneLine = false;
        }
        const float extra = (g.oneLine ? 32.0f * ts : 0.0f) + kPillsH;
        g.area = area;
        g.left = area;
        if (g.wide) g.left.w = std::floor(area.w * 0.5f);
        // Layout: cover centered, text 88 DIP under it (1920×1080: 738 cover, 73 top margin).
        const float side =
            std::floor(std::max(96.0f, std::min({g.left.w - 96, rect.h - 62 - (88 + 30) * ts - extra, g.wide ? 620.0f : 738.0f})));
        const float block = side + (88 + 30) * ts + extra;
        const float top = rect.y + std::max(24.0f, (rect.h - block) / 2);
        g.cover = {std::round(g.left.Cx() - side / 2), std::round(top), side, side};
        g.title = {g.left.x + 48, g.cover.Bottom() + 70 * ts, g.left.w - 96, 36 * ts};
        const VizLayout layout =
            g.shown || style_ == VizStyle::Off || style_ == VizStyle::Spectrometer ? VizLayout::Replace : layout_;
        if (layout == VizLayout::Replace && style_ == VizStyle::Vu) {
            // The meters take the cover's place in their own shape: as wide as the view and as
            // tall as its height allow, with no 738 cap, so they keep growing on a large screen.
            // The title goes right under them and the whole block is centred, so a squarish
            // window leaves no hole between the needles and the title. The cover (on a pause)
            // sits in the middle of the same box, no taller than it.
            const float room = rect.h - 62 - (88 + 30) * ts - extra;  // 180 at ts 1, as the cover's
            const Rect box = VizPaint::VuPairBox({0, 0, g.left.w - 96, std::max(96.0f, room)});
            if (!box.Empty()) {
                const float bh = std::max(box.h, 96.0f);
                const float t = rect.y + std::max(24.0f, (rect.h - (bh + (88 + 30) * ts + extra)) / 2);
                g.vu = {std::round(g.left.Cx() - box.w / 2), std::round(t), box.w, bh};
                const float cs = std::floor(std::min(side, bh));
                g.cover = {std::round(g.left.Cx() - cs / 2), std::round(t + (bh - cs) / 2), cs, cs};
                g.title = {g.left.x + 48, g.vu.Bottom() + 70 * ts, g.left.w - 96, 36 * ts};
            }
        } else if (layout == VizLayout::Wings) {
            // Mirrored bars grow out of the cover's sides, the low notes next to it.
            const float wing = std::floor((area.w - side) / 2 - 64);
            if (wing >= 120) {
                g.beside = true;
                g.viz = {g.cover.x - 24 - wing, g.cover.y, wing, side};
                g.viz2 = {g.cover.Right() + 24, g.cover.y, wing, side};
            }
        } else if (layout == VizLayout::Side) {
            // The cover and its text take the right 40 %, the visualizer the rest at the cover's height.
            const float colW = std::floor(area.w * 0.4f);
            const float s2 = std::floor(std::max(96.0f, std::min({colW - 96, rect.h - 62 - (88 + 30) * ts - extra, 560.0f})));
            if (area.w - colW >= 320) {
                g.beside = true;
                g.left = {area.Right() - colW, area.y, colW, area.h};
                const float t2 = rect.y + std::max(24.0f, (rect.h - (s2 + (88 + 30) * ts + extra)) / 2);
                g.cover = {std::round(g.left.Cx() - s2 / 2), std::round(t2), s2, s2};
                g.title = {g.left.x + 24, g.cover.Bottom() + 70 * ts, g.left.w - 48, 36 * ts};
                g.viz = {area.x + 64, g.cover.y, g.left.x - area.x - 96, s2};
            }
        } else if (layout == VizLayout::Below) {
            // As in AIMP: a small cover with the text beside it on top, the visualizer across the rest.
            const float s2 = std::floor(std::clamp(rect.h * 0.28f, 120.0f, 280.0f));
            const float x = area.x + 64, y = rect.y + 48;
            const Rect viz{x, y + s2 + 48, area.w - 128, rect.Bottom() - 40 - (y + s2 + 48)};
            if (viz.h >= 120 && area.w >= 560) {
                g.beside = g.textLeft = true;
                g.ts = 1;  // beside a small cover
                g.cover = {x, y, s2, s2};
                const float textH = 36 + 22 + kPillsH;
                g.title = {x + s2 + 32, std::round(y + (s2 - textH) / 2), area.Right() - 64 - (x + s2 + 32), 36};
                g.viz = viz;
            }
        }
        return g;
    }

    static std::wstring CoverPath() {
        const QueueItem* item = TheApp().queue().CurrentItem();
        return item ? item->path : TheApp().player().State().path;
    }

    // The cover this view would paint now is decoded, or the file has none. Asks for it like a
    // paint does, so the player opened by App::CheckPlayerHold shows it in its first frame.
    bool CoverReady() {
        const std::wstring path = CoverPath();
        if (path.empty() || !GetHost()) return true;
        const int px = static_cast<int>(Place().cover.w * GetHost()->Scale());
        ImageCache& images = TheApp().images();
        return images.Get(path, px) || !images.Loading(path, px);
    }

    void Paint(Painter& p) override {
        TakeLoadedLyrics();
        const Palette& c = P();
        p.Fill(rect, c.videoBg);
        const PlayerState& s = TheApp().player().State();
        const QueueItem* item = TheApp().queue().CurrentItem();
        const std::wstring path = CoverPath();
        const Geometry g = Place();
        const Rect& area = g.area;
        const Rect& left = g.left;
        const Rect& cover = g.cover;
        const bool shown = g.shown, wide = g.wide, oneLine = g.oneLine;

        ID2D1Bitmap* bmp = nullptr;
        float appear = 1;
        bool loading = false;
        if (!path.empty() && GetHost()) {
            const int px = static_cast<int>(cover.w * GetHost()->Scale());
            ImageCache& images = TheApp().images();
            bmp = images.Get(path, px);
            appear = images.Appear(path, px);
            // On its way: neither the stand-in art nor its glow, so the cover never replaces them.
            loading = !bmp && images.Loading(path, px);
        }
        // The stand-in art of a file without a cover fades in when the lookup ends, as a cover does.
        if (loading) {
            artWaited_ = true;
            art_.Set(0);
        } else if (artWaited_) {
            artWaited_ = false;
            art_.To(1.0f, 140);
        }
        if (!bmp) appear = std::min(appear, art_.Get());

        p.PushClip(rect);
        p.PushOpacity(appear);
        if (bmp) PaintBackdrop(p, bmp, cover);
        else if (!loading) PaintGlow(p, cover);
        p.PopOpacity();
        p.PopClip();

        lyricsBtn_->visible = queueBtn_->visible = !g.full;
        if (g.full) {
            PaintLyricsOnly(p, g);
            return;
        }
        FitLyrics(1);
        const float viz = UpdateVisualizer(s);
        const float gone = g.beside ? 0.0f : viz;  // how far the cover has made way
        if (gone < 1 && !loading) {
            p.PushOpacity((1 - gone) * appear);
            p.Shadow(cover, 16, 40, 12, 0.55f * (1 - gone) * appear);
            if (bmp) {
                p.Image(bmp, cover, 16, true);
                p.ImageOutline(cover, 16);
            } else {
                PaintGeneratedArt(p, cover, ArtSeed(path));
            }
            p.PopOpacity();
        }
        // Beside the cover the band stays in place at rest (a pause, before the first frame): one
        // dim row instead of an empty half of the view.
        if (g.beside && viz < 1) {
            p.PushOpacity((1 - viz) * 0.6f);
            PaintBand(p, g.viz, g.viz2.w > 0, true);
            if (g.viz2.w > 0) PaintBand(p, g.viz2, false, true);
            p.PopOpacity();
        }
        if (viz > 0) {
            p.PushOpacity(viz);
            if (!g.beside) {
                PaintVisualizer(p, g, bmp);
            } else {
                PaintBand(p, g.viz, g.viz2.w > 0, false);
                if (g.viz2.w > 0) PaintBand(p, g.viz2, false, false);
            }
            p.PopOpacity();
        }

        // Two lines, as in the design: the title, then the artist and the album. A long title is
        // cut on its own line and never hides the artist; the full text is in a tooltip.
        const PlayingTags& tags = CurrentTags();  // the library's tags when it knows the file
        const std::wstring title = tags.title.empty() ? (item ? FileNameOf(item->path) : L"") : tags.title;
        std::wstring second = tags.artist;
        if (!tags.album.empty()) second += (second.empty() ? L"" : L" · ") + tags.album;
        // The view is always on the (dark) video background, also in the light theme, so the
        // text is light rather than the palette's text colors.
        const Color white = Color::Hex(0xFFFFFF);
        const Rect titleBox = g.title;
        const float ts = g.ts;
        const Rect secondBox{titleBox.x, titleBox.y + 36 * ts, titleBox.w, 22 * ts};
        const HAlign textAlign = g.textLeft ? HAlign::Leading : HAlign::Center;
        // A style drawn `ts` times larger in `box` (Geometry::ts).
        auto scaled = [&](const std::wstring& text, const Rect& box, TextStyle style, Color c) {
            if (ts == 1) {
                p.Text(text, box, style, c, textAlign);
                return;
            }
            D2D1_MATRIX_3X2_F old;
            p.Ctx()->GetTransform(&old);
            const D2D1_POINT_2F at{textAlign == HAlign::Center ? box.Cx() : box.x, box.y};
            p.Ctx()->SetTransform(D2D1::Matrix3x2F::Scale(ts, ts, at) * old);
            const float w = box.w / ts;
            p.Text(text, {textAlign == HAlign::Center ? box.Cx() - w / 2 : box.x, box.y, w, box.h / ts}, style, c,
                   textAlign);
            p.Ctx()->SetTransform(old);
        };
        scaled(title, titleBox, TextStyle::Title, white.Fade(0.8f));
        if (!second.empty()) scaled(second, secondBox, TextStyle::Body, white.Fade(0.55f));
        const bool cut = p.Measure(title, TextStyle::Title).width * ts > titleBox.w ||
                         (!second.empty() && p.Measure(second, TextStyle::Body).width * ts > secondBox.w);
        titleTip_->visible = cut;
        if (cut) {
            titleTip_->rect = {titleBox.x, titleBox.y, titleBox.w, second.empty() ? titleBox.h : secondBox.Bottom() - titleBox.y};
            titleTip_->tooltip = second.empty() ? title : title + L"\n" + second;
        }

        // The pills: under the artist, or under the sung line when it shows there.
        const float textEnd =
            titleBox.y + ((oneLine ? (second.empty() ? 130.0f : 162.0f) : second.empty() ? 106.0f : 128.0f) - 70) * ts;
        lyricsBtn_->active = shown;
        lyricsBtn_->dim = lyrics_.Empty() && !pending_ && !onlineReq_;
        queueBtn_->active = PlayerPanelOpen(PanelKind::Queue);
        const float lw = lyricsBtn_->Width(), qw = queueBtn_->Width();
        const float px = g.textLeft ? titleBox.x : std::round(left.Cx() - (lw + 10 + qw) / 2);
        const float py = std::round(textEnd + 16);
        lyricsBtn_->rect = {px, py, lw, kPillsH - 16};
        queueBtn_->rect = {px + lw + 10, py, qw, kPillsH - 16};
        if (noLyricsCardPending_ && !pending_ && !onlineReq_) {
            noLyricsCardPending_ = false;
            if (lyrics_.Empty())
                if (ui::Host* h = GetHost()) h->Post([this, alive = alive_] { if (*alive) LyricsClicked(); });
        }

        source_->visible = false;
        if (wide) {
            const float x = left.Right() + 16;
            box_ = {x, rect.y + 48, std::min(area.Right() - 48 - x, 640.0f), rect.h - 96};
            if (online_) {
                // The column gives its last 40 DIP to the source line.
                box_.h -= 40;
                source_->visible = true;
                source_->rect = {box_.x, box_.Bottom() + 8, std::min(source_->Width(), box_.w), 28};
            }
            PaintLyrics(p, box_);
        } else {
            box_ = {};
            std::fill(lineRects_.begin(), lineRects_.end(), Rect{});
            if (oneLine && current_ >= 0)
                scaled(lyrics_.lines[static_cast<size_t>(current_)].text,
                       {left.x + 48, g.title.y + (second.empty() ? 36.0f : 68.0f) * ts, left.w - 96, 24 * ts},
                       TextStyle::Large, white.Fade(0.9f));
        }
    }

    // Lyrics only: over the blurred cover, the title as one dim line on top and the lyrics in a
    // column across the rest. No visualizer: the capture rests.
    void PaintLyricsOnly(Painter& p, const Geometry& g) {
        titleTip_->visible = false;
        source_->visible = false;
        const Rect& area = g.area;
        const Rect head{area.x + 48, rect.y + 28, area.w - 96, 24};
        // Fullscreen has its own title panel at the top; it would sit on this line.
        const bool fullscreen = TheApp().main() && TheApp().main()->IsFullscreen();
        if (!fullscreen) {
            const QueueItem* item = TheApp().queue().CurrentItem();
            const PlayingTags& tags = CurrentTags();
            std::wstring line = tags.title.empty() ? (item ? FileNameOf(item->path) : L"") : tags.title;
            if (!tags.artist.empty()) line += L" \u00B7 " + tags.artist;
            p.Text(line, head, TextStyle::Body, Color::Hex(0xFFFFFF).Fade(0.55f), HAlign::Center);
        }
        // Letters grow with the view (a TV across the room), from the look's size at 700 DIP high.
        FitLyrics(std::clamp(rect.h / 700.0f, 1.0f, 2.4f));
        const float w = std::min(area.w - 96, 1400.0f * fit_);
        box_ = {std::round(area.Cx() - w / 2), head.Bottom() + 24, w, rect.Bottom() - 40 - (head.Bottom() + 24)};
        PaintLyrics(p, box_);
    }

    // The look's letters and gaps at `f` times their size; the line layouts are made again.
    void FitLyrics(float f) {
        if (f == fit_) return;
        fit_ = f;
        look_ = base_;
        look_.size *= f;
        look_.gap *= f;
        format_.Reset();
        layoutW_ = -1;
    }

    bool OnMouseMove(const MouseEvent& e) override {
        if (MainWindow* w = TheApp().main(); w && w->BarOverlays()) w->PointerMoved(e.x, e.y);
        const int hit = LineAt(e.x, e.y);
        if (hit != hovered_) {
            hovered_ = hit;
            cursor = hit >= 0 ? IDC_HAND : IDC_ARROW;
            Invalidate();
        }
        return hit >= 0;
    }
    void OnMouseLeave() override {
        if (hovered_ < 0) return;
        hovered_ = -1;
        cursor = IDC_ARROW;
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        // Right click on the lyrics: their style (and the save command for lyrics from the
        // internet); elsewhere the player's menu.
        if (e.button == MouseButton::Right && box_.w > 0 && box_.Contains(e.x, e.y) && GetHost()) {
            std::vector<MenuItem> items = LyricsStyleItems();
            items.push_back(MenuItem::Separator());
            if (lyrics_.synced) {
                // For an .lrc that runs ahead of the song or behind it; remembered for the file.
                const double d = Live().LyricsDelay();
                MenuItem earlier;
                earlier.label = Tr(L"Show lyrics 0.5 s earlier");
                earlier.icon = "arrow_up";
                earlier.action = [this, d] { ShiftLyrics(d - 0.5); };
                items.push_back(std::move(earlier));
                MenuItem later;
                later.label = Tr(L"Show lyrics 0.5 s later");
                later.icon = "arrow_down";
                later.action = [this, d] { ShiftLyrics(d + 0.5); };
                items.push_back(std::move(later));
                if (std::abs(d) >= 0.0005) {
                    MenuItem reset;
                    reset.label = Fmt(Tr(L"Reset lyrics timing ({0})"), {ShiftText(d)});
                    reset.icon = "refresh";
                    reset.action = [this] { ShiftLyrics(0); };
                    items.push_back(std::move(reset));
                }
                items.push_back(MenuItem::Separator());
            }
            MenuItem only;
            only.label = Tr(L"Lyrics only, without the cover");
            only.checked = look_.only;
            only.action = [on = !look_.only] { TheApp().config().SetBool("audio.lyricsOnly", on); };
            items.push_back(std::move(only));
            MenuItem more;
            more.label = Tr(L"Lyrics settings…");
            more.icon = "settings";
            more.action = [] { TheApp().OpenSettings("audio"); };
            items.push_back(std::move(more));
            if (online_) {
                MenuItem save;
                save.label = Tr(L"Save lyrics as .lrc next to the song");
                save.icon = "save";
                save.action = [this] { SaveOnlineLyrics(); };
                items.push_back(std::move(save));
            }
            ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
            return true;
        }
        if (e.button == MouseButton::Right && GetHost()) {
            ShowPlayerContextMenu(GetHost(), {e.x, e.y});
            return true;
        }
        if (e.button != MouseButton::Left) return false;
        const int hit = LineAt(e.x, e.y);
        if (hit < 0) return false;
        TheApp().player().SeekAbsolute(lyrics_.lines[static_cast<size_t>(hit)].time + Live().LyricsDelay(), true);
        ClearHover();  // the column glides to the line: no second highlight next to the sung one
        return true;
    }
    // Settings > Controls > Mouse, as on a video: fullscreen by default. On a lyrics line the second
    // click seeks there again, like the first.
    bool OnDoubleClick(const MouseEvent& e) override {
        if (LineAt(e.x, e.y) >= 0) return OnMouseDown(e);
        VideoDoubleClick();
        return true;
    }
    bool OnWheel(const WheelEvent& e) override {
        if (lyrics_.synced || lyrics_.Empty() || e.horizontal || !box_.Contains(e.x, e.y)) return false;
        const float maxScroll = std::max(0.0f, totalH_ - box_.h);
        const float to = std::clamp(staticScroll_ - e.delta * 64, 0.0f, maxScroll);
        if (to == staticScroll_) return false;
        staticScroll_ = to;
        Invalidate();
        return true;
    }

private:
    // --- visualizer -----------------------------------------------------------------------
    void ReadVisualizerSetting() {
        layout_ = VizLayoutOf(TheApp().config().GetString("audio.visualizerLayout"));
        style_ = audio::ProcessLoopbackSupported() ? VizStyleOf(TheApp().config().GetString("audio.visualizer"))
                                                   : VizStyle::Off;
        // The shared capture runs for this view only while a visualization is chosen.
        if (style_ != VizStyle::Off) {
            AudioFeed::Get().Attach(this);
        } else {
            AudioFeed::Get().Detach(this);
            vizShow_.Set(0);
        }
    }

    // Runs the capture while the song plays and this view is drawn; returns how far the cover
    // has given way to the visualizer (0..1).
    float UpdateVisualizer(const PlayerState& /*s*/) {
        if (style_ == VizStyle::Off) return 0;
        AudioFeed& feed = AudioFeed::Get();
        feed.Pull(this);
        frame_ = feed.Frame();
        const bool show = feed.IsLive();
        if (style_ == VizStyle::Vu) {
            if (viz_.UpdateNeedles(frame_, show, feed.Reference()) && !show) Invalidate();  // falling back to the stop after a pause
        }
        const float target = show ? 1.0f : 0.0f;
        if (vizShow_.Target() != target) vizShow_.To(target, 220, Ease::InOut);
        return vizShow_.Get();
    }

    ID2D1StrokeStyle* RoundStroke(ID2D1DeviceContext5* ctx) { return viz_.RoundStroke(ctx); }
    ID2D1SolidColorBrush* LineBrush(ID2D1DeviceContext5* ctx, Color c, float opacity) {
        return viz_.LineBrush(ctx, c, opacity);
    }

    // Monochrome on the video background, like the title (the view is dark in both themes).
    void PaintVisualizer(Painter& p, const Geometry& g, ID2D1Bitmap* bmp) {
        const Rect& left = g.left;
        const Rect& cover = g.cover;
        const Color white = Color::Hex(0xFFFFFF);
        const Color fg = white.Fade(0.86f), dim = white.Fade(0.4f);
        // The bar styles take a wider band than the square cover, at the same height, so the
        // title below stays where it was. The VU pair has its own box (Place).
        const float w = std::floor(std::min(left.w - 96, cover.w * 1.5f));
        const Rect band = style_ == VizStyle::Vu && !g.vu.Empty() ? g.vu : Rect{std::round(left.Cx() - w / 2), cover.y, w, cover.h};
        switch (style_) {
            case VizStyle::Spectrum: viz_.Spectrum(p, band, frame_, white, false, false); break;
            case VizStyle::Waveform: viz_.Waveform(p, band, frame_, white); break;
            case VizStyle::Scope: viz_.Scope(p, band, frame_, fg, dim); break;
            case VizStyle::Spectrometer: PaintSpectrometer(p, cover, bmp, fg, dim); break;
            case VizStyle::Vu: viz_.VuPair(p, band, false, white); break;
            case VizStyle::Off: break;
        }
    }

    // A bar-like visualizer in its own rect next to the cover. `leftWing`: the left one of two
    // mirrored wings, so the low notes sit next to the cover on both sides. `rest`: silence.
    void PaintBand(Painter& p, const Rect& r, bool leftWing, bool rest) {
        const Color white = Color::Hex(0xFFFFFF);
        if (rest && style_ != VizStyle::Spectrum && style_ != VizStyle::Vu) {
            p.Fill({r.x, std::round(r.Cy()), r.w, 1}, white.Fade(0.3f));
            return;
        }
        switch (style_) {
            case VizStyle::Spectrum: viz_.Spectrum(p, r, frame_, white, leftWing, layout_ == VizLayout::Wings, rest); break;
            case VizStyle::Waveform: viz_.Waveform(p, r, frame_, white); break;
            case VizStyle::Scope: viz_.Scope(p, r, frame_, white.Fade(0.86f), white.Fade(0.4f)); break;
            case VizStyle::Vu:
                // Wings: the left meter on the left of the cover, the right one on its right.
                if (layout_ == VizLayout::Wings) viz_.VuMeter(p, VizPaint::FitVu(r), leftWing ? 0 : 1, rest, white);
                else viz_.VuPair(p, r, rest, white);
                break;
            default: break;
        }
    }

    // Spokes around a round copy of the cover: the bands go down one side and back up the other.
    void PaintSpectrometer(Painter& p, const Rect& cover, ID2D1Bitmap* bmp, Color fg, Color dim) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const Point c{cover.Cx(), cover.Cy()};
        const float outer = cover.w / 2;
        const float inner = outer * 0.46f * (1 + 0.03f * frame_.level);
        const float maxLen = outer * 0.5f;
        constexpr int n = audio::kBands;
        const float thick = std::clamp(2 * 3.14159265f * inner / (2 * n) * 0.45f, 2.0f, 6.0f);
        ID2D1StrokeStyle* stroke = RoundStroke(ctx);
        ID2D1SolidColorBrush* brush = LineBrush(ctx, fg, p.Opacity());
        for (int k = 0; brush && k < 2 * n; ++k) {
            const int band = k < n ? k : 2 * n - 1 - k;
            const float a = -1.5707963f + 3.14159265f * (static_cast<float>(k) + 0.5f) / n;
            const float dx = std::cos(a), dy = std::sin(a);
            const float r0 = inner + 6 + thick / 2, len = std::max(1.0f, frame_.bands[band] * maxLen);
            ctx->DrawLine({c.x + dx * r0, c.y + dy * r0}, {c.x + dx * (r0 + len), c.y + dy * (r0 + len)}, brush, thick,
                          stroke);
        }
        const Rect disc{c.x - inner, c.y - inner, inner * 2, inner * 2};
        if (bmp) {
            p.Image(bmp, disc, inner, true);
        } else {
            p.FillCircle(c, inner, Color::Hex(0xFFFFFF, 0.12f));  // the stand-in art's tones (PaintGeneratedArt)
            const float icon = inner * 0.5f;
            p.Icon("music", {c.x - icon / 2, c.y - icon / 2, icon, icon}, Color::Hex(0xFFFFFF, 0.55f));
        }
        if (brush) ctx->DrawEllipse(D2D1::Ellipse({c.x, c.y}, inner + 0.5f, inner + 0.5f), LineBrush(ctx, dim, p.Opacity()), 1.0f);
    }


    // --- lyrics ---------------------------------------------------------------------------
    // A new file: forget the old lyrics and look for a .lrc on a worker. The embedded tag is
    // read whenever the metadata changes until the track has lyrics (tags may arrive after
    // MPV_EVENT_FILE_LOADED); a .lrc, usually synced, wins over the tag.
    void RefreshLyrics() {
        const PlayerState& s = TheApp().player().State();
        if (s.loadSerial != serial_) {
            serial_ = s.loadSerial;
            pending_.reset();
            net::Cancel(onlineReq_);
            onlineReq_ = 0;
            onlineSerial_ = 0;
            online_ = false;
            onlineAnswer_ = {};
            SetLyrics({});
            if (!s.fileLoaded) return;
            const QueueItem* item = TheApp().queue().CurrentItem();
            const std::wstring media = item ? item->path : s.path;
            // Songs always ask the worker: without a .lrc it reads SYLT/USLT with our tag parser,
            // which mpv's metadata list below lacks (it has no synced SYLT). Other files only
            // look for a .lrc: parsing a film's tags for lyrics is wasted work.
            const bool song = library::KindOf(media) == library::MediaKind::Audio;
            const std::wstring lrc = LyricsPathFor(media);
            if (song || !lrc.empty()) {
                auto job = std::make_shared<LyricsLoad>();
                pending_ = job;
                const HWND hwnd = GetHost() ? GetHost()->Hwnd() : nullptr;
                std::thread([job, media, lrc, song, hwnd] {
                    // The .lrc, else (songs) SYLT / USLT from the file.
                    Lyrics l = song ? LoadLyricsFor(media) : LoadLyricsFile(lrc);
                    {
                        std::lock_guard lock(job->mu);
                        job->lyrics = std::move(l);
                        job->done = true;
                    }
                    if (hwnd) PostMessageW(hwnd, ui::Host::kRepaintMessage, 0, 0);
                }).detach();
            }
        }
        // A tag that arrives late still wins over lyrics from the internet.
        if (s.fileLoaded && (lyrics_.Empty() || online_)) ReadEmbeddedLyrics();
        FetchOnline();
    }

    // No .lrc and no tag (the local sources answered first): ask LRCLIB, when the user allowed it.
    void FetchOnline() {
        const PlayerState& s = TheApp().player().State();
        if (!s.fileLoaded || !lyrics_.Empty() || pending_ || onlineSerial_ == serial_ ||
            (!net::online::LyricsOn() && !asked_))
            return;
        const QueueItem* item = TheApp().queue().CurrentItem();
        net::online::TrackInfo t;
        t.path = item ? item->path : s.path;
        if (library::KindOf(t.path) != library::MediaKind::Audio) return;
        const PlayingTags& tags = CurrentTags();
        t.artist = tags.artist;
        t.title = tags.title;
        t.album = tags.album;
        t.duration = s.duration;
        const uint64_t serial = serial_;
        auto alive = alive_;
        onlineReq_ = net::online::FetchLyrics(t, [this, alive, serial, t](const net::LyricsAnswer& a) {
            if (!*alive || serial != serial_) return;
            onlineReq_ = 0;
            const bool asked = std::exchange(asked_, false);
            Lyrics l = a.found && lyrics_.Empty() ? ParseLyrics(a.text) : Lyrics{};
            if (l.Empty()) {
                if (asked && lyrics_.Empty())
                    ShowToast(TheApp().main(), Tr(L"LRCLIB has no lyrics for this song"), "info", 4000);
                return;
            }
            lyricsOn_ = true;
            onlineAnswer_ = a;
            onlineTrack_ = t;
            SetLyrics(std::move(l));
            online_ = true;
            Invalidate();
        }, asked_);
        // Asked; or nothing to ask with yet (tags may come with the next metadata change).
        if (onlineReq_) onlineSerial_ = serial_;
    }

    void SaveOnlineLyrics() {
        if (online_) net::online::SaveLyrics(onlineTrack_, onlineAnswer_, GetHost());
    }

    // With lyrics: show or hide them. Without: say so, and offer LRCLIB or a .lrc of the user's own.
    void LyricsClicked() {
        if (!lyrics_.Empty()) {
            lyricsOn_ = !lyricsOn_;
            Invalidate();
            return;
        }
        std::vector<MenuItem> items;
        MenuItem none;
        none.label = Tr(L"This song has no lyrics");
        none.icon = "info";
        none.enabled = false;
        none.note = true;
        items.push_back(std::move(none));
        // Why, in one line: where Moonbow looked.
        MenuItem why;
        why.label = !net::online::LyricsOn() ? Tr(L"No .lrc file, no lyrics in the tags, online search is off")
                    : onlineSerial_ == serial_ && !onlineReq_ ? Tr(L"No .lrc file, no lyrics in the tags or on LRCLIB")
                                                               : Tr(L"No .lrc file, no lyrics in the tags");
        why.enabled = false;
        why.note = true;
        items.push_back(std::move(why));
        items.push_back(MenuItem::Separator());
        MenuItem find;
        find.label = onlineReq_ ? Tr(L"Searching LRCLIB…") : Tr(L"Find online (LRCLIB)");
        find.icon = "search";
        find.enabled = !onlineReq_;
        find.action = [this] { FindOnline(); };
        items.push_back(std::move(find));
        MenuItem add;
        add.label = Tr(L"Add .lrc file…");
        add.icon = "add";
        add.action = [this] { AddLrcFile(); };
        items.push_back(std::move(add));
        ShowMenu(GetHost(), lyricsBtn_->rect, std::move(items), Place::Below, 240);
    }

    // LRCLIB is off until the user allows it (privacy.fetchLyrics): the first search asks. "Find now"
    // searches for this song only; "Always search" turns the setting on for the songs after it too.
    void FindOnline() {
        auto search = [this, alive = alive_] {
            if (!*alive) return;
            onlineSerial_ = 0;  // ask again, also for a song asked about before
            asked_ = true;
            FetchOnline();
            if (onlineReq_ || !lyrics_.Empty()) return;
            asked_ = false;
            ShowToast(TheApp().main(), Tr(L"Can't search: the song has no title or artist tag"), "info", 4000);
        };
        if (net::online::LyricsOn()) {
            search();
            return;
        }
        lib::ShowConfirm(Tr(L"Find lyrics online?"),
                         Tr(L"The song's artist, title, album and length are sent to lrclib.net. “Always search” "
                            L"also looks up the next songs without lyrics; you can turn it off in Settings › Privacy › Online services."),
                         Tr(L"Find now"), search, Tr(L"Always search"), [search] {
                             TheApp().config().SetBool("privacy.fetchLyrics", true);
                             search();
                         });
    }

    // A .lrc picked by hand shows now and is copied next to the song, so it is there next time
    // too (never over a file already there).
    void AddLrcFile() {
        const std::vector<std::wstring> picked = panel::PickFiles(3);
        const QueueItem* item = TheApp().queue().CurrentItem();
        const std::wstring media = item ? item->path : TheApp().player().State().path;
        if (picked.empty() || media.empty()) return;
        const std::wstring from = picked[0], to = LyricsPathFor(media);
        auto job = std::make_shared<LyricsLoad>();
        pending_ = job;
        asked_ = true;
        const HWND hwnd = GetHost() ? GetHost()->Hwnd() : nullptr;
        std::thread([job, from, to, hwnd] {
            Lyrics l = LoadLyricsFile(from);
            if (!l.Empty() && !to.empty() && _wcsicmp(from.c_str(), to.c_str()) != 0)
                CopyFileW(from.c_str(), to.c_str(), TRUE);
            {
                std::lock_guard lock(job->mu);
                job->lyrics = std::move(l);
                job->done = true;
            }
            if (hwnd) PostMessageW(hwnd, ui::Host::kRepaintMessage, 0, 0);
        }).detach();
    }

    void ReadEmbeddedLyrics() {
        for (const std::wstring& tag : TheApp().player().State().lyricsTags) {
            Lyrics l = ParseLyrics(tag);
            if (!l.Empty()) {
                SetLyrics(std::move(l));
                online_ = false;
                return;
            }
        }
    }

    void TakeLoadedLyrics() {
        if (!pending_) return;
        Lyrics l;
        {
            std::lock_guard lock(pending_->mu);
            if (!pending_->done) return;
            l = std::move(pending_->lyrics);
        }
        pending_.reset();
        if (!l.Empty()) {
            SetLyrics(std::move(l));
            online_ = false;
            lyricsOn_ = lyricsOn_ || std::exchange(asked_, false);
        } else if (std::exchange(asked_, false)) {
            ShowToast(TheApp().main(), Tr(L"No lyrics in this file"), "info", 4000);  // a .lrc picked by hand
        } else {
            FetchOnline();
        }
    }

    void SetLyrics(Lyrics l) {
        lyrics_ = std::move(l);
        layoutW_ = -1;
        lineRects_.clear();
        hovered_ = -1;
        cursor = IDC_ARROW;
        staticScroll_ = 0;
        current_ = lyrics_.LineAt(LyricsTime(TheApp().player().State().position) + kLead);
        scroll_.Set(static_cast<float>(current_));
        Invalidate();
    }

public:
    // For the lyrics window (MusicLyrics): what the view holds, and the line sung now.
    const Lyrics& LyricsNow(int* line) {
        TakeLoadedLyrics();
        if (line)
            *line = lyrics_.synced ? lyrics_.LineAt(LyricsTime(TheApp().player().State().position) + kLead) : -1;
        return lyrics_;
    }

private:

    // Playback moved: when another line starts, glide the column to it.
    void FollowTime() {
        TakeLoadedLyrics();
        const double pos = TheApp().player().State().position;
        if (!lyrics_.synced) return;
        const int line = lyrics_.LineAt(LyricsTime(pos) + kLead);
        if (line == current_) return;
        // Far jumps (a seek) move quicker than the next line coming up.
        const int dist = std::abs(line - current_);
        current_ = line;
        // The lines move under a still pointer; the next mouse move finds the new one.
        ClearHover();
        scroll_.To(static_cast<float>(line), dist > 3 ? 260.0f : 340.0f, Ease::InOut);
        Invalidate();
    }

    // Where the song is in the lyrics' own time: the file's lyrics shift taken off.
    static double LyricsTime(double position) { return position - Live().LyricsDelay(); }

    static std::wstring ShiftText(double d) {
        return (d > 0 ? L"+" : d < 0 ? L"−" : L"") + panel::FormatSeconds(std::abs(d), 1);
    }

    void ShiftLyrics(double d) {
        d = std::clamp(d, -30.0, 30.0);
        Live().SetLyricsDelay(d);
        FollowTime();
        Invalidate();
        ShowToast(TheApp().main(),
                  std::abs(d) < 0.0005 ? Tr(L"Lyrics timing reset") : Fmt(Tr(L"Lyrics shifted {0}"), {ShiftText(d)}),
                  "info", 2500);
    }

    // The playback position for a smooth fill: mpv reports it in steps, so a clock of our own
    // runs between the reports and is pulled toward them, never jumping back and forth.
    double SmoothTime() {
        const PlayerState& s = TheApp().player().State();
        const double now = AnimFloat::Now();
        const double dt = std::clamp((now - clockAt_) / 1000.0, 0.0, 0.1);
        clockAt_ = now;
        const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
        if (!playing || std::abs(s.position - clock_) > 0.5) {
            clock_ = s.position;  // paused, or a seek
        } else {
            clock_ += dt * s.speed;
            clock_ += (s.position - clock_) * 0.1;
        }
        return clock_;
    }

    void ClearHover() {
        if (hovered_ < 0) return;
        hovered_ = -1;
        cursor = IDC_ARROW;
    }

    // One layout per line in the look's font; made again only when the width, the look or the
    // lyrics change. Lines are narrower by the sung line's growth, so it still fits when grown.
    void EnsureLayout(float w) {
        if (w == layoutW_ && format_) return;
        layoutW_ = w;
        const size_t n = lyrics_.lines.size();
        tops_.assign(n, 0);
        heights_.assign(n, 0);
        lineRects_.assign(n, Rect{});
        layouts_.assign(n, nullptr);
        if (!format_) format_ = gfx::Text::Get().CustomFormat(look_.font, look_.size, look_.weight);
        if (!format_) return;
        format_->SetTextAlignment(look_.center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        const bool synced = lyrics_.synced;
        float y = 0;
        for (size_t i = 0; i < n; ++i) {
            // A pause in synced lyrics shows as dots; a stanza break in plain text stays blank.
            const std::wstring& t = lyrics_.lines[i].text;
            const std::wstring_view text = t.empty() && synced ? std::wstring_view(L"· · ·") : t;
            heights_[i] = std::round(look_.size * 0.8f);
            DWRITE_TEXT_METRICS m{};
            if (!text.empty() &&
                SUCCEEDED(gfx::Gpu::Get().DWrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                                                     format_.Get(), w / look_.grow, 100000,
                                                                     &layouts_[i])) &&
                SUCCEEDED(layouts_[i]->GetMetrics(&m)))
                heights_[i] = std::ceil(m.height);
            tops_[i] = y;
            y += heights_[i] + (synced ? look_.gap : 4.0f);
        }
        totalH_ = y;
    }

    // Center of line `f` in column space, interpolated between lines while scrolling; -1 is
    // a slot above the first line (before it is sung).
    float CenterAt(float f) const {
        const int n = static_cast<int>(tops_.size());
        auto center = [&](int i) {
            if (i < 0) return tops_[0] - 48.0f;
            const size_t k = static_cast<size_t>(std::min(i, n - 1));
            return tops_[k] + heights_[k] / 2;
        };
        const int i = static_cast<int>(std::floor(f));
        const float frac = f - static_cast<float>(i);
        return center(i) + (center(i + 1) - center(i)) * frac;
    }

    void PaintLyrics(Painter& p, const Rect& box) {
        if (box.w < 120 || box.h < 80) return;
        EnsureLayout(box.w);
        ID2D1DeviceContext5* ctx = p.Ctx();
        // Translucent white, never a grey premixed with videoBg: the blurred cover shows under the
        // text, and an opaque grey would sit on it as a dull patch (qa/visual-rules.md, rule 1).
        const Color white = Color::Hex(0xFFFFFF);
        const bool synced = lyrics_.synced;
        const LyricsLook& k = look_;
        const float at = scroll_.Get();
        // Synced: the sung line sits where the look says. Unsynced: from the top (centered when
        // it fits), moved only by the wheel.
        const float offset = synced ? box.y + box.h * k.anchor - CenterAt(at)
                                    : box.y + (totalH_ <= box.h ? (box.h - totalH_) / 2 : -staticScroll_);
        p.PushClip(box);
        const bool masked = PushEdgeMask(p, box);
        bool filling = false;
        for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const float lw = box.w / k.grow;
            const Rect r{k.center ? box.x + (box.w - lw) / 2 : box.x, std::round(offset + tops_[i]), lw, heights_[i]};
            lineRects_[i] = {};
            IDWriteTextLayout* layout = layouts_[i].Get();
            if (!layout || r.Bottom() < box.y || r.y > box.Bottom()) continue;
            float a = 0.72f, shown = 1, grow = 1;
            if (synced) {
                const int line = static_cast<int>(i);
                const float d = static_cast<float>(std::abs(line - current_));
                // Lines to come stay readable ahead; sung ones step back further.
                if (line == current_) a = k.sung;
                else if (line > current_) a = std::max(k.ahead - 0.22f, k.ahead - 0.05f * d);
                else a = std::max(k.past - 0.22f, k.past - 0.06f * d);
                if (line == hovered_) a = std::max(a, 0.78f);
                // Lines past the look's limit fade out while the column glides, not at once.
                const float rel = static_cast<float>(line) - at;
                const int limit = rel < 0 ? k.before : k.after;
                if (limit >= 0) shown = std::clamp(static_cast<float>(limit) + 1 - std::abs(rel), 0.0f, 1.0f);
                if (shown <= 0) continue;
                grow = 1 + (k.grow - 1) * std::max(0.0f, 1 - std::abs(rel));
                if (lyrics_.lines[i].time >= 0) lineRects_[i] = r;
            }
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            if (grow != 1)
                ctx->SetTransform(D2D1::Matrix3x2F::Scale(grow, grow, {k.center ? r.Cx() : r.x, r.Cy()}) * old);
            // Reduced motion: the sung line lights up whole, as the column jumps line by line
            // (AnimFloat::To does not glide then).
            if (synced && k.fill && static_cast<int>(i) == current_ && !AnimFloat::ReducedMotion()) {
                const double t = LyricsTime(SmoothTime());
                const double chars = lyrics_.SungChars(current_, t);
                const double sung = chars >= 0 ? 0 : lyrics_.SungFraction(current_, t);
                filling = chars >= 0 ? chars < static_cast<double>(lyrics_.lines[i].text.size()) : sung < 1;
                PaintFill(ctx, layout, {r.x, r.y}, static_cast<float>(sung), chars, white.Fade(k.ahead), a, shown);
            } else if (ID2D1SolidColorBrush* brush = LineBrush(ctx, white.Fade(a), shown)) {
                ctx->DrawTextLayout({r.x, r.y}, layout, brush);
            }
            ctx->SetTransform(old);
        }
        if (masked) ctx->PopLayer();
        p.PopClip();
        if (filling) Invalidate();  // the next frame moves the fill on
    }

    // A line lit (white at `lit`) from the left up to `sung` of its words, with a soft edge,
    // over its unlit color; wrapped parts fill one after another. With word stamps `chars` (>= 0)
    // says where the fill is instead: up to that character, lit through the word being sung.
    void PaintFill(ID2D1DeviceContext5* ctx, IDWriteTextLayout* layout, D2D1_POINT_2F o, float sung, double chars,
                   Color unlit, float lit, float opacity) {
        if (ID2D1SolidColorBrush* brush = LineBrush(ctx, unlit, opacity)) ctx->DrawTextLayout(o, layout, brush);
        ID2D1LinearGradientBrush* edge = FillBrush(ctx);
        if (!edge || (chars < 0 && sung <= 0) || chars == 0) return;
        edge->SetOpacity(lit * opacity);
        UINT32 n = 0;
        layout->GetLineMetrics(nullptr, 0, &n);
        std::vector<DWRITE_LINE_METRICS> lines(n);
        if (n == 0 || FAILED(layout->GetLineMetrics(lines.data(), n, &n))) return;
        // Where each wrapped part's words start and end.
        struct Part {
            float x, w, y, h;
            UINT32 pos, len;
        };
        std::vector<Part> parts;
        float total = 0, y = 0;
        UINT32 pos = 0;
        for (const DWRITE_LINE_METRICS& l : lines) {
            DWRITE_HIT_TEST_METRICS hm[8];
            UINT32 count = 0;
            float x0 = 0, x1 = 0;
            if (SUCCEEDED(layout->HitTestTextRange(pos, l.length - l.trailingWhitespaceLength, 0, 0, hm, 8, &count)) &&
                count > 0) {
                x0 = hm[0].left;
                x1 = hm[0].left + hm[0].width;
                for (UINT32 j = 1; j < count; ++j) {
                    x0 = std::min(x0, hm[j].left);
                    x1 = std::max(x1, hm[j].left + hm[j].width);
                }
            }
            parts.push_back({x0, x1 - x0, y, l.height, pos, l.length});
            total += x1 - x0;
            y += l.height;
            pos += l.length;
        }
        // The soft edge runs past each part's end, so a finished part is lit to its last letter.
        const float soft = look_.size * 0.8f;
        float left = sung * (total + soft * static_cast<float>(parts.size()));
        if (chars >= 0) {
            // The parts before the one being sung are lit whole; in it, up to the character's x
            // (between two letters for a fraction) and half the soft edge past it.
            left = 0;
            const UINT32 c = static_cast<UINT32>(chars);
            for (const Part& part : parts) {
                if (c >= part.pos + part.len && &part != &parts.back()) {
                    left += part.w + soft;
                    continue;
                }
                auto xAt = [layout](UINT32 i) {
                    FLOAT x = 0, y = 0;
                    DWRITE_HIT_TEST_METRICS m{};
                    layout->HitTestTextPosition(i, FALSE, &x, &y, &m);
                    return std::make_pair(x, m.width);
                };
                const auto [x, w] = xAt(std::min(c, part.pos + part.len));
                const float frac = static_cast<float>(chars - static_cast<double>(c));
                left += std::clamp(x + w * frac - part.x, 0.0f, part.w) + soft * 0.5f;
                break;
            }
        }
        const float lineW = layout->GetMaxWidth();
        for (const Part& part : parts) {
            const float done = std::min(left, part.w + soft);
            left -= done;
            if (done <= 0) break;
            const float edgeX = o.x + part.x + done;
            edge->SetStartPoint({edgeX - soft, 0});
            edge->SetEndPoint({edgeX, 0});
            ctx->PushAxisAlignedClip(D2D1::RectF(o.x - soft, o.y + part.y, o.x + lineW + soft, o.y + part.y + part.h),
                                     D2D1_ANTIALIAS_MODE_ALIASED);
            ctx->DrawTextLayout(o, layout, edge);
            ctx->PopAxisAlignedClip();
        }
    }

    // White fading to clear from the start point to the end point (PaintFill moves them).
    ID2D1LinearGradientBrush* FillBrush(ID2D1DeviceContext5* ctx) {
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (fill_ && fillGen_ == gen) return fill_.Get();
        fill_.Reset();
        fillGen_ = gen;
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, {1, 1, 1, 1}}, {1.0f, {1, 1, 1, 0}}};
        gfx::ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll)) ||
            FAILED(ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {1, 0}), coll.Get(),
                                                  &fill_)))
            return nullptr;
        return fill_.Get();
    }

    // A vertical opacity mask over the column: lines fade out toward its top and bottom edges
    // instead of being cut there.
    bool PushEdgeMask(Painter& p, const Rect& box) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (!mask_ || maskH_ != box.h || maskGen_ != gen) {
            mask_.Reset();
            maskH_ = box.h;
            maskGen_ = gen;
            const float f = std::min(0.4f, kEdgeFade / box.h);
            const D2D1_GRADIENT_STOP stops[] = {{0.0f, {0, 0, 0, 0}}, {f, {0, 0, 0, 1}}, {1 - f, {0, 0, 0, 1}},
                                                {1.0f, {0, 0, 0, 0}}};
            gfx::ComPtr<ID2D1GradientStopCollection> coll;
            if (FAILED(ctx->CreateGradientStopCollection(stops, 4, &coll)) ||
                FAILED(ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), coll.Get(),
                                                      &mask_)))
                return false;
        }
        mask_->SetStartPoint({box.x, box.y});
        mask_->SetEndPoint({box.x, box.Bottom()});
        ctx->PushLayer(D2D1::LayerParameters1(D2D1::RectF(box.x, box.y, box.Right(), box.Bottom()), nullptr,
                                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f,
                                              mask_.Get()),
                       nullptr);
        return true;
    }

    int LineAt(float x, float y) const {
        if (!lyrics_.synced) return -1;
        for (size_t i = 0; i < lineRects_.size(); ++i)
            if (lineRects_[i].w > 0 && lineRects_[i].Contains(x, y)) return static_cast<int>(i);
        return -1;
    }

    // The cover, blurred and scaled to fill the view, then darkened toward the edges.
    void PaintBackdrop(Painter& p, ID2D1Bitmap* bmp, const Rect& cover) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (bmp != blurFor_ || gfx::Gpu::Get().Generation() != generation_) {
            blur_.Reset();
            blurFor_ = bmp;
            generation_ = gfx::Gpu::Get().Generation();
            if (SUCCEEDED(ctx->CreateEffect(kGaussianBlur, &blur_))) {
                blur_->SetInput(0, bmp);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 18.0f);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
            }
        }
        gfx::ComPtr<ID2D1Image> out;
        if (blur_) blur_->GetOutput(&out);
        // The size of what the effect draws, in DIPs of this context: an effect works in pixels,
        // so at 125-200 % the output is 1/scale of the bitmap's 96-dpi GetSize().
        D2D1_RECT_F b{};
        if (out && FAILED(ctx->GetImageLocalBounds(out.Get(), &b))) b = {};
        const float bw = b.right - b.left, bh = b.bottom - b.top;
        if (out && bw > 0 && bh > 0) {
            // Cover the view with the image, centered on the cover (slightly larger, like the design);
            // with lyrics the cover sits left of center, so the image must reach the far edge too.
            const float halfW = std::max(cover.Cx() - rect.x, rect.Right() - cover.Cx());
            const float halfH = std::max(cover.Cy() - rect.y, rect.Bottom() - cover.Cy());
            const float k = std::max(2 * halfW / bw, 2 * halfH / bh) * 1.15f;
            const float w = bw * k, h = bh * k;
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            ctx->SetTransform(D2D1::Matrix3x2F::Translation(-b.left, -b.top) * D2D1::Matrix3x2F::Scale(k, k) *
                              D2D1::Matrix3x2F::Translation(cover.Cx() - w / 2, cover.Cy() - h / 2) * old);
            // DrawImage ignores the painter's opacity (the cover fading in): a layer carries it.
            const float fade = p.Opacity();
            if (fade < 1)
                ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                      D2D1::IdentityMatrix(), fade),
                               nullptr);
            ctx->DrawImage(out.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
            if (fade < 1) ctx->PopLayer();
            ctx->SetTransform(old);
        }
        const Palette& c = P();
        p.Fill(rect, c.videoBg.WithAlpha(0.62f));
        Vignette(p, cover);
    }

    // The gradient brushes, made once per device and palette and moved with
    // SetCenter / SetRadius: synced lyrics repaint the view every frame while they glide.
    // The stage is dark in both themes, so the light parts are white, not the palette's text.
    bool EnsureBrushes(ID2D1DeviceContext5* ctx) {
        const Palette& c = P();
        const Color white = Color::Hex(0xFFFFFF);
        const D2D1_COLOR_F bg = c.videoBg.D2D();
        const uint32_t gen = gfx::Gpu::Get().Generation();
        auto same = [](const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        if (glow_ && brushGen_ == gen && same(brushBg_, bg)) return true;
        glow_.Reset();
        vignette_.Reset();
        brushGen_ = gen;
        brushBg_ = bg;
        const D2D1_GRADIENT_STOP glowStops[] = {{0.0f, white.WithAlpha(0.07f).D2D()},
                                                {0.55f, white.WithAlpha(0.025f).D2D()},
                                                {1.0f, white.WithAlpha(0.0f).D2D()}};
        const D2D1_GRADIENT_STOP vignetteStops[] = {{0.0f, c.videoBg.WithAlpha(0.0f).D2D()},
                                                    {1.0f, c.videoBg.WithAlpha(0.85f).D2D()}};
        gfx::ComPtr<ID2D1GradientStopCollection> glowColl, vignetteColl;
        const auto props = D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1);
        if (FAILED(ctx->CreateGradientStopCollection(glowStops, 3, &glowColl)) ||
            FAILED(ctx->CreateGradientStopCollection(vignetteStops, 2, &vignetteColl)) ||
            FAILED(ctx->CreateRadialGradientBrush(props, glowColl.Get(), &glow_)) ||
            FAILED(ctx->CreateRadialGradientBrush(props, vignetteColl.Get(), &vignette_))) {
            glow_.Reset();
            return false;
        }
        return true;
    }

    // No artwork: a soft light halo behind the generated art.
    void PaintGlow(Painter& p, const Rect& cover) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (!EnsureBrushes(ctx)) return;
        const float r = cover.w * 0.95f;
        glow_->SetCenter({cover.Cx(), cover.Cy()});
        glow_->SetRadiusX(r);
        glow_->SetRadiusY(r);
        glow_->SetOpacity(p.Opacity());
        ctx->FillRectangle(rect.D2D(), glow_.Get());
    }

    void Vignette(Painter& p, const Rect& cover) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (!EnsureBrushes(ctx)) return;
        const float rx = std::max(rect.w, rect.h) * 0.75f;
        vignette_->SetCenter({cover.Cx(), cover.Cy()});
        vignette_->SetRadiusX(rx);
        vignette_->SetRadiusY(rx);
        vignette_->SetOpacity(p.Opacity());
        ctx->FillRectangle(rect.D2D(), vignette_.Get());
    }

    // Monochrome stand-in art (app/StandInArt): translucent white on the always-dark stage, the
    // dark theme's card tones in both themes (a light card there looked like a hole).
    void PaintGeneratedArt(Painter& p, const Rect& r, uint32_t seed) {
        const Color white = Color::Hex(0xFFFFFF);
        PaintStandInArt(p, r, 16, seed, {white.Fade(0.12f), white, white.Fade(0.55f), white.Fade(0.15f)});
    }

    Widget* titleTip_ = nullptr;
    LyricsSourceButton* source_ = nullptr;
    MusicPill *lyricsBtn_ = nullptr, *queueBtn_ = nullptr;
    static constexpr float kPillsH = 16 + 32;  // the gap above the pills and their height
    Lyrics lyrics_;
    bool lyricsOn_ = true;  // the Lyrics pill hides them (for the session)
    bool asked_ = false;    // the user asked for lyrics (LRCLIB, a .lrc): say when there are none
    // Lyrics from LRCLIB, shown only while the track has none of its own.
    bool online_ = false;
    net::LyricsAnswer onlineAnswer_;
    net::online::TrackInfo onlineTrack_;
    uint64_t onlineReq_ = 0, onlineSerial_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    bool noLyricsCardPending_ = false;
    uint64_t serial_ = 0;
    std::shared_ptr<LyricsLoad> pending_;
    int current_ = -1, hovered_ = -1;
    AnimFloat scroll_{-1};  // the line the column is centered on, fractional while moving
    float staticScroll_ = 0;
    float layoutW_ = -1, totalH_ = 0;
    std::vector<float> tops_, heights_;
    std::vector<gfx::ComPtr<IDWriteTextLayout>> layouts_;  // EnsureLayout, one per line
    std::vector<Rect> lineRects_;  // where clickable synced lines were painted
    LyricsLook look_;  // base_ fitted to the view (FitLyrics)
    LyricsLook base_;  // as the settings say
    float fit_ = 1;
    gfx::ComPtr<IDWriteTextFormat> format_;  // the look's font; reset when it changes
    Config::Token lyricsToken_ = 0;
    double clock_ = 0, clockAt_ = 0;  // SmoothTime's clock and when it last ran
    gfx::ComPtr<ID2D1LinearGradientBrush> fill_;  // FillBrush
    uint32_t fillGen_ = 0;
    Rect box_;

    ScopedConnection conn_, imagesConn_;
    Config::Token vizToken_ = 0;
    VizStyle style_ = VizStyle::Off;
    VizLayout layout_ = VizLayout::Replace;
    audio::VisualFrame frame_;
    AnimFloat vizShow_{0};  // 0 = the cover, 1 = the visualizer
    bool artWaited_ = false;   // the cover was being looked up at the last paint
    AnimFloat art_{1.0f};      // the stand-in art's fade-in after that lookup
    VizPaint viz_;  // the visualizer's drawing, shared with the meters window
    gfx::ComPtr<ID2D1Effect> blur_;
    gfx::ComPtr<ID2D1RadialGradientBrush> glow_, vignette_;
    gfx::ComPtr<ID2D1LinearGradientBrush> mask_;  // PushEdgeMask
    float maskH_ = -1;
    uint32_t maskGen_ = 0;
    uint32_t brushGen_ = 0;
    D2D1_COLOR_F brushBg_{};
    ID2D1Bitmap* blurFor_ = nullptr;
    uint32_t generation_ = 0;
};

}  // namespace

void ShowMusicLyrics() {
    if (g_musicView) g_musicView->ShowLyrics();
}

const Lyrics& MusicLyrics(int* line) {
    static const Lyrics kNone;
    if (!g_musicView) {
        if (line) *line = -1;
        return kNone;
    }
    return g_musicView->LyricsNow(line);
}

bool MusicCoverReady() {
    return !g_musicView || g_musicView->CoverReady();
}

Rect MusicToastSlot() {
    if (!g_musicView || !g_musicView->visible) return {};
    const auto g = g_musicView->Place();
    // Lyrics only: under the title line on top, clear of the lyrics (PaintLyricsOnly).
    if (g.full) return {g.area.x, g_musicView->rect.y + 60, g.area.w, 40};
    // The title is 70 DIP under the cover (Paint): a 40 DIP toast sits in the middle of the gap.
    if (g.textLeft) return {g.viz.x, g.viz.y - 44, g.viz.w, 40};
    return {g.left.x, std::round(g.title.y - 35 * g.ts - 20), g.left.w, 40};
}

std::vector<MenuItem> LyricsStyleItems() {
    std::vector<MenuItem> items;
    const std::string cur = TheApp().config().GetString("audio.lyricsStyle");
    for (const auto& [value, label] : {std::pair<const char*, const wchar_t*>{"moonbow", L"Moonbow"},
                                       {"spotify", L"Like Spotify"},
                                       {"apple", L"Like Apple Music"},
                                       {"yandex", L"Like Yandex Music"},
                                       {"karaoke", L"Karaoke"}}) {
        MenuItem m;
        m.label = Tr(label);
        m.radio = true;
        m.checked = cur == value;
        const std::string v = value;
        m.action = [v] { TheApp().config().SetString("audio.lyricsStyle", v); };
        items.push_back(std::move(m));
    }
    return items;
}

std::unique_ptr<Widget> CreateMusicView() {
    return std::make_unique<MusicView>();
}

}  // namespace app
