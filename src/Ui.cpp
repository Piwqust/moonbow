#include "Ui.h"

#include "Theme.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

using namespace theme;

namespace {

bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

D2D1_RECT_F Inflate(const D2D1_RECT_F& r, float dx, float dy) {
    return {r.left - dx, r.top - dy, r.right + dx, r.bottom + dy};
}

float Clamp01(double v) {
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

bool HasTracks(const PlayerState* p, const wchar_t* type) {
    if (!p) return false;
    for (const Track& t : p->tracks)
        if (t.type == type) return true;
    return false;
}

}  // namespace

float Ui::TitleHeight(bool fullscreen) {
    return fullscreen ? 0.0f : kTitleH;
}

bool Ui::Init(Renderer& renderer) {
    renderer_ = &renderer;
    const wchar_t* ui = renderer.HasFontFamily(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t* icons =
        renderer.HasFontFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

    auto make = [&](const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                    bool ellipsis) {
        return renderer.CreateFormat(family, size, weight, align, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, ellipsis);
    };
    fmtTitle_ = make(ui, kFontUi, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    fmtLabel_ = make(ui, kFontUi, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fmtCenter_ = make(ui, kFontUi, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fmtTimeLeft_ = make(ui, kFontUi, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
    fmtTimeRight_ = make(ui, kFontUi, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fmtEmpty_ = make(ui, kFontEmpty, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fmtIcon_ = make(icons, kFontIcon, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fmtIconLarge_ = make(icons, 40.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fmtIconSmall_ = make(icons, kFontGlyphSmall, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    return fmtTitle_ && fmtLabel_ && fmtIcon_;
}

void Ui::Layout(const UiModel& m) {
    const float W = m.width, H = m.height;
    const float th = TitleHeight(m.fullscreen);

    title_ = {0, 0, W, th};
    close_ = {W - kCaptionBtnW, 0, W, th};
    maximize_ = {close_.left - kCaptionBtnW, 0, close_.left, th};
    minimize_ = {maximize_.left - kCaptionBtnW, 0, maximize_.left, th};

    bar_ = {0, H - kBarH, W, H};
    video_ = m.fullscreen ? D2D1_RECT_F{0, 0, W, H} : D2D1_RECT_F{0, th, W, H - kBarH};

    const float cy = bar_.top + kBarH / 2;
    auto btn = [&](float x) { return D2D1_RECT_F{x, cy - kBtn / 2, x + kBtn, cy + kBtn / 2}; };

    float x = kPad;
    prev_ = btn(x);
    x += kBtn + kGap;
    play_ = {x, cy - kBtn / 2, x + kPlayW, cy + kBtn / 2};
    x += kPlayW + kGap;
    next_ = btn(x);
    x += kBtn + kPad;

    withHours_ = m.player && m.player->duration >= 3600;
    const float tw = withHours_ ? 50.0f : 38.0f;
    timeLeft_ = {x, cy - 10, x + tw, cy + 10};
    x += tw + 10;

    float r = W - kPad;
    fullscreen_ = btn(r - kBtn);
    r -= kBtn + kGap;
    subs_ = btn(r - kBtn);
    r -= kBtn + kGap;
    audio_ = btn(r - kBtn);
    r -= kBtn + 8;
    volume_ = {r - kVolW, cy - kVolH / 2, r, cy + kVolH / 2};
    r -= kVolW + 6;
    mute_ = btn(r - kBtn);
    r -= kBtn + kPad;
    timeRight_ = {r - tw, cy - 10, r, cy + 10};
    r -= tw + 10;

    seek_ = {x, cy - kSeekHit / 2, std::max(x, r), cy + kSeekHit / 2};

    const float cx = (video_.left + video_.right) / 2;
    const float vy = (video_.top + video_.bottom) / 2;
    open_ = {cx - 80, vy + 30, cx + 80, vy + 66};
}

D2D1_RECT_F Ui::MenuRect(const UiModel& m) const {
    const TrackMenu& menu = *m.menu;
    const D2D1_RECT_F& anchor = menu.anchor == Hit::Audio ? audio_ : subs_;
    const float h = 8 + kMenuItemH * static_cast<float>(menu.items.size() + 1);
    float right = std::min(m.width - 8, anchor.right);
    float left = std::max(8.0f, right - kMenuW);
    float bottom = bar_.top - 6;
    float top = std::max(video_.top + 8, bottom - h);
    return {left, top, right, bottom};
}

Hit Ui::HitTest(float x, float y, const UiModel& m, int* menuIndex) const {
    if (menuIndex) *menuIndex = -1;
    const bool loaded = m.player && m.player->fileLoaded;

    if (m.menu && m.menu->open) {
        D2D1_RECT_F mr = MenuRect(m);
        if (Contains(mr, x, y)) {
            int idx = static_cast<int>(std::floor((y - (mr.top + 4 + kMenuItemH)) / kMenuItemH));
            if (idx >= 0 && idx < static_cast<int>(m.menu->items.size())) {
                if (menuIndex) *menuIndex = idx;
                return Hit::MenuItem;
            }
            return Hit::Menu;
        }
    }

    if (!m.fullscreen && Contains(title_, x, y)) {
        if (Contains(close_, x, y)) return Hit::Close;
        if (Contains(maximize_, x, y)) return Hit::Maximize;
        if (Contains(minimize_, x, y)) return Hit::Minimize;
        return Hit::Caption;
    }

    const bool barVisible = !m.fullscreen || m.barOpacity > 0.05f;
    if (barVisible && Contains(bar_, x, y)) {
        if (Contains(prev_, x, y)) return Hit::Prev;
        if (Contains(play_, x, y)) return Hit::Play;
        if (Contains(next_, x, y)) return Hit::Next;
        if (loaded && m.player->duration > 0 && Contains(seek_, x, y)) return Hit::Seek;
        if (Contains(mute_, x, y)) return Hit::Mute;
        if (Contains(Inflate(volume_, 2, 9), x, y)) return Hit::Volume;
        if (Contains(audio_, x, y)) return Hit::Audio;
        if (Contains(subs_, x, y)) return Hit::Subs;
        if (Contains(fullscreen_, x, y)) return Hit::Fullscreen;
        return Hit::ControlBar;
    }

    if (!loaded && Contains(open_, x, y)) return Hit::Open;
    if (Contains(video_, x, y)) return Hit::Video;
    return Hit::None;
}

double Ui::SeekFraction(float x) const {
    float w = seek_.right - seek_.left;
    return w > 0 ? Clamp01((x - seek_.left) / w) : 0.0;
}

double Ui::VolumeFromX(float x) const {
    return Clamp01((x - volume_.left) / kVolW) * 100.0;
}

D2D1_COLOR_F Ui::Fade(D2D1_COLOR_F c) const {
    c.a *= fade_;
    return c;
}

void Ui::Fill(const D2D1_RECT_F& r, D2D1_COLOR_F color, float radius) {
    brush_->SetColor(Fade(color));
    if (radius > 0)
        ctx_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    else
        ctx_->FillRectangle(r, brush_.Get());
}

void Ui::DrawText(const std::wstring& text, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, D2D1_COLOR_F color) {
    if (text.empty() || !fmt) return;
    brush_->SetColor(Fade(color));
    ctx_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, r, brush_.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_NONE);
}

D2D1_SIZE_F Ui::Measure(const std::wstring& text, IDWriteTextFormat* fmt, float maxWidth) const {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(renderer_->DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, maxWidth,
                                                     100.0f, &layout)))
        return {0, 0};
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return {std::ceil(tm.widthIncludingTrailingWhitespace), std::ceil(tm.height)};
}

void Ui::Draw(ID2D1DeviceContext* ctx, const UiModel& m) {
    ctx_ = ctx;
    if (FAILED(ctx->CreateSolidColorBrush(kText, &brush_))) return;

    fade_ = 1.0f;
    if (!m.fullscreen) DrawTitleBar(m);
    if (!m.player || !m.player->fileLoaded) DrawEmptyState(m);

    fade_ = m.fullscreen ? m.barOpacity : 1.0f;
    if (fade_ > 0.01f) {
        DrawControlBar(m);
        DrawMenu(m);
    }

    fade_ = m.toastOpacity;
    if (fade_ > 0.01f) DrawToast(m);

    brush_.Reset();
    ctx_ = nullptr;
}

void Ui::DrawTitleBar(const UiModel& m) {
    Fill(title_, kChrome);

    const float th = title_.bottom;
    Fill({14, th / 2 - 4, 22, th / 2 + 4}, kAccent, 2);
    DrawText(L"Lumen", {30, 0, 140, th}, fmtLabel_.Get(), kTextDim);

    std::wstring title = m.player && !m.player->title.empty() ? m.player->title : std::wstring();
    DrawText(title, {150, 0, m.width - 150, th}, fmtTitle_.Get(), m.active ? kText : kTextDim);

    struct Btn {
        const D2D1_RECT_F* rect;
        Hit id;
        const wchar_t* glyph;
    };
    const Btn buttons[] = {
        {&minimize_, Hit::Minimize, glyph::kMinimize},
        {&maximize_, Hit::Maximize, m.maximized ? glyph::kRestore : glyph::kMaximize},
        {&close_, Hit::Close, glyph::kClose},
    };
    for (const Btn& b : buttons) {
        const bool hot = m.hover == b.id;
        const bool down = hot && m.pressed == b.id;
        if (hot) Fill(*b.rect, b.id == Hit::Close ? kDanger : (down ? kPress : kHover));
        D2D1_COLOR_F color = hot ? kText : (m.active ? kText : kTextDim);
        if (hot && b.id == Hit::Close) color = Rgb(0xFFFFFF);
        DrawText(b.glyph, *b.rect, fmtIconSmall_.Get(), color);
    }
}

void Ui::DrawEmptyState(const UiModel& m) {
    Fill(video_, kBg);
    const float cx = (video_.left + video_.right) / 2;
    const float cy = (video_.top + video_.bottom) / 2;

    if (!m.engineError.empty()) {
        DrawText(m.engineError, {video_.left + 24, cy - 20, video_.right - 24, cy + 20}, fmtEmpty_.Get(), kTextDim);
        return;
    }
    if (video_.bottom - video_.top < 160) return;

    DrawText(glyph::kPlay, {cx - 40, cy - 86, cx + 40, cy - 30}, fmtIconLarge_.Get(), kTrack);
    DrawText(L"Перетащите файл сюда или нажмите Ctrl+O", {video_.left + 24, cy - 18, video_.right - 24, cy + 10},
             fmtEmpty_.Get(), kTextDim);

    const bool hot = m.hover == Hit::Open;
    Fill(open_, hot ? (m.pressed == Hit::Open ? kPress : kHover) : Rgb(0xFFFFFF, 0.04f), kRadius);
    brush_->SetColor(Fade(kLine));
    ctx_->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(open_, -0.5f, -0.5f), kRadius, kRadius), brush_.Get(), 1.0f);
    DrawText(glyph::kOpenFile, {open_.left + 14, open_.top, open_.left + 34, open_.bottom}, fmtIcon_.Get(), kAccent);
    DrawText(L"Открыть файл", {open_.left + 30, open_.top, open_.right - 6, open_.bottom}, fmtCenter_.Get(), kText);
}

void Ui::DrawIconButton(const D2D1_RECT_F& r, const wchar_t* glyph, Hit id, const UiModel& m, bool enabled,
                        bool active) {
    const bool hot = enabled && m.hover == id;
    if (hot) Fill(r, m.pressed == id ? kPress : kHover, kRadius);
    D2D1_COLOR_F color = !enabled ? Rgb(0x8F8F8F, 0.35f) : active ? kAccent : hot ? kText : kTextDim;
    DrawText(glyph, r, fmtIcon_.Get(), color);
}

void Ui::DrawControlBar(const UiModel& m) {
    const PlayerState* p = m.player;
    const bool loaded = p && p->fileLoaded;
    const float cy = bar_.top + kBarH / 2;

    Fill(bar_, m.fullscreen ? kChromeOverlay : kChrome);

    DrawIconButton(prev_, glyph::kPrev, Hit::Prev, m, m.hasPrev);

    const D2D1_RECT_F playBox = {play_.left, cy - kPlayH / 2, play_.right, cy + kPlayH / 2};
    D2D1_COLOR_F playColor = !loaded ? kTrack : (m.hover == Hit::Play ? kAccentHover : kAccent);
    Fill(playBox, playColor, kRadius);
    const bool showPlay = !loaded || p->paused;
    DrawText(showPlay ? glyph::kPlay : glyph::kPause, playBox, fmtIcon_.Get(), loaded ? kOnAccent : kTextDim);

    DrawIconButton(next_, glyph::kNext, Hit::Next, m, m.hasNext);

    const double pos = m.seekDragging ? m.seekDragPos : (p ? p->position : 0);
    DrawText(loaded ? FormatTime(pos, withHours_) : L"--:--", timeLeft_, fmtTimeLeft_.Get(), loaded ? kText : kTextDim);
    DrawText(loaded && p->duration > 0 ? FormatTime(p->duration, withHours_) : L"--:--", timeRight_,
             fmtTimeRight_.Get(), kTextDim);

    DrawSeek(m);

    const bool ready = m.engineReady;
    DrawIconButton(mute_, p && p->mute ? glyph::kMute : glyph::kVolume, Hit::Mute, m, ready);
    DrawVolume(m);

    const bool menuOpen = m.menu && m.menu->open;
    DrawIconButton(audio_, glyph::kAudio, Hit::Audio, m, HasTracks(p, L"audio"),
                   menuOpen && m.menu->anchor == Hit::Audio);
    DrawIconButton(subs_, glyph::kSubtitles, Hit::Subs, m, HasTracks(p, L"sub"),
                   menuOpen && m.menu->anchor == Hit::Subs);
    DrawIconButton(fullscreen_, m.fullscreen ? glyph::kExitFull : glyph::kFullscreen, Hit::Fullscreen, m, true);
}

void Ui::DrawSeek(const UiModel& m) {
    const PlayerState* p = m.player;
    const bool loaded = p && p->fileLoaded && p->duration > 0;
    const bool hot = loaded && (m.hover == Hit::Seek || m.seekDragging);
    const float cy = (seek_.top + seek_.bottom) / 2;
    const float th = hot ? kSeekTrackHot : kSeekTrack;
    const D2D1_RECT_F track = {seek_.left, cy - th / 2, seek_.right, cy + th / 2};
    if (track.right - track.left < 4) return;

    Fill(track, kTrack, th / 2);
    if (!loaded) return;

    const double pos = m.seekDragging ? m.seekDragPos : p->position;
    const float fx = track.left + (track.right - track.left) * Clamp01(pos / p->duration);
    if (fx > track.left + 1) Fill({track.left, track.top, fx, track.bottom}, kAccentDim, th / 2);

    const float tx = std::clamp(fx, track.left + kThumbW / 2, track.right - kThumbW / 2);
    Fill({tx - kThumbW / 2, cy - kThumbH / 2, tx + kThumbW / 2, cy + kThumbH / 2}, kAccent, 2);

    if (hot) {
        const double t = m.seekDragging ? m.seekDragPos : SeekFraction(m.mouseX) * p->duration;
        const std::wstring label = FormatTime(t, withHours_);
        const D2D1_SIZE_F size = Measure(label, fmtCenter_.Get(), 200);
        const float w = size.width + 16, h = 22;
        const float x = std::clamp(m.seekDragging ? fx : m.mouseX, seek_.left + w / 2, seek_.right - w / 2);
        const D2D1_RECT_F tip = {x - w / 2, bar_.top - h - 6, x + w / 2, bar_.top - 6};
        Fill(tip, kChromeOverlay, 4);
        DrawText(label, tip, fmtCenter_.Get(), kText);
    }
}

void Ui::DrawVolume(const UiModel& m) {
    const PlayerState* p = m.player;
    const double volume = p ? std::clamp(p->volume, 0.0, 100.0) : 100.0;

    ComPtr<ID2D1PathGeometry> wedge;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(renderer_->D2DFactory()->CreatePathGeometry(&wedge)) || FAILED(wedge->Open(&sink))) return;
    sink->BeginFigure({volume_.left, volume_.bottom}, D2D1_FIGURE_BEGIN_FILLED);
    const D2D1_POINT_2F pts[] = {{volume_.right, volume_.bottom}, {volume_.right, volume_.top}};
    sink->AddLines(pts, 2);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    brush_->SetColor(Fade(kTrack));
    ctx_->FillGeometry(wedge.Get(), brush_.Get());

    const float fx = volume_.left + kVolW * static_cast<float>(volume / 100.0);
    if (fx > volume_.left) {
        ctx_->PushAxisAlignedClip({volume_.left, volume_.top - 1, fx, volume_.bottom + 1},
                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        brush_->SetColor(Fade(p && p->mute ? kTextDim : kAccent));
        ctx_->FillGeometry(wedge.Get(), brush_.Get());
        ctx_->PopAxisAlignedClip();
    }

    if (m.hover == Hit::Volume || m.pressed == Hit::Volume) {
        const std::wstring label = std::to_wstring(static_cast<int>(std::lround(volume))) + L"%";
        const float w = 44, h = 22;
        const float x = (volume_.left + volume_.right) / 2;
        const D2D1_RECT_F tip = {x - w / 2, bar_.top - h - 6, x + w / 2, bar_.top - 6};
        Fill(tip, kChromeOverlay, 4);
        DrawText(label, tip, fmtCenter_.Get(), kText);
    }
}

void Ui::DrawMenu(const UiModel& m) {
    if (!m.menu || !m.menu->open) return;
    const TrackMenu& menu = *m.menu;
    const D2D1_RECT_F r = MenuRect(m);

    Fill(r, kChrome, 8);
    brush_->SetColor(Fade(kLine));
    ctx_->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(r, -0.5f, -0.5f), 8, 8), brush_.Get(), 1.0f);

    float y = r.top + 4;
    DrawText(menu.header, {r.left + 14, y, r.right - 14, y + kMenuItemH}, fmtLabel_.Get(), kTextDim);
    y += kMenuItemH;
    for (size_t i = 0; i < menu.items.size(); ++i) {
        const MenuItem& item = menu.items[i];
        const D2D1_RECT_F row = {r.left + 4, y, r.right - 4, y + kMenuItemH};
        if (menu.hover == static_cast<int>(i)) Fill(row, kHover, 4);
        if (item.checked) DrawText(glyph::kCheck, {r.left + 8, y, r.left + 30, y + kMenuItemH}, fmtIconSmall_.Get(), kAccent);
        DrawText(item.label, {r.left + 34, y, r.right - 14, y + kMenuItemH}, fmtLabel_.Get(),
                 item.checked ? kText : kTextDim);
        y += kMenuItemH;
    }
}

void Ui::DrawToast(const UiModel& m) {
    if (m.toast.empty()) return;
    const D2D1_SIZE_F size = Measure(m.toast, fmtLabel_.Get(), std::max(100.0f, video_.right - video_.left - 64));
    const D2D1_RECT_F r = {video_.left + 16, video_.top + 16, video_.left + 16 + size.width + 28, video_.top + 48};
    Fill(r, kChromeOverlay, kRadius);
    DrawText(m.toast, {r.left + 14, r.top, r.right - 14, r.bottom}, fmtLabel_.Get(), kText);
}
