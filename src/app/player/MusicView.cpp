// Music view shown instead of the video for audio files (design/ref/Player-Music_*.png):
// a large cover over a blurred, darkened copy of itself, and "title - artist" below.
// Files without artwork get generated monochrome art (concentric grooves).
// With lyrics (a .lrc next to the track or an embedded tag) a wide view puts the cover on the
// left and the lyrics on the right: synced lines follow playback with the sung line highlighted
// and a click on a line seeks to it; unsynced text stays put and scrolls with the wheel.
// With Settings > Audio > Visualization on, a playing song shows the visualizer where the cover
// is (spectrum bars, a radial spectrometer, an oscilloscope or a scrolling waveform), fed by
// audio/LoopbackCapture from Lumen's own sound; a pause brings the cover back.
#include "Util.h"
#include "app/App.h"
#include "app/MainWindow.h"
#include "app/library/LibraryCommon.h"
#include "app/player/ControlBar.h"
#include "app/player/PanelKit.h"
#include "app/player/Lyrics.h"
#include "app/player/NowPlaying.h"
#include "app/player/PlayerView.h"
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

uint32_t Hash(const std::wstring& s) {
    uint32_t h = 2166136261u;
    for (wchar_t c : s) h = (h ^ static_cast<uint32_t>(c)) * 16777619u;
    return h;
}

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

enum class VizStyle { Off, Spectrometer, Scope, Spectrum, Waveform };

