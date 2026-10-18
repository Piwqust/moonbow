// Music view shown instead of the video for audio files (design/ref/Player-Music_*.png):
// a large cover over a blurred, darkened copy of itself, and "title - artist" below.
// Files without artwork get generated monochrome art (concentric grooves).
// With lyrics (a .lrc next to the track or an embedded tag) a wide view puts the cover on the
// left and the lyrics on the right: synced lines follow playback with the sung line highlighted
// and a click on a line seeks to it; unsynced text stays put and scrolls with the wheel.
#include "Util.h"
#include "app/App.h"
#include "app/player/ControlBar.h"
#include "app/player/PanelKit.h"
#include "app/player/Lyrics.h"
#include "app/player/PlayerView.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"
#include "ui/Anim.h"

#include <d2d1_1.h>
#include <d2d1effects.h>

#include <algorithm>
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

class MusicView : public Widget {
public:
    MusicView() {
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) RefreshLyrics();
            if (m & kChangeTime) FollowTime();
            if (m & (kChangeMeta | kChangeFile | kChangeState)) Invalidate();
        });
        imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
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

        p.Shadow(cover, 16, 40, 12, 0.55f);
        if (bmp) p.Image(bmp, cover, 16, true);
        else PaintGeneratedArt(p, cover, Hash(path));

        std::wstring title = s.title.empty() ? (item ? FileNameOf(item->path) : L"") : s.title;
        if (!s.artist.empty()) title += L" · " + s.artist;
        // The view is always on the (dark) video background, also in the light theme, so the
        // text is light rather than the palette's text colors.
        const Color white = Color::Hex(0xFFFFFF);
        p.Text(title, {left.x + 48, cover.Bottom() + 70, left.w - 96, 36}, TextStyle::Title,
               Color::Mix(c.videoBg, white, 0.8f), HAlign::Center);
        if (!s.album.empty())
            p.Text(s.album, {left.x + 48, cover.Bottom() + 106, left.w - 96, 22}, TextStyle::Body,
                   Color::Mix(c.videoBg, white, 0.5f), HAlign::Center);

        if (wide) {
            const float x = left.Right() + 16;
            box_ = {x, rect.y + 48, std::min(area.Right() - 48 - x, 640.0f), rect.h - 96};
            PaintLyrics(p, box_);
        } else {
            box_ = {};
            std::fill(lineRects_.begin(), lineRects_.end(), Rect{});
            if (oneLine && current_ >= 0)
                p.Text(lyrics_.lines[static_cast<size_t>(current_)].text,
                       {left.x + 48, cover.Bottom() + (s.album.empty() ? 106.0f : 138.0f), left.w - 96, 24},
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
        scroll_.To(static_cast<float>(line), dist > 3 ? 260.0f : 340.0f, Ease::InOut);
        Invalidate();
    }

    // Line heights for the column width; measured again only when the width or the lyrics change.
    void EnsureLayout(Painter& p, float w) {
        if (w == layoutW_) return;
        layoutW_ = w;
        const size_t n = lyrics_.lines.size();
        tops_.assign(n, 0);
        heights_.assign(n, 0);
        lineRects_.assign(n, Rect{});
        const TextStyle style = lyrics_.synced ? TextStyle::Section : TextStyle::Large;
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
        for (size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const Rect r{box.x, std::round(offset + tops_[i]), box.w, heights_[i]};
            lineRects_[i] = {};
            if (r.Bottom() < box.y || r.y > box.Bottom()) continue;
            const LyricLine& line = lyrics_.lines[i];
            float a = 0.72f;
            if (synced) {
                const int at = static_cast<int>(i);
                const float d = static_cast<float>(std::abs(at - current_));
                a = at == current_ ? 0.96f : std::max(0.18f, 0.46f - 0.05f * d);
                if (at == hovered_) a = std::max(a, 0.78f);
                // Fade toward the top and bottom edges of the column.
                const float edge = std::min(r.Cy() - box.y, box.Bottom() - r.Cy());
                if (edge < 72) a *= std::max(0.0f, edge / 72);
                if (line.time >= 0) lineRects_[i] = r;
            }
            // A pause in synced lyrics shows as dots; a stanza break in plain text stays blank.
            if (line.text.empty() && !synced) continue;
            const std::wstring_view text = line.text.empty() ? std::wstring_view(L"\u00B7 \u00B7 \u00B7") : line.text;
            p.Text(text, r, synced ? TextStyle::Section : TextStyle::Large, Color::Mix(bg, white, a),
                   HAlign::Leading, VAlign::Top, gfx::kTextWrap);
        }
        p.PopClip();
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
        const D2D1_SIZE_F bs = bmp->GetSize();
        if (blur_ && bs.width > 0 && bs.height > 0) {
            // Cover the view with the image, centered on the cover (slightly larger, like the design);
            // with lyrics the cover sits left of center, so the image must reach the far edge too.
            const float halfW = std::max(cover.Cx() - rect.x, rect.Right() - cover.Cx());
            const float halfH = std::max(cover.Cy() - rect.y, rect.Bottom() - cover.Cy());
            const float k = std::max(2 * halfW / bs.width, 2 * halfH / bs.height) * 1.15f;
            const float w = bs.width * k, h = bs.height * k;
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                              D2D1::Matrix3x2F::Translation(cover.Cx() - w / 2, cover.Cy() - h / 2) * old);
            gfx::ComPtr<ID2D1Image> out;
            blur_->GetOutput(&out);
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
    gfx::ComPtr<ID2D1Effect> blur_;
    gfx::ComPtr<ID2D1RadialGradientBrush> glow_, vignette_;
    gfx::ComPtr<ID2D1SolidColorBrush> grooves_;
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
