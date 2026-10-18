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
#include "app/player/ControlBar.h"
#include "app/player/PanelKit.h"
#include "app/player/Lyrics.h"
#include "app/player/PlayerView.h"
#include "audio/LoopbackCapture.h"
#include "audio/LoopbackSupport.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"
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

constexpr float kWideLyrics = 980;  // narrower views show only the sung line under the title
constexpr float kLineGap = 14;
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

class MusicView : public Widget {
public:
    MusicView() {
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
        imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
        // Hover target for the full title when it is cut (placed by Paint, where the text goes).
        titleTip_ = Add<Widget>();
        titleTip_->interactive = true;
        titleTip_->visible = false;
    }
    ~MusicView() override {
        TheApp().config().Unsubscribe(vizToken_);
        TheApp().player().SetSignalTap(false);
    }
    void OnLayout() override {}  // titleTip_ is placed by Paint

    void DebugInfo(std::string& out) const override {
        Widget::DebugInfo(out);
        if (style_ == VizStyle::Off) return;
        float top = 0;
        for (float b : frame_.bands) top = std::max(top, b);
        char text[160];
        snprintf(text, sizeof text, " viz=%d frames=%llu captured=%llu level=%.2f top=%.2f%s%s", static_cast<int>(style_),
                 static_cast<unsigned long long>(frame_.serial),
                 static_cast<unsigned long long>(capture_ ? capture_->Captured() : 0), frame_.level, top,
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
        if (panel && rect.w - (panel::kPanelW + panel::kGap) >= 480) area.w -= panel::kPanelW + panel::kGap;
        // With lyrics in a wide view the cover and its text take the left half.
        const bool wide = !lyrics_.Empty() && area.w >= kWideLyrics;
        const bool oneLine = !wide && lyrics_.synced;
        const float extra = oneLine ? 32.0f : 0.0f;
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
        const std::wstring title = s.title.empty() ? (item ? FileNameOf(item->path) : L"") : s.title;
        std::wstring second = s.artist;
        if (!s.album.empty()) second += (second.empty() ? L"" : L" · ") + s.album;
        // The view is always on the (dark) video background, also in the light theme, so the
        // text is light rather than the palette's text colors.
        const Color white = Color::Hex(0xFFFFFF);
        const Rect titleBox{left.x + 48, cover.Bottom() + 70, left.w - 96, 36};
        const Rect secondBox{left.x + 48, cover.Bottom() + 106, left.w - 96, 22};
        p.Text(title, titleBox, TextStyle::Title, Color::Mix(c.videoBg, white, 0.8f), HAlign::Center);
        if (!second.empty())
            p.Text(second, secondBox, TextStyle::Body, Color::Mix(c.videoBg, white, 0.55f), HAlign::Center);
        const bool cut = p.Measure(title, TextStyle::Title).width > titleBox.w ||
                         (!second.empty() && p.Measure(second, TextStyle::Body).width > secondBox.w);
        titleTip_->visible = cut;
        if (cut) {
            titleTip_->rect = {titleBox.x, titleBox.y, titleBox.w, second.empty() ? titleBox.h : secondBox.Bottom() - titleBox.y};
            titleTip_->tooltip = second.empty() ? title : title + L"\n" + second;
        }

        if (wide) {
            const float x = left.Right() + 16;
            box_ = {x, rect.y + 48, std::min(area.Right() - 48 - x, 640.0f), rect.h - 96};
            PaintLyrics(p, box_);
        } else {
            box_ = {};
            std::fill(lineRects_.begin(), lineRects_.end(), Rect{});
            if (oneLine && current_ >= 0)
                p.Text(lyrics_.lines[static_cast<size_t>(current_)].text,
                       {left.x + 48, cover.Bottom() + (second.empty() ? 106.0f : 138.0f), left.w - 96, 24},
                       TextStyle::Large, Color::Mix(c.videoBg, white, 0.9f), HAlign::Center);
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
        const Color bg = P().videoBg, white = Color::Hex(0xFFFFFF);
        const Color fg = Color::Mix(bg, white, 0.86f), dim = Color::Mix(bg, white, 0.4f);
        // The bar styles take a wider band than the square cover, at the same height, so the
        // title below stays where it was.
        const float w = std::floor(std::min(left.w - 96, cover.w * 1.5f));
        const Rect band{std::round(left.Cx() - w / 2), cover.y, w, cover.h};
        switch (style_) {
            case VizStyle::Spectrum: PaintSpectrum(p, band, fg, dim); break;
            case VizStyle::Waveform: PaintWaveform(p, band, bg, white); break;
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
    void PaintWaveform(Painter& p, const Rect& r, Color bg, Color white) {
        constexpr int n = audio::kWaveColumns;
        const float step = r.w / n;
        const float bw = std::max(2.0f, std::round(step * 0.5f));
        const float cy = r.Cy();
        for (int i = 0; i < n; ++i) {
            const float x = std::round(r.x + i * step + (step - bw) / 2);
            const float h = std::max(2.0f, std::round(frame_.wave[i] * r.h * 0.9f / 2) * 2);
            const float k = static_cast<float>(i) / (n - 1);
            p.Fill({x, std::round(cy - h / 2), bw, h}, Color::Mix(bg, white, 0.22f + 0.64f * k * k), bw / 2);
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
            SetLyrics({});
            if (!s.fileLoaded) return;
            const QueueItem* item = TheApp().queue().CurrentItem();
            const std::wstring lrc = LyricsPathFor(item ? item->path : s.path);
            if (!lrc.empty()) {
                auto job = std::make_shared<LyricsLoad>();
                pending_ = job;
                const HWND hwnd = GetHost() ? GetHost()->Hwnd() : nullptr;
                std::thread([job, lrc, hwnd] {
                    Lyrics l = LoadLyricsFile(lrc);
                    {
                        std::lock_guard lock(job->mu);
                        job->lyrics = std::move(l);
                        job->done = true;
                    }
                    if (hwnd) PostMessageW(hwnd, ui::Host::kRepaintMessage, 0, 0);
                }).detach();
            }
        }
        if (s.fileLoaded && lyrics_.Empty()) ReadEmbeddedLyrics();
    }

    void ReadEmbeddedLyrics() {
        Player& player = TheApp().player();
        const int count = static_cast<int>(player.GetPropertyDouble("metadata/list/count", 0));
        for (int i = 0; i < count && i < 256; ++i) {
            const std::string base = "metadata/list/" + std::to_string(i);
            if (!IsLyricsTag(player.GetPropertyString((base + "/key").c_str()))) continue;
            Lyrics l = ParseLyrics(Utf8ToWide(player.GetPropertyString((base + "/value").c_str())));
            if (!l.Empty()) {
                SetLyrics(std::move(l));
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
        if (!l.Empty()) SetLyrics(std::move(l));
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
        if (!lyrics_.synced) return;
        const int line = lyrics_.LineAt(TheApp().player().State().position + kLead);
        if (line == current_) return;
        // Far jumps (a seek) move quicker than the next line coming up.
        const int dist = std::abs(line - current_);
        current_ = line;
        // The lines move under a still pointer; the next mouse move finds the new one.
        ClearHover();
        scroll_.To(static_cast<float>(line), dist > 3 ? 260.0f : 340.0f, Ease::InOut);
        Invalidate();
    }

    void ClearHover() {
        if (hovered_ < 0) return;
        hovered_ = -1;
        cursor = IDC_ARROW;
    }

    // Line heights for the column width; measured again only when the width or the lyrics change.
    void EnsureLayout(Painter& p, float w) {
        if (w == layoutW_) return;
        layoutW_ = w;
        const size_t n = lyrics_.lines.size();
        tops_.assign(n, 0);
        heights_.assign(n, 0);
        lineRects_.assign(n, Rect{});
        const TextStyle style = TextStyle::Section;
        const float blank = p.Measure(L"A", style).height * 0.6f;
        float y = 0;
        for (size_t i = 0; i < n; ++i) {
            const std::wstring& t = lyrics_.lines[i].text;
            heights_[i] = t.empty() ? blank : p.Measure(t, style, w, gfx::kTextWrap).height;
            tops_[i] = y;
            y += heights_[i] + (lyrics_.synced ? kLineGap : 4.0f);
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
        EnsureLayout(p, box.w);
        const Color bg = P().videoBg, white = Color::Hex(0xFFFFFF);
        const bool synced = lyrics_.synced;
        // Synced: the sung line sits a little above the middle. Unsynced: from the top (centered
        // when it fits), moved only by the wheel.
        const float offset = synced ? box.y + box.h * 0.42f - CenterAt(scroll_.Get())
                                    : box.y + (totalH_ <= box.h ? (box.h - totalH_) / 2 : -staticScroll_);
        p.PushClip(box);
        const bool masked = PushEdgeMask(p, box);
        for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const Rect r{box.x, std::round(offset + tops_[i]), box.w, heights_[i]};
            lineRects_[i] = {};
            if (r.Bottom() < box.y || r.y > box.Bottom()) continue;
            const LyricLine& line = lyrics_.lines[i];
            float a = 0.72f;
            if (synced) {
                const int at = static_cast<int>(i);
                const float d = static_cast<float>(std::abs(at - current_));
                // Lines to come stay readable ahead (at least 40 %); sung ones step back further.
                if (at == current_) a = 0.96f;
                else if (at > current_) a = std::max(0.40f, 0.62f - 0.05f * d);
                else a = std::max(0.24f, 0.46f - 0.06f * d);
                if (at == hovered_) a = std::max(a, 0.78f);
                if (line.time >= 0) lineRects_[i] = r;
            }
            // A pause in synced lyrics shows as dots; a stanza break in plain text stays blank.
            if (line.text.empty() && !synced) continue;
            const std::wstring_view text = line.text.empty() ? std::wstring_view(L"\u00B7 \u00B7 \u00B7") : line.text;
            p.Text(text, r, TextStyle::Section, Color::Mix(bg, white, a), HAlign::Leading, VAlign::Top,
                   gfx::kTextWrap);
        }
        if (masked) p.Ctx()->PopLayer();
        p.PopClip();
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
    Lyrics lyrics_;
    uint64_t serial_ = 0;
    std::shared_ptr<LyricsLoad> pending_;
    int current_ = -1, hovered_ = -1;
    AnimFloat scroll_{-1};  // the line the column is centered on, fractional while moving
    float staticScroll_ = 0;
    float layoutW_ = -1, totalH_ = 0;
    std::vector<float> tops_, heights_;
    std::vector<Rect> lineRects_;  // where clickable synced lines were painted
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

std::unique_ptr<Widget> CreateMusicView() {
    return std::make_unique<MusicView>();
}

}  // namespace app