VizStyle VizStyleOf(const std::string& v) {
    if (v == "spectrometer") return VizStyle::Spectrometer;
    if (v == "scope") return VizStyle::Scope;
    if (v == "spectrum") return VizStyle::Spectrum;
    if (v == "waveform") return VizStyle::Waveform;
    return VizStyle::Off;
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
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) RefreshLyrics();
            if (m & kChangeTime) FollowTime();
            if (m & (kChangeMeta | kChangeFile | kChangeState)) Invalidate();
            // The capture parked itself after a long frame (a busy UI thread): a paint wakes it.
            // Only while the view is still drawn; hidden or minimized it stays parked.
            if ((m & kChangeTime) && capture_ && capture_->Stalled() && AnimFloat::Now() - lastPaint_ < 1000)
                Invalidate();
        });
        vizToken_ = TheApp().config().Subscribe("audio.visualizer", [this](const std::string&) {
            ReadVisualizerSetting();
            Invalidate();
        });
        ReadVisualizerSetting();
        look_ = ReadLyricsLook(TheApp().config());
        lyricsToken_ = TheApp().config().Subscribe("audio.lyrics", [this](const std::string&) {
            look_ = ReadLyricsLook(TheApp().config());
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
        queueBtn_->onClick = [this] { TogglePlayerPanel(PanelKind::Queue, queueBtn_->rect); };
    }
    ~MusicView() override {
        if (g_musicView == this) g_musicView = nullptr;
        TheApp().config().Unsubscribe(vizToken_);
        TheApp().config().Unsubscribe(lyricsToken_);
        TheApp().player().SetSignalTap(false);
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
                 static_cast<unsigned long long>(capture_ ? capture_->Captured() : 0),
                 capture_ ? capture_->CpuSeconds() * 1000 : 0.0, frame_.level, top,
                 capture_ && capture_->Failed() ? " failed" : "", capture_ && capture_->Stalled() ? " parked" : "");
        out += text;
    }

    void Paint(Painter& p) override {
        TakeLoadedLyrics();
        const Palette& c = P();
        p.Fill(rect, c.videoBg);
        const PlayerState& s = TheApp().player().State();
        const QueueItem* item = TheApp().queue().CurrentItem();
        const std::wstring path = item ? item->path : s.path;

        // An open popover (queue, tracks, settings) takes the right side: center the cover and
        // the text in what is left, when that is still wide enough.
        Rect area = rect;
        const bool panel = PlayerPanelOpen(PanelKind::Queue) || PlayerPanelOpen(PanelKind::Tracks) ||
                           PlayerPanelOpen(PanelKind::Subtitles) || PlayerPanelOpen(PanelKind::Settings);
        // 320 leaves a usable cover next to the compact library too (844 - 420 DIP at 1280 wide).
        if (panel && rect.w - (panel::kPanelW + panel::kGap) >= 320) area.w -= panel::kPanelW + panel::kGap;
        // With lyrics in a wide view the cover and its text take the left half.
        const bool shown = lyricsOn_ && !lyrics_.Empty();
        const bool wide = shown && area.w >= kWideLyrics;
        const bool oneLine = shown && !wide && lyrics_.synced;
        const float extra = (oneLine ? 32.0f : 0.0f) + kPillsH;
        Rect left = area;
        if (wide) left.w = std::floor(area.w * 0.5f);
        // Layout: cover centered, text 88 DIP under it (1920×1080: 738 cover, 73 top margin).
        const float side =
            std::floor(std::max(96.0f, std::min({left.w - 96, rect.h - 180 - extra, wide ? 620.0f : 738.0f})));
        const float block = side + 88 + 30 + extra;
        const float top = rect.y + std::max(24.0f, (rect.h - block) / 2);
        const Rect cover{std::round(left.Cx() - side / 2), std::round(top), side, side};

        ID2D1Bitmap* bmp = nullptr;
        if (!path.empty() && GetHost()) {
            const int px = static_cast<int>(side * GetHost()->Scale());
            bmp = TheApp().images().Get(path, px);
        }

        p.PushClip(rect);
        if (bmp) PaintBackdrop(p, bmp, cover);
        else PaintGlow(p, cover);
        p.PopClip();

        lastPaint_ = AnimFloat::Now();
        const float viz = UpdateVisualizer(s);
        if (viz < 1) {
            p.PushOpacity(1 - viz);
            p.Shadow(cover, 16, 40, 12, 0.55f * (1 - viz));
            if (bmp) p.Image(bmp, cover, 16, true);
            else PaintGeneratedArt(p, cover, Hash(path));
            p.PopOpacity();
        }
        if (viz > 0) {
            p.PushOpacity(viz);
            PaintVisualizer(p, left, cover, bmp);
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
        const Rect titleBox{left.x + 48, cover.Bottom() + 70, left.w - 96, 36};
        const Rect secondBox{left.x + 48, cover.Bottom() + 106, left.w - 96, 22};
        p.Text(title, titleBox, TextStyle::Title, white.Fade(0.8f), HAlign::Center);
        if (!second.empty())
            p.Text(second, secondBox, TextStyle::Body, white.Fade(0.55f), HAlign::Center);
        const bool cut = p.Measure(title, TextStyle::Title).width > titleBox.w ||
                         (!second.empty() && p.Measure(second, TextStyle::Body).width > secondBox.w);
        titleTip_->visible = cut;
        if (cut) {
            titleTip_->rect = {titleBox.x, titleBox.y, titleBox.w, second.empty() ? titleBox.h : secondBox.Bottom() - titleBox.y};
            titleTip_->tooltip = second.empty() ? title : title + L"\n" + second;
        }

        // The pills: under the artist, or under the sung line when it shows there.
        const float textEnd =
            cover.Bottom() + (oneLine ? (second.empty() ? 130.0f : 162.0f) : second.empty() ? 106.0f : 128.0f);
        lyricsBtn_->active = shown;
        lyricsBtn_->dim = lyrics_.Empty() && !pending_ && !onlineReq_;
        queueBtn_->active = PlayerPanelOpen(PanelKind::Queue);
        const float lw = lyricsBtn_->Width(), qw = queueBtn_->Width();
        const float px = std::round(left.Cx() - (lw + 10 + qw) / 2), py = std::round(textEnd + 16);
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
                p.Text(lyrics_.lines[static_cast<size_t>(current_)].text,
                       {left.x + 48, cover.Bottom() + (second.empty() ? 106.0f : 138.0f), left.w - 96, 24},
                       TextStyle::Large, white.Fade(0.9f), HAlign::Center);
        }
    }

    bool OnMouseMove(const MouseEvent& e) override {
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
        TheApp().player().SeekAbsolute(lyrics_.lines[static_cast<size_t>(hit)].time, true);
        ClearHover();  // the column glides to the line: no second highlight next to the sung one
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
        style_ = audio::ProcessLoopbackSupported() ? VizStyleOf(TheApp().config().GetString("audio.visualizer"))
                                                   : VizStyle::Off;
        // While it is on, volume 0 still carries a (silent) signal to listen to (Player::SetSignalTap).
        TheApp().player().SetSignalTap(style_ != VizStyle::Off);
        if (style_ == VizStyle::Off) {
            capture_.reset();
            vizShow_.Set(0);
        }
    }

    // Runs the capture while the song plays and this view is drawn; returns how far the cover
    // has given way to the visualizer (0..1).
    float UpdateVisualizer(const PlayerState& s) {
        if (style_ == VizStyle::Off) return 0;
        const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
        if (playing && !capture_ && GetHost())
            capture_ = std::make_unique<audio::LoopbackCapture>(GetHost()->Hwnd(), ui::Host::kRepaintMessage);
        bool show = false;
        if (capture_) {
            capture_->SetGain(TheApp().player().OutputGain());
            capture_->SetRunning(playing);
            capture_->Take(frame_);
            show = playing && !capture_->Failed();
        }
        const float target = show ? 1.0f : 0.0f;
        if (vizShow_.Target() != target) vizShow_.To(target, 220, Ease::InOut);
        return vizShow_.Get();
    }

    ID2D1StrokeStyle* RoundStroke(ID2D1DeviceContext5* ctx) {
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (roundStroke_ && strokeGen_ == gen) return roundStroke_.Get();
        roundStroke_.Reset();
        strokeGen_ = gen;
        gfx::ComPtr<ID2D1Factory> factory;
        ctx->GetFactory(&factory);
        const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                       D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        if (factory) factory->CreateStrokeStyle(props, nullptr, 0, &roundStroke_);
        return roundStroke_.Get();
    }

    // Lines go straight to the context (Painter has no line call); one brush, made once per device.
    ID2D1SolidColorBrush* LineBrush(ID2D1DeviceContext5* ctx, Color c, float opacity) {
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (!lineBrush_ || lineGen_ != gen) {
            lineBrush_.Reset();
            lineGen_ = gen;
            if (FAILED(ctx->CreateSolidColorBrush(c.D2D(), &lineBrush_))) return nullptr;
        }
        lineBrush_->SetColor(c.Fade(opacity).D2D());
        return lineBrush_.Get();
    }

    // Monochrome on the video background, like the title (the view is dark in both themes).
    void PaintVisualizer(Painter& p, const Rect& left, const Rect& cover, ID2D1Bitmap* bmp) {
        const Color white = Color::Hex(0xFFFFFF);
        const Color fg = white.Fade(0.86f), dim = white.Fade(0.4f);
        // The bar styles take a wider band than the square cover, at the same height, so the
        // title below stays where it was.
        const float w = std::floor(std::min(left.w - 96, cover.w * 1.5f));
        const Rect band{std::round(left.Cx() - w / 2), cover.y, w, cover.h};
        switch (style_) {
            case VizStyle::Spectrum: PaintSpectrum(p, band, fg, dim); break;
            case VizStyle::Waveform: PaintWaveform(p, band, white); break;
            case VizStyle::Scope: PaintScope(p, band, fg, dim); break;
            case VizStyle::Spectrometer: PaintSpectrometer(p, cover, bmp, fg, dim); break;
            case VizStyle::Off: break;
        }
    }

    // Bars rising from the cover's bottom edge, with falling peak marks.
    void PaintSpectrum(Painter& p, const Rect& r, Color fg, Color dim) {
        constexpr int n = audio::kBands;
        const float step = r.w / n;
        const float bw = std::max(2.0f, step * 0.7f);
        const float top = r.h * 0.9f;
        for (int i = 0; i < n; ++i) {
            const float x0 = std::round(r.x + i * step), x1 = std::round(r.x + i * step + bw);
            const float h = std::max(3.0f, std::round(frame_.bands[i] * top));
            p.Fill({x0, r.Bottom() - h, x1 - x0, h}, fg, std::min(2.0f, (x1 - x0) / 2));
            const float peak = frame_.peaks[i] * top;
            if (peak > h + 6) p.Fill({x0, std::round(r.Bottom() - peak - 2), x1 - x0, 2}, dim, 1);
        }
    }

    // The recent loudness scrolling to the left, mirrored around the middle; older columns fade.
    void PaintWaveform(Painter& p, const Rect& r, Color white) {
        constexpr int n = audio::kWaveColumns;
        const float step = r.w / n;
        const float bw = std::max(2.0f, std::round(step * 0.5f));
        const float cy = r.Cy();
        for (int i = 0; i < n; ++i) {
            const float x = std::round(r.x + i * step + (step - bw) / 2);
            const float h = std::max(2.0f, std::round(frame_.wave[i] * r.h * 0.9f / 2) * 2);
            const float k = static_cast<float>(i) / (n - 1);
            p.Fill({x, std::round(cy - h / 2), bw, h}, white.Fade(0.22f + 0.64f * k * k), bw / 2);
        }
    }

    // An oscilloscope trace across the band, over a faint centre line.
    void PaintScope(Painter& p, const Rect& r, Color fg, Color dim) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const float cy = r.Cy(), amp = r.h * 0.42f;
        p.Fill({r.x, std::round(cy), r.w, 1}, dim.Fade(0.5f));
        ID2D1StrokeStyle* stroke = RoundStroke(ctx);
        ID2D1SolidColorBrush* brush = LineBrush(ctx, fg, p.Opacity());
        if (!brush) return;
        constexpr int n = audio::kScopePoints;
        D2D1_POINT_2F prev{r.x, cy - frame_.scope[0] * amp};
        for (int i = 1; i < n; ++i) {
            const D2D1_POINT_2F pt{r.x + r.w * static_cast<float>(i) / (n - 1), cy - frame_.scope[i] * amp};
            ctx->DrawLine(prev, pt, brush, 2.0f, stroke);
            prev = pt;
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
            p.FillCircle(c, inner, P().bgCard);
            const float icon = inner * 0.5f;
            p.Icon("music", {c.x - icon / 2, c.y - icon / 2, icon, icon}, P().textTertiary);
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
        items.push_back(std::move(none));
        // Why, in one line: where Lumen looked.
        MenuItem why;
        why.label = !net::online::LyricsOn() ? Tr(L"No .lrc file, no lyrics in the tags, online search is off")
                    : onlineSerial_ == serial_ && !onlineReq_ ? Tr(L"No .lrc file, no lyrics in the tags or on LRCLIB")
                                                               : Tr(L"No .lrc file, no lyrics in the tags");
        why.enabled = false;
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
                            L"also looks up the next songs without lyrics; you can turn it off in Settings > Network."),
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
        interactive = !lyrics_.Empty();
        layoutW_ = -1;
        lineRects_.clear();
        hovered_ = -1;
        cursor = IDC_ARROW;
        staticScroll_ = 0;
        current_ = lyrics_.LineAt(TheApp().player().State().position + kLead);
        scroll_.Set(static_cast<float>(current_));
        Invalidate();
    }

    // Playback moved: when another line starts, glide the column to it.
    void FollowTime() {
        TakeLoadedLyrics();
        const double pos = TheApp().player().State().position;
        if (!lyrics_.synced) return;
        const int line = lyrics_.LineAt(pos + kLead);
        if (line == current_) return;
        // Far jumps (a seek) move quicker than the next line coming up.
        const int dist = std::abs(line - current_);
        current_ = line;
        // The lines move under a still pointer; the next mouse move finds the new one.
        ClearHover();
        scroll_.To(static_cast<float>(line), dist > 3 ? 260.0f : 340.0f, Ease::InOut);
        Invalidate();
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
            if (synced && k.fill && static_cast<int>(i) == current_) {
                const double sung = lyrics_.SungFraction(current_, SmoothTime());
                filling = sung < 1;
                PaintFill(ctx, layout, {r.x, r.y}, static_cast<float>(sung), white.Fade(k.ahead), a, shown);
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
    // over its unlit color; wrapped parts fill one after another.
    void PaintFill(ID2D1DeviceContext5* ctx, IDWriteTextLayout* layout, D2D1_POINT_2F o, float sung, Color unlit,
                   float lit, float opacity) {
        if (ID2D1SolidColorBrush* brush = LineBrush(ctx, unlit, opacity)) ctx->DrawTextLayout(o, layout, brush);
        ID2D1LinearGradientBrush* edge = FillBrush(ctx);
        if (!edge || sung <= 0) return;
        edge->SetOpacity(lit * opacity);
        UINT32 n = 0;
        layout->GetLineMetrics(nullptr, 0, &n);
        std::vector<DWRITE_LINE_METRICS> lines(n);
        if (n == 0 || FAILED(layout->GetLineMetrics(lines.data(), n, &n))) return;
        // Where each wrapped part's words start and end.
        struct Part {
            float x, w, y, h;
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
            parts.push_back({x0, x1 - x0, y, l.height});
            total += x1 - x0;
            y += l.height;
            pos += l.length;
        }
        // The soft edge runs past each part's end, so a finished part is lit to its last letter.
        const float soft = look_.size * 0.8f;
        float left = sung * (total + soft * static_cast<float>(parts.size()));
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
            ctx->DrawImage(out.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
            ctx->SetTransform(old);
        }
        const Palette& c = P();
        p.Fill(rect, c.videoBg.WithAlpha(0.62f));
        Vignette(p, cover);
    }

    // The gradient and groove brushes, made once per device and palette and moved with
    // SetCenter / SetRadius: synced lyrics repaint the view every frame while they glide.
    bool EnsureBrushes(ID2D1DeviceContext5* ctx) {
        const Palette& c = P();
        const D2D1_COLOR_F text = c.textPrimary.D2D(), bg = c.videoBg.D2D();
        const uint32_t gen = gfx::Gpu::Get().Generation();
        auto same = [](const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        if (glow_ && brushGen_ == gen && same(brushText_, text) && same(brushBg_, bg)) return true;
        glow_.Reset();
        vignette_.Reset();
        grooves_.Reset();
        brushGen_ = gen;
        brushText_ = text;
        brushBg_ = bg;
        const D2D1_GRADIENT_STOP glowStops[] = {{0.0f, c.textPrimary.WithAlpha(0.07f).D2D()},
                                                {0.55f, c.textPrimary.WithAlpha(0.025f).D2D()},
                                                {1.0f, c.textPrimary.WithAlpha(0.0f).D2D()}};
        const D2D1_GRADIENT_STOP vignetteStops[] = {{0.0f, c.videoBg.WithAlpha(0.0f).D2D()},
                                                    {1.0f, c.videoBg.WithAlpha(0.85f).D2D()}};
        gfx::ComPtr<ID2D1GradientStopCollection> glowColl, vignetteColl;
        const auto props = D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1);
        if (FAILED(ctx->CreateGradientStopCollection(glowStops, 3, &glowColl)) ||
            FAILED(ctx->CreateGradientStopCollection(vignetteStops, 2, &vignetteColl)) ||
            FAILED(ctx->CreateRadialGradientBrush(props, glowColl.Get(), &glow_)) ||
            FAILED(ctx->CreateRadialGradientBrush(props, vignetteColl.Get(), &vignette_)) ||
            FAILED(ctx->CreateSolidColorBrush(text, &grooves_))) {
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

    // Monochrome stand-in art: dark square, record-like grooves and a center label.
    void PaintGeneratedArt(Painter& p, const Rect& r, uint32_t seed) {
        const Palette& c = P();
        p.Fill(r, c.bgCard, 16);
        p.PushRoundClip(r, 16);
        const Point center{r.Cx(), r.Cy()};
        const float maxR = r.w * 0.42f;
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (EnsureBrushes(ctx)) {
            ID2D1SolidColorBrush* brush = grooves_.Get();
            int k = 0;
            for (float rad = maxR * 0.38f; rad < maxR; rad += r.w * 0.012f, ++k) {
                const float a = 0.025f + 0.035f * static_cast<float>((seed >> (k % 24)) & 1);
                brush->SetColor(c.textPrimary.WithAlpha(a * p.Opacity()).D2D());
                ctx->DrawEllipse(D2D1::Ellipse({center.x, center.y}, rad, rad), brush, 1.0f);
            }
            brush->SetColor(c.textPrimary.WithAlpha(0.14f * p.Opacity()).D2D());
            ctx->DrawEllipse(D2D1::Ellipse({center.x, center.y}, maxR, maxR), brush, 1.5f);
        }
        p.FillCircle(center, maxR * 0.30f, c.bgSelected);
        const float icon = maxR * 0.22f;
        p.Icon("music", {center.x - icon / 2, center.y - icon / 2, icon, icon}, c.textTertiary);
        p.PopClip();
        p.Stroke(r, c.line, 16);
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
    LyricsLook look_;
    gfx::ComPtr<IDWriteTextFormat> format_;  // the look's font; reset when it changes
    Config::Token lyricsToken_ = 0;
    double clock_ = 0, clockAt_ = 0;  // SmoothTime's clock and when it last ran
    gfx::ComPtr<ID2D1LinearGradientBrush> fill_;  // FillBrush
    uint32_t fillGen_ = 0;
    Rect box_;

    ScopedConnection conn_, imagesConn_;
    Config::Token vizToken_ = 0;
    VizStyle style_ = VizStyle::Off;
    std::unique_ptr<audio::LoopbackCapture> capture_;
    audio::VisualFrame frame_;
    AnimFloat vizShow_{0};  // 0 = the cover, 1 = the visualizer
    double lastPaint_ = 0;
    gfx::ComPtr<ID2D1StrokeStyle> roundStroke_;
    gfx::ComPtr<ID2D1SolidColorBrush> lineBrush_;
    uint32_t strokeGen_ = 0, lineGen_ = 0;
    gfx::ComPtr<ID2D1Effect> blur_;
    gfx::ComPtr<ID2D1RadialGradientBrush> glow_, vignette_;
    gfx::ComPtr<ID2D1SolidColorBrush> grooves_;
    gfx::ComPtr<ID2D1LinearGradientBrush> mask_;  // PushEdgeMask
    float maskH_ = -1;
    uint32_t maskGen_ = 0;
    uint32_t brushGen_ = 0;
    D2D1_COLOR_F brushText_{}, brushBg_{};
    ID2D1Bitmap* blurFor_ = nullptr;
    uint32_t generation_ = 0;
};

}  // namespace

void ShowMusicLyrics() {
    if (g_musicView) g_musicView->ShowLyrics();
}

std::vector<MenuItem> LyricsStyleItems() {
    std::vector<MenuItem> items;
    const std::string cur = TheApp().config().GetString("audio.lyricsStyle");
    for (const auto& [value, label] : {std::pair<const char*, const wchar_t*>{"lumen", L"Lumen"},
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
