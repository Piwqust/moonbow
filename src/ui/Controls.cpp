#include "ui/Controls.h"

#include "gfx/Text.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <map>

namespace ui {

namespace {

// A row holding one line of `style`: the design height, or the text plus the same air once Windows
// "Text size" makes the text taller.
float FitRow(float design, TextStyle style, float air) {
    return std::max(design, gfx::Text::Get().LineHeight(style) + air);
}

// Windows 11 menus use 32 to 36 DIP rows; 17 items stay under 650 DIP.
float MenuItemH() {
    return FitRow(36, TextStyle::Body, 16);
}
constexpr float kMenuSepH = 9;
constexpr float kMenuPad = 6;

// DebugInfo helpers (tree dumps only).
void AppendNumber(std::string& out, const char* key, double v) {
    char buf[64];
    snprintf(buf, sizeof buf, " %s=%g", key, v);
    out += buf;
}
void AppendIcon(std::string& out, const std::string& icon) {
    if (!icon.empty()) out += " icon=" + icon;
}

std::wstring ClipboardText(HWND owner) {
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

// The transport glyphs fill only 10 of the icon grid's 24 DIP; the outline icons fill about 17.
// In a column of menu icons they are drawn larger so both read at one size.
bool SmallGlyph(const std::string& icon) {
    return icon == "play" || icon == "pause" || icon == "stop" || icon == "previous" || icon == "next";
}

bool IsWordChar(wchar_t c) {
    return std::iswalnum(c) || c == L'_';
}

// One disabled look for Button, Dropdown, Segmented and Toggle: a flat fill between the page and
// a card, no border, text.disabled. Flatter than every live control in both themes.
Color DisabledFill() {
    return Color::Mix(P().bgBase, P().bgCard, 0.5f);
}

// Hover fades in fast and out a little slower, as Windows 11's own controls do; a press shows at
// once. Called from Paint: the host keeps drawing frames while the fade runs.
float HoverFade(AnimFloat& fade, bool hot) {
    const float to = hot ? 1.0f : 0.0f;
    if (fade.Target() != to) fade.To(to, hot ? 90.0f : 160.0f);
    return fade.Get();
}

}  // namespace

Color ToneColor(Tone t) {
    switch (t) {
        case Tone::Primary: return P().textPrimary;
        case Tone::Secondary: return P().textSecondary;
        case Tone::Tertiary: return P().textTertiary;
        case Tone::Disabled: return P().textDisabled;
        case Tone::Accent: return P().accent;
    }
    return P().textSecondary;
}

// --- Label ------------------------------------------------------------------------------

Label::Label(std::wstring text, TextStyle s, Tone t) : style(s), tone(t), text_(std::move(text)) {}

void Label::SetText(std::wstring text) {
    if (text == text_) return;
    text_ = std::move(text);
    InvalidateLayout();
}

Size Label::MeasureContent(float availW, float) {
    const float lh = gfx::Text::Get().LineHeight(style);
    if (text_.empty()) return {0, lh};
    if (wrap) {
        auto m = gfx::Text::Get().Measure(text_, style, std::max(1.0f, availW), gfx::kTextWrap);
        float h = std::max(lh, m.height);
        if (maxLines > 0) h = std::min(h, lh * static_cast<float>(maxLines));
        return {std::min(m.width, availW), h};
    }
    auto m = gfx::Text::Get().Measure(text_, style);
    return {std::min(m.width, availW), lh};
}

void Label::Paint(Painter& p) {
    const Color c = color ? color() : (IsEnabled() ? ToneColor(tone) : P().textDisabled);
    if (wrap && maxLines > 0) {
        // Only maxLines fit: DirectWrite trims the last one with "…". Without the cap a long unbroken
        // name (a 40 000-character file name) spilled over the whole window.
        Rect box = rect;
        box.h = std::min(box.h, gfx::Text::Get().LineHeight(style) * static_cast<float>(maxLines));
        box.y = valign == VAlign::Top ? rect.y : valign == VAlign::Center ? rect.Cy() - box.h / 2 : rect.y + rect.h - box.h;
        p.Text(text_, box, style, c, halign, valign, gfx::kTextWrap | gfx::kTextEllipsis);
        return;
    }
    p.Text(text_, rect, style, c, halign, valign, wrap ? gfx::kTextWrap : gfx::kTextEllipsis);
    if (!wrap) PaintTextMarks(p, text_, rect, style, halign, marks);
}

void PaintTextMarks(Painter& p, std::wstring_view text, const Rect& box, TextStyle style, HAlign h, const TextMarks& marks) {
    if (marks.empty() || text.empty()) return;
    const gfx::TextMetrics whole = p.Measure(text, style);
    const float x0 = h == HAlign::Center ? box.x + (box.w - whole.width) / 2 : h == HAlign::Trailing ? box.Right() - whole.width : box.x;
    const float y = box.Cy() + whole.height / 2 - 1;
    for (const auto& [at, len] : marks) {
        if (at >= text.size()) continue;
        const float a = x0 + (at ? std::round(p.Measure(text.substr(0, at), style).width) : 0.0f);
        const float b = x0 + std::ceil(p.Measure(text.substr(0, std::min(text.size(), at + len)), style).width);
        const float w = std::min(b, box.Right()) - a;
        if (w > 0) p.Fill({a, y, w, 2}, P().accent, 1);
    }
}

// --- IconView / Divider -------------------------------------------------------------------

IconView::IconView(std::string i, int s, Tone t) : icon(std::move(i)), size(static_cast<float>(s)), tone(t) {}

void IconView::Paint(Painter& p) {
    p.Icon(icon, rect.Center(size, size), color ? color() : ToneColor(tone));
}

Divider::Divider(bool v) : vertical(v) {
    if (vertical) {
        width = Length::Px(1);
        height = Length::Grow();
    } else {
        height = Length::Px(1);
        width = Length::Grow();
    }
}

void Divider::Paint(Painter& p) {
    p.Fill(rect, P().line);
}

// --- Button -------------------------------------------------------------------------------

Button::Button(std::wstring text, ButtonKind k, std::string i) : kind(k), icon(std::move(i)), text_(std::move(text)) {
    interactive = true;
    focusable = true;
    height = Length::Px(32);
}

void Button::SetText(std::wstring text) {
    text_ = std::move(text);
    InvalidateLayout();
}

Size Button::MeasureContent(float, float) {
    float w = gfx::Text::Get().Measure(text_, style).width + 32;
    if (!icon.empty()) w += text_.empty() ? 20 - 32 + 12 : 28;
    return {w, FitRow(32, style, 12)};
}

void Button::Paint(Painter& p) {
    const bool on = IsEnabled();
    const bool hot = on && IsHot(), down = on && IsPressed();
    const float h = HoverFade(hover_, hot);
    const Palette& c = P();
    Color bg = Color::Clear(), fg = c.textSecondary;
    switch (kind) {
        case ButtonKind::Primary:
            bg = on ? (down ? Color::Mix(c.accent, c.bgBase, 0.22f) : Color::Mix(c.accent, c.bgBase, 0.1f * h)) : DisabledFill();
            fg = on ? c.onAccent : c.textDisabled;
            break;
        case ButtonKind::Secondary:
            // Hover and press step towards the text colour: a visible change in both themes.
            bg = !on ? DisabledFill() : down ? Color::Mix(c.bgPanel, c.textPrimary, 0.2f)
                                             : Color::Mix(c.bgPanel, c.textPrimary, 0.06f + 0.06f * h);
            fg = on ? c.textPrimary : c.textDisabled;
            break;
        case ButtonKind::Ghost:
            bg = down ? c.press : c.hover.Fade(h);
            fg = !on ? c.textDisabled : Color::Mix(c.textSecondary, c.textPrimary, h);
            break;
        case ButtonKind::Outline:
            bg = !on ? DisabledFill() : down ? c.press : Color::Mix(c.textPrimary.Fade(0.03f), c.hover, h);
            fg = on ? c.textPrimary : c.textDisabled;
            break;
    }
    p.Fill(rect, bg, 4);
    if (kind == ButtonKind::Outline && on) p.Stroke(rect, c.lineStrong, 4);
    PaintFocusRing(p, rect, 4);

    const float tw = gfx::Text::Get().Measure(text_, style).width;
    const float iw = icon.empty() ? 0.0f : 20.0f;
    const float total = tw + iw + (iw > 0 && tw > 0 ? 8 : 0);
    float x = rect.Cx() - total / 2;
    if (!icon.empty()) {
        p.Icon(icon, {x, rect.Cy() - 10, 20, 20}, fg);
        x += 28;
    }
    if (!text_.empty()) p.Text(text_, {x, rect.y, tw + 1, rect.h}, style, fg, HAlign::Leading, VAlign::Center, 0);
}

// --- IconButton -------------------------------------------------------------------------

IconButton::IconButton(std::string i, std::wstring tip, int box, int isz) : icon(std::move(i)), iconSize(static_cast<float>(isz)) {
    interactive = true;
    focusable = true;
    tooltip = std::move(tip);
    width = Length::Px(static_cast<float>(box));
    height = Length::Px(static_cast<float>(box));
}

void IconButton::Paint(Painter& p) {
    const bool on = IsEnabled();
    const bool hot = on && IsHot(), down = on && IsPressed();
    const float h = HoverFade(hover_, hot);
    const float r = round ? rect.w / 2 : 4;
    if (down)
        p.Fill(rect, P().press, r);
    else if (h > 0)
        p.Fill(rect, P().hover.Fade(h), r);
    PaintFocusRing(p, rect, r);
    Color c = !on ? P().textDisabled : active ? P().accent : Color::Mix(ToneColor(tone), P().textPrimary, h);
    p.Icon(icon, rect.Center(iconSize, iconSize), c, active ? P().accent : P().accentMid);
}

// --- LinkButton ---------------------------------------------------------------------------

LinkButton::LinkButton(std::wstring t, TextStyle s) : text(std::move(t)), style(s) {
    interactive = true;
    focusable = true;
    cursor = IDC_HAND;
}

Size LinkButton::MeasureContent(float, float) {
    float w = gfx::Text::Get().Measure(text, style).width;
    if (!trailingIcon.empty()) w += 24;
    return {w, std::max(gfx::Text::Get().LineHeight(style), 24.0f)};
}

void LinkButton::Paint(Painter& p) {
    const float h = HoverFade(hover_, IsHot());
    const Color c = Color::Mix(P().textSecondary, P().textPrimary, h);
    const float tw = gfx::Text::Get().Measure(text, style).width;
    p.Text(text, {rect.x, rect.y, tw + 1, rect.h}, style, c, HAlign::Leading, VAlign::Center, 0);
    // Grey text that only turns white reads as a label: the underline says it is a link. 1 DIP
    // and faded, so it is not taken for the 2 DIP search-match mark (PaintTextMarks). It draws
    // from the left with the hover.
    if (h > 0) p.Fill({rect.x, std::round(rect.Cy() + p.Measure(text, style).height / 2 - 1), std::round(tw * h), 1}, c.Fade(0.6f));
    if (!trailingIcon.empty()) p.Icon(trailingIcon, {rect.x + tw + 4, rect.Cy() - 10, 20, 20}, c);
    PaintFocusRing(p, rect, 4);
}

// --- Toggle -------------------------------------------------------------------------------

Toggle::Toggle(bool on) : on_(on), knob_(on ? 1.0f : 0.0f) {
    interactive = true;
    focusable = true;
}

void Toggle::SetValue(bool on, bool animate) {
    if (on == on_) return;
    on_ = on;
    if (animate)
        knob_.To(on ? 1.0f : 0.0f, 160, Ease::Out);
    else
        knob_.Set(on ? 1.0f : 0.0f);
    Invalidate();
}

void Toggle::OnClick() {
    SetValue(!on_);
    if (onChange) onChange(on_);
}

namespace {
std::wstring g_menuNoMatch = L"Nothing found";
std::wstring g_textMenu[4] = {L"Cut", L"Copy", L"Paste", L"Select all"};
}  // namespace

void SetMenuNoMatchCaption(std::wstring text) {
    g_menuNoMatch = std::move(text);
}

void SetTextMenuCaptions(std::wstring cut, std::wstring copy, std::wstring paste, std::wstring selectAll) {
    g_textMenu[0] = std::move(cut);
    g_textMenu[1] = std::move(copy);
    g_textMenu[2] = std::move(paste);
    g_textMenu[3] = std::move(selectAll);
}

Size Toggle::MeasureContent(float, float) {
    const float s = gfx::Text::Get().Scale();
    return {48 * s, 32 * s};  // a bigger target than the 40×22 track
}

// After shadcn/ui and Apple's switches: a filled pill and a large round thumb that nearly fills
// it. Off is a grey pill with a light thumb (dark theme) or a white one (light theme), on is the
// accent pill with a thumb in the accent's ink. The thumb is >= 3:1 on its track in both states,
// and its side says the state, not only the color.
void Toggle::Paint(Painter& p) {
    const Palette& c = P();
    const float k = knob_.Get();
    const float s = gfx::Text::Get().Scale();
    const Rect box = rect.Center(40 * s, 22 * s);
    const float r = box.h / 2;
    const bool live = IsEnabled();
    const bool hot = live && IsHot(), down = live && IsPressed();
    const bool dark = Theme::Get().Mode() == ThemeMode::Dark;
    const Color offTrack = Color::Mix(c.bgBase, c.textPrimary, (dark ? 0.24f : 0.44f) + (hot ? 0.06f : 0.0f));
    const Color onTrack = hot ? Color::Mix(c.accent, c.bgBase, 0.1f) : c.accent;
    const Color offThumb = dark ? c.textPrimary : Color::Hex(0xFFFFFF);
    if (live)
        p.Fill(box, Color::Mix(offTrack, onTrack, k), r);
    else
        p.Fill(box, DisabledFill(), r);
    PaintFocusRing(p, box, r);

    // The thumb stretches towards the far side while pressed (iOS, Windows 11).
    const float d = 18 * s, kw = d + (down ? 4 * s : 0);
    const float inset = (box.h - d) / 2;
    const Rect knob{box.x + inset + (box.w - 2 * inset - kw) * k, box.y + inset, kw, d};
    if (!live) {
        p.Stroke(knob.Inset(0.75f), c.textDisabled, d / 2, 1.5f);
        return;
    }
    if (!dark) p.Shadow(knob, d / 2, 3, 1, 0.22f);
    p.Fill(knob, Color::Mix(offThumb, c.onAccent, k), d / 2);
}

// --- Checkbox -------------------------------------------------------------------------------

Checkbox::Checkbox(std::wstring text, bool checked) : text_(std::move(text)), checked_(checked) {
    interactive = true;
    focusable = true;
}

Size Checkbox::MeasureContent(float availW, float) {
    float w = 20 + (text_.empty() ? 0 : 10 + gfx::Text::Get().Measure(text_, TextStyle::Body).width);
    return {std::min(w, availW), FitRow(24, TextStyle::Body, 4)};
}

void Checkbox::OnClick() {
    checked_ = !checked_;
    if (onChange) onChange(checked_);
    Invalidate();
}

void Checkbox::Paint(Painter& p) {
    const Palette& c = P();
    const Rect box{rect.x + 1, rect.Cy() - 9, 18, 18};
    if (checked_) {
        p.Fill(box, c.accent, kRadiusControl);
        p.Icon("check_selected", box.Inset(1), c.onAccent);
    } else {
        p.Fill(box, c.bgDeep, kRadiusControl);
        p.Stroke(box, IsHot() ? c.textSecondary : c.lineControl, kRadiusControl);
    }
    PaintFocusRing(p, box, kRadiusControl);
    if (!text_.empty())
        p.Text(text_, {box.Right() + 10, rect.y, rect.Right() - box.Right() - 10, rect.h}, TextStyle::Body,
               IsEnabled() ? (IsHot() ? c.textPrimary : c.textSecondary) : c.textDisabled);
}

// --- Segmented ------------------------------------------------------------------------------

Segmented::Segmented(std::vector<Choice> choices, int selected) : choices_(std::move(choices)), selected_(selected) {
    interactive = true;
    focusable = true;
}

void Segmented::SetSelected(int i) {
    selected_ = i;
    Invalidate();
}

Size Segmented::MeasureContent(float availW, float) {
    float w = 4;
    for (auto& ch : choices_) w += std::max(segmentWidth, gfx::Text::Get().Measure(ch.label, TextStyle::Body).width + 24) + 2;
    return {std::min(w, availW), FitRow(36, TextStyle::Body, 16)};
}

int Segmented::IndexAt(float x) const {
    if (choices_.empty()) return -1;
    const float segW = (rect.w - 4 - 2 * static_cast<float>(choices_.size() - 1)) / static_cast<float>(choices_.size());
    int i = static_cast<int>((x - rect.x - 2) / (segW + 2));
    return std::clamp(i, 0, static_cast<int>(choices_.size()) - 1);
}

bool Segmented::OnMouseMove(const MouseEvent& e) {
    int i = IndexAt(e.x);
    if (i != hoverIndex_) {
        hoverIndex_ = i;
        Invalidate();
    }
    return false;
}

bool Segmented::OnMouseDown(const MouseEvent& e) {
    int i = IndexAt(e.x);
    if (i >= 0 && i != selected_) {
        selected_ = i;
        if (onChange) onChange(i);
        Invalidate();
    }
    return true;
}

bool Segmented::OnKeyDown(const KeyEvent& e) {
    if ((e.key != VK_LEFT && e.key != VK_RIGHT) || choices_.empty()) return false;
    int i = std::clamp(selected_ + (e.key == VK_LEFT ? -1 : 1), 0, static_cast<int>(choices_.size()) - 1);
    if (i != selected_) {
        selected_ = i;
        if (onChange) onChange(i);
        a11y::FocusChanged(GetHost());
    }
    return true;
}

Rect Segmented::SegmentRect(int i) const {
    const float n = static_cast<float>(choices_.size());
    const float segW = (rect.w - 4 - 2 * (n - 1)) / n;
    return {rect.x + 2 + static_cast<float>(i) * (segW + 2), rect.y + 2, segW, rect.h - 4};
}

void Segmented::Describe(A11y& a) {
    a.role = Role::Tab;
    a.items = static_cast<int>(choices_.size());
    a.item = [this](int i, A11y::Item& it) {
        it.role = Role::TabItem;
        it.name = choices_[static_cast<size_t>(i)].label;
        it.rect = SegmentRect(i);
        it.selected = i == selected_;
    };
    a.selectItem = [this](int i) {
        if (i == selected_) return;
        selected_ = i;
        if (onChange) onChange(i);
        Invalidate();
    };
    a.focusedItem = selected_;
}

void Segmented::Paint(Painter& p) {
    const Palette& c = P();
    const bool live = IsEnabled();
    p.Fill(rect, live ? c.bgDeep : DisabledFill(), kRadiusControl);
    PaintFocusRing(p, rect, kRadiusControl);
    if (choices_.empty()) return;
    for (size_t i = 0; i < choices_.size(); ++i) {
        const Rect seg = SegmentRect(static_cast<int>(i));
        const bool sel = static_cast<int>(i) == selected_;
        const bool hot = live && IsHot() && static_cast<int>(i) == hoverIndex_;
        const bool dark = Theme::Get().Mode() == ThemeMode::Dark;
        if (sel && !live) {
            p.Stroke(seg, c.lineStrong, kRadiusSmall);  // which value is set, without the live highlight
        } else if (sel) {
            p.Fill(seg, dark ? Color::Mix(c.bgPanel, c.accent, 0.14f) : c.bgPanel, kRadiusSmall);
            if (!dark) p.Stroke(seg, c.line, kRadiusSmall);
        } else if (hot) {
            p.Fill(seg, c.hover, kRadiusSmall);
        }
        const Color fg = !live ? c.textDisabled : sel ? c.textPrimary : hot ? c.textPrimary : c.textSecondary;
        float tx = seg.x;
        if (!choices_[i].icon.empty()) {
            const float tw = gfx::Text::Get().Measure(choices_[i].label, TextStyle::Body).width;
            tx = seg.Cx() - (tw + 28) / 2;
            p.Icon(choices_[i].icon, {tx, seg.Cy() - 10, 20, 20}, fg);
            p.Text(choices_[i].label, {tx + 28, seg.y, tw + 1, seg.h}, TextStyle::Body, fg, HAlign::Leading, VAlign::Center, 0);
        } else {
            p.Text(choices_[i].label, seg, TextStyle::Body, fg, HAlign::Center);
        }
    }
}

// --- Dropdown -------------------------------------------------------------------------------

Dropdown::Dropdown(std::vector<Choice> choices, int selected) : choices_(std::move(choices)), selected_(selected) {
    interactive = true;
    focusable = true;
}

void Dropdown::SetSelected(int i) {
    selected_ = i;
    Invalidate();
}

void Dropdown::SetChoices(std::vector<Choice> choices, int selected) {
    choices_ = std::move(choices);
    selected_ = selected;
    InvalidateLayout();
}

Size Dropdown::MeasureContent(float availW, float) {
    float w = 0;
    for (auto& ch : choices_) w = std::max(w, gfx::Text::Get().Measure(ch.label, TextStyle::Body).width);
    return {std::min(availW, std::max(160.0f, w + 56)), FitRow(36, TextStyle::Body, 16)};
}

void Dropdown::Paint(Painter& p) {
    const Palette& c = P();
    const bool open = menu_ && GetHost() && GetHost()->IsPopupOpen(menu_);
    const bool live = IsEnabled();
    if (live) {
        const bool dark = Theme::Get().Mode() == ThemeMode::Dark;
        if (!dark) p.Shadow(rect, kRadiusControl, 2, 1, 0.08f);
        // Dark: a faint veil over whatever the field sits on (page or panel); light: a white field.
        const Color white = Color::Hex(0xFFFFFF);
        p.Fill(rect, dark ? c.textPrimary.Fade(IsHot() || open ? 0.09f : 0.05f)
                          : (IsHot() || open ? Color::Mix(white, c.textPrimary, 0.04f) : white),
               kRadiusControl);
        p.Stroke(rect, open ? c.textTertiary : IsHot() ? c.lineStrong : c.lineControl, kRadiusControl);
    } else {
        p.Fill(rect, DisabledFill(), kRadiusControl);
    }
    PaintFocusRing(p, rect, kRadiusControl);
    const bool has = selected_ >= 0 && selected_ < static_cast<int>(choices_.size());
    const std::wstring& label = has ? choices_[static_cast<size_t>(selected_)].label : placeholder;
    const Color fg = !live ? c.textDisabled : has ? c.textPrimary : c.textTertiary;
    const Rect box{rect.x + 12, rect.y, rect.w - 40, rect.h};
    // A value cut by the ellipsis can be read in full in the tooltip (unless the owner set one).
    if (tooltip.empty() || autoTooltip_) {
        const bool cut = !label.empty() && p.Measure(label, TextStyle::Body).width > box.w;
        autoTooltip_ = cut;
        if (cut && tooltip != label) tooltip = label;
        else if (!cut && !tooltip.empty()) tooltip.clear();
    }
    p.Text(label, box, TextStyle::Body, fg);
    p.Icon(open ? "chevron_up" : "chevron_down", {rect.Right() - 28, rect.Cy() - 8, 16, 16},
           !live ? c.textDisabled : IsHot() || open ? c.textSecondary : c.textTertiary);
}

void Dropdown::OnClick() {
    Host* host = GetHost();
    if (!host) return;
    if (menu_ && host->IsPopupOpen(menu_)) {
        host->ClosePopup(menu_);
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < choices_.size(); ++i) {
        MenuItem m;
        m.label = choices_[i].label;
        m.icon = choices_[i].icon;
        m.checked = static_cast<int>(i) == selected_;
        m.action = [this, i] {
            if (static_cast<int>(i) == selected_) return;
            selected_ = static_cast<int>(i);
            if (onChange) onChange(selected_);
            Invalidate();
        };
        items.push_back(std::move(m));
    }
    menu_ = ShowMenu(host, rect, std::move(items), Place::Below, rect.w);
}

bool Dropdown::OnKeyDown(const KeyEvent& e) {
    if ((e.key != VK_UP && e.key != VK_DOWN) || choices_.empty()) return false;
    int i = std::clamp(selected_ + (e.key == VK_UP ? -1 : 1), 0, static_cast<int>(choices_.size()) - 1);
    if (i != selected_) {
        selected_ = i;
        if (onChange) onChange(i);
        Invalidate();
    }
    return true;
}

// --- Slider ---------------------------------------------------------------------------------

Slider::Slider(float mn, float mx, float v, float st) : min(mn), max(mx), step(st), value_(v) {
    interactive = true;
    focusable = true;
}

void Slider::SetValue(float v) {
    v = std::clamp(v, min, max);
    if (step > 0) v = min + std::round((v - min) / step) * step;
    value_ = v;
    Invalidate();
}

void Slider::SetFromX(float x) {
    const float t = std::clamp((x - rect.x - 6) / std::max(1.0f, rect.w - 12), 0.0f, 1.0f);
    const float old = value_;
    SetValue(min + (max - min) * t);
    if (value_ != old && onChange) onChange(value_);
}

bool Slider::OnMouseDown(const MouseEvent& e) {
    dragging_ = true;
    dragFrom_ = value_;
    SetFromX(e.x);
    return true;
}

bool Slider::OnMouseMove(const MouseEvent& e) {
    if (dragging_) SetFromX(e.x);
    return dragging_;
}

bool Slider::OnMouseUp(const MouseEvent& e) {
    if (dragging_ && e.cancelled) {
        // Capture lost mid-drag (Alt+Tab, a dialog): back to where the drag started, uncommitted.
        if (value_ != dragFrom_) {
            SetValue(dragFrom_);
            if (onChange) onChange(value_);
        }
    } else if (dragging_ && onCommit) {
        onCommit(value_);
    }
    dragging_ = false;
    Invalidate();
    return true;
}

bool Slider::OnKeyDown(const KeyEvent& e) {
    const float d = step > 0 ? step : (max - min) / 20;
    float v = value_;
    if (e.key == VK_LEFT || e.key == VK_DOWN) v -= d;
    else if (e.key == VK_RIGHT || e.key == VK_UP) v += d;
    else if (e.key == VK_HOME) v = min;
    else if (e.key == VK_END) v = max;
    else return false;
    SetValue(v);
    if (onChange) onChange(value_);
    if (onCommit) onCommit(value_);
    return true;
}

bool Slider::OnWheel(const WheelEvent& e) {
    if (!IsFocused()) return false;
    const float d = step > 0 ? step : (max - min) / 20;
    SetValue(value_ + d * e.delta);
    if (onChange) onChange(value_);
    if (onCommit) onCommit(value_);
    return true;
}

void Slider::Paint(Painter& p) {
    const Palette& c = P();
    const Rect track{rect.x + 6, rect.Cy() - 2, rect.w - 12, 4};
    p.Fill(track, c.bgDeep, 2);
    p.Stroke(track.Inset(-1), c.lineControl, 3);
    const float t = (max > min) ? (value_ - min) / (max - min) : 0;
    const float x = track.x + track.w * t;
    if (showFill) {
        if (centerFill) {
            const float mid = track.x + track.w * ((0 - min) / (max - min));
            p.Fill({std::min(mid, x), track.y, std::abs(x - mid), track.h}, c.accentMid, 2);
        } else {
            p.Fill({track.x, track.y, x - track.x, track.h}, c.accentMid, 2);
        }
    }
    const float r = dragging_ || IsHot() ? 7.0f : 6.0f;
    p.FillCircle({x, rect.Cy()}, r, IsEnabled() ? c.accent : c.textDisabled);
    if (ShowFocusRing()) p.Stroke({x - r - 3, rect.Cy() - r - 3, (r + 3) * 2, (r + 3) * 2}, c.focusRing, r + 3, 2.0f);
}

// --- TextBox --------------------------------------------------------------------------------

TextBox::TextBox(std::wstring ph, std::string icon) : placeholder(std::move(ph)), leadingIcon(std::move(icon)) {
    interactive = true;
    focusable = true;
    cursor = IDC_IBEAM;
    height = Length::Px(FitRow(34, TextStyle::Body, 14));
    // Cut, Copy, Paste and Select all, as in every Windows edit field: the same as their keys.
    onContextMenu = [this](const MouseEvent& e) {
        const bool sel = caret_ != anchor_ && !password;
        auto key = [this](UINT vk) {
            return [this, vk] {
                OnKeyDown({vk, {true, false, false}});
                Invalidate();
            };
        };
        std::vector<MenuItem> items;
        items.push_back({g_textMenu[0], L"Ctrl+X", "", false, sel && !readOnly, false, false, key('X')});
        items.push_back({g_textMenu[1], L"Ctrl+C", "", false, sel, false, false, key('C')});
        items.push_back({g_textMenu[2], L"Ctrl+V", "", false, !readOnly && IsClipboardFormatAvailable(CF_UNICODETEXT),
                         false, false, key('V')});
        items.push_back(MenuItem::Separator());
        items.push_back({g_textMenu[3], L"Ctrl+A", "", false, !text_.empty(), false, false, key('A')});
        ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
    };
}

void TextBox::SetText(std::wstring text, bool notify) {
    text_ = std::move(text);
    caret_ = anchor_ = text_.size();
    scroll_ = 0;
    if (notify && onChange) onChange(text_);
    Invalidate();
}

void TextBox::SelectAll() {
    anchor_ = 0;
    caret_ = text_.size();
    Invalidate();
}

std::wstring TextBox::Shown() const {
    return password ? std::wstring(text_.size(), L'•') : text_;
}

Rect TextBox::TextRect() const {
    float l = leadingIcon.empty() ? 12.0f : 40.0f;
    return {rect.x + l, rect.y, rect.w - l - 12, rect.h};
}

float TextBox::XOf(size_t index) const {
    const std::wstring shown = Shown();
    if (shown.empty() || index == 0) return 0;
    IDWriteTextLayout* layout = gfx::Text::Get().Layout(shown, TextStyle::Body, 100000.0f, rect.h, HAlign::Leading,
                                                        VAlign::Center, 0);
    if (!layout) return 0;
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestTextPosition(static_cast<UINT32>(index), FALSE, &x, &y, &m);
    return x;
}

size_t TextBox::IndexAt(float x) const {
    const std::wstring shown = Shown();
    if (shown.empty()) return 0;
    IDWriteTextLayout* layout = gfx::Text::Get().Layout(shown, TextStyle::Body, 100000.0f, rect.h, HAlign::Leading,
                                                        VAlign::Center, 0);
    if (!layout) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(x - TextRect().x + scroll_, rect.h / 2, &trailing, &inside, &m);
    return std::min<size_t>(shown.size(), m.textPosition + (trailing ? m.length : 0));
}

Rect TextBox::CaretRect() const {
    const Rect tr = TextRect();
    return {tr.x + XOf(caret_) - scroll_, rect.Cy() - 9, 1, 18};
}

bool TextBox::MenuPoint(Point& at) const {
    at = {std::clamp(CaretRect().x, rect.x, rect.Right()), rect.Bottom() - 4};
    return true;
}

// The Text pattern: screen readers read the character, word or selection at the caret.
void TextBox::Describe(A11y& a) {
    if (password) return;
    a.caret = caret_;
    a.anchor = anchor_;
    a.select = [this](size_t from, size_t to) {
        anchor_ = std::min(from, text_.size());
        MoveCaret(to, true);
    };
    a.textRect = [this](size_t from, size_t to) {
        const float x1 = XOf(from), x2 = XOf(to);
        return Rect{TextRect().x + x1 - scroll_, rect.Cy() - 10, std::max(1.0f, x2 - x1), 20};
    };
}

TextBox::~TextBox() {
    // A focused box can be destroyed with its popup or page: stop the caret timer,
    // unless its host is already gone.
    if (!blinkTimer_ || !blinkHost_) return;
    const auto& hosts = Host::All();
    if (std::find(hosts.begin(), hosts.end(), blinkHost_) != hosts.end()) blinkHost_->CancelTimer(blinkTimer_);
}

void TextBox::ResetBlink() {
    caretOn_ = true;
    Host* host = GetHost();
    if (!host) return;
    if (blinkTimer_ && blinkHost_) blinkHost_->CancelTimer(blinkTimer_);
    blinkTimer_ = 0;
    if (readOnly) return;  // no caret to blink
    blinkHost_ = host;
    blinkTimer_ = host->StartTimer(530, [this] {
        // A window in the background shows no caret and wakes nothing: the blink stops here and
        // starts again with the first frame after the window is activated (Paint).
        if (!blinkHost_->IsActive()) {
            blinkHost_->CancelTimer(blinkTimer_);
            blinkTimer_ = 0;
            caretOn_ = false;
        } else {
            caretOn_ = !caretOn_;
        }
        Invalidate();
    }, true);
}

void TextBox::OnFocusChanged(bool focused) {
    if (focused) {
        ResetBlink();
    } else {
        if (blinkHost_ && blinkTimer_) blinkHost_->CancelTimer(blinkTimer_);
        blinkTimer_ = 0;
        anchor_ = caret_;
    }
    Invalidate();
}

void TextBox::MoveCaret(size_t pos, bool extend) {
    caret_ = std::min(pos, text_.size());
    if (!extend) anchor_ = caret_;
    // Keep the caret inside the visible text area.
    const Rect tr = TextRect();
    const float cx = XOf(caret_);
    if (cx - scroll_ > tr.w - 2) scroll_ = cx - tr.w + 2;
    if (cx - scroll_ < 0) scroll_ = cx;
    if (scroll_ < 0) scroll_ = 0;
    ResetBlink();
    Invalidate();
}

void TextBox::Changed() {
    if (onChange) onChange(text_);
}

void TextBox::DeleteSelection() {
    if (caret_ == anchor_) return;
    const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
    text_.erase(a, b - a);
    caret_ = anchor_ = a;
}

void TextBox::Insert(std::wstring_view s) {
    if (readOnly) return;
    DeleteSelection();
    // A single line, not a document: a pasted multi-megabyte clipboard would be laid out by
    // DirectWrite on every keystroke and paint. The longest real input (a stream URL) fits.
    constexpr size_t kMaxText = 32768;
    std::wstring clean;
    for (wchar_t ch : s)
        if (ch >= 0x20 && ch != 0x7F && text_.size() + clean.size() < kMaxText) clean.push_back(ch);
    if (text_.size() + clean.size() >= kMaxText && !clean.empty() && IS_HIGH_SURROGATE(clean.back())) clean.pop_back();
    text_.insert(caret_, clean);
    MoveCaret(caret_ + clean.size(), false);
    Changed();
}

bool TextBox::OnChar(wchar_t ch) {
    if (readOnly) return false;
    // WM_CHAR delivers an emoji as two messages: insert the pair at once, so onChange (search)
    // never sees half of it.
    if (IS_HIGH_SURROGATE(ch)) {
        pendingHigh_ = ch;
        return true;
    }
    if (IS_LOW_SURROGATE(ch) && pendingHigh_) {
        const wchar_t pair[2] = {pendingHigh_, ch};
        pendingHigh_ = 0;
        Insert(std::wstring_view(pair, 2));
        return true;
    }
    pendingHigh_ = 0;
    Insert(std::wstring_view(&ch, 1));
    return true;
}

bool TextBox::OnKeyDown(const KeyEvent& e) {
    const bool shift = e.mods.shift, ctrl = e.mods.ctrl;
    auto wordLeft = [&](size_t i) {
        while (i > 0 && !IsWordChar(text_[i - 1])) --i;
        while (i > 0 && IsWordChar(text_[i - 1])) --i;
        return i;
    };
    auto wordRight = [&](size_t i) {
        while (i < text_.size() && !IsWordChar(text_[i])) ++i;
        while (i < text_.size() && IsWordChar(text_[i])) ++i;
        return i;
    };
    // One character back / forward: an emoji is a surrogate pair, never split it.
    auto prev = [&](size_t i) {
        if (i == 0) return i;
        --i;
        if (i > 0 && IS_LOW_SURROGATE(text_[i]) && IS_HIGH_SURROGATE(text_[i - 1])) --i;
        return i;
    };
    auto next = [&](size_t i) {
        if (i >= text_.size()) return text_.size();
        ++i;
        if (i < text_.size() && IS_LOW_SURROGATE(text_[i]) && IS_HIGH_SURROGATE(text_[i - 1])) ++i;
        return i;
    };
    HWND hwnd = GetHost() ? GetHost()->Hwnd() : nullptr;
    switch (e.key) {
        case VK_LEFT:
            if (!shift && caret_ != anchor_) MoveCaret(std::min(caret_, anchor_), false);
            else MoveCaret(ctrl ? wordLeft(caret_) : prev(caret_), shift);
            return true;
        case VK_RIGHT:
            if (!shift && caret_ != anchor_) MoveCaret(std::max(caret_, anchor_), false);
            else MoveCaret(ctrl ? wordRight(caret_) : next(caret_), shift);
            return true;
        case VK_HOME: MoveCaret(0, shift); return true;
        case VK_END: MoveCaret(text_.size(), shift); return true;
        case VK_BACK:
            if (readOnly) return true;
            if (caret_ == anchor_ && caret_ > 0) anchor_ = ctrl ? wordLeft(caret_) : prev(caret_);
            DeleteSelection();
            MoveCaret(caret_, false);
            Changed();
            return true;
        case VK_DELETE:
            if (readOnly) return true;
            if (caret_ == anchor_ && caret_ < text_.size()) anchor_ = ctrl ? wordRight(caret_) : next(caret_);
            DeleteSelection();
            MoveCaret(caret_, false);
            Changed();
            return true;
        case VK_RETURN: {
            // onSubmit may close the dialog and destroy this box: copy the callback
            // and the text, and touch no member after the call.
            if (!onSubmit) return false;
            auto submit = onSubmit;
            submit(std::wstring(text_));
            return true;
        }
        case VK_ESCAPE:
            if (onEscape) {
                onEscape();
                return true;
            }
            return false;
        default: break;
    }
    if (ctrl) {
        const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        switch (e.key) {
            case 'A': SelectAll(); return true;
            case 'C':
                if (b > a && !password) SetClipboardText(hwnd, text_.substr(a, b - a));
                return true;
            case 'X':
                if (b > a && !password && !readOnly) {
                    SetClipboardText(hwnd, text_.substr(a, b - a));
                    DeleteSelection();
                    MoveCaret(caret_, false);
                    Changed();
                }
                return true;
            case 'V': Insert(ClipboardText(hwnd)); return true;
            default: break;
        }
    }
    // Printable keys arrive as WM_CHAR; claim them so window shortcuts don't fire.
    return !ctrl && !e.mods.alt && e.key >= 0x20 && e.key < VK_F1 && e.key != VK_TAB && e.key != VK_UP &&
           e.key != VK_DOWN && e.key != VK_PRIOR && e.key != VK_NEXT && e.key != VK_APPS;
}

bool TextBox::OnMouseDown(const MouseEvent& e) {
    selecting_ = true;
    MoveCaret(IndexAt(e.x), e.mods.shift);
    return true;
}

bool TextBox::OnMouseMove(const MouseEvent& e) {
    if (!selecting_) return false;
    MoveCaret(IndexAt(e.x), true);
    return true;
}

bool TextBox::OnMouseUp(const MouseEvent&) {
    selecting_ = false;
    return true;
}

bool TextBox::OnDoubleClick(const MouseEvent& e) {
    size_t i = IndexAt(e.x), a = i, b = i;
    while (a > 0 && IsWordChar(text_[a - 1])) --a;
    while (b < text_.size() && IsWordChar(text_[b])) ++b;
    anchor_ = a;
    MoveCaret(b, true);
    return true;
}

void TextBox::Paint(Painter& p) {
    const Palette& c = P();
    const bool live = IsEnabled();
    const Color bg = background.a > 0 ? background : raised ? c.bgRaised : c.bgDeep;
    // Disabled: the flat fill of the other disabled controls, no hover or focus outline.
    p.Fill(rect, live ? bg : DisabledFill(), 4);
    if (live && IsFocused())
        p.Stroke(rect, c.lineStrong, 4);
    else if (live)
        p.Stroke(rect, IsHot() ? c.textTertiary : c.lineControl, 4);
    PaintFocusRing(p, rect, 4);  // reached with Tab: the ring of every other control, not just the caret
    if (!leadingIcon.empty())
        p.Icon(leadingIcon, {rect.x + 10, rect.Cy() - 10, 20, 20}, live ? c.textTertiary : c.textDisabled);

    const Rect tr = TextRect();
    p.PushClip(tr);
    const std::wstring shown = Shown();
    if (shown.empty()) {
        p.Text(placeholder, tr, TextStyle::Body, live ? c.textTertiary : c.textDisabled);
    } else {
        if (IsFocused() && caret_ != anchor_) {
            const float x1 = XOf(std::min(caret_, anchor_)), x2 = XOf(std::max(caret_, anchor_));
            p.Fill({tr.x + x1 - scroll_, rect.Cy() - 10, x2 - x1, 20}, c.accentDim, 2);
        }
        p.Text(shown, {tr.x - scroll_, tr.y, 100000.0f, tr.h}, TextStyle::Body, IsEnabled() ? c.textPrimary : c.textDisabled,
               HAlign::Leading, VAlign::Center, 0);
    }
    if (live && IsFocused() && !readOnly && !blinkTimer_ && GetHost()->IsActive()) ResetBlink();
    if (live && IsFocused() && caretOn_ && !readOnly) p.Fill(CaretRect(), c.textPrimary);
    p.PopClip();
}

// --- AccentPicker -----------------------------------------------------------------------

AccentPicker::AccentPicker(int selected) : selected_(selected) {
    interactive = true;
    focusable = true;
}

Size AccentPicker::MeasureContent(float, float) {
    return {8 + kAccentCount * 36.0f, 36};
}

bool AccentPicker::OnMouseDown(const MouseEvent& e) {
    const int i = IndexAt(e.x);
    if (i >= 0 && i != selected_) {
        selected_ = i;
        if (onChange) onChange(i);
        Invalidate();
    }
    return true;
}

int AccentPicker::IndexAt(float x) const {
    const int i = static_cast<int>(std::floor((x - rect.x - 4) / 36));
    return i >= 0 && i < kAccentCount ? i : -1;
}

bool AccentPicker::OnMouseMove(const MouseEvent& e) {
    const int i = IndexAt(e.x);
    if (i != hover_) {
        hover_ = i;
        Invalidate();
    }
    return true;
}

void AccentPicker::Paint(Painter& p) {
    p.Fill(rect, P().bgDeep, kRadiusControl);
    for (int i = 0; i < kAccentCount; ++i) {
        const Rect cell{rect.x + 4 + i * 36.0f, rect.y + 4, 32, rect.h - 8};
        if (i == selected_) p.Fill(cell, P().bgSelected, 4);
        p.Fill(cell.Center(18, 18), Theme::Get().AccentSwatch(i), 3);
        if (i == selected_)
            p.Stroke(cell, P().accentMid, 4);
        else if (i == hover_ && IsHot())
            p.Stroke(cell, P().lineControl, 4);
    }
    PaintFocusRing(p, rect, kRadiusControl);
}

// --- ScrollView -----------------------------------------------------------------------------

ScrollView::ScrollView() {
    interactive = true;
    clipChildren = true;
    width = Length::Grow();
    height = Length::Grow();
}

ScrollView::~ScrollView() {
    // A page can go away within 900 ms of a scroll: the hide timer must not fire on it.
    if (!hideTimer_ || !timerHost_) return;
    const auto& hosts = Host::All();
    if (std::find(hosts.begin(), hosts.end(), timerHost_) != hosts.end()) timerHost_->CancelTimer(hideTimer_);
}

Size ScrollView::MeasureContent(float availW, float availH) {
    Widget* c = Content();
    if (!c) return {0, 0};
    Size s = c->Measure(availW, kInf);
    return {s.w, std::min(s.h, availH)};
}

void ScrollView::OnLayout() {
    Widget* c = Content();
    if (!c) return;
    contentH_ = c->Measure(rect.w, kInf).h;
    if (const Host* host = GetHost()) {
        const Rect& card = host->floatingCard;
        if (card.w > 0 && card.x < rect.Right() && card.Right() > rect.x && card.y < rect.Bottom())
            contentH_ += rect.Bottom() - card.y + 12;
    }
    // Not scrollable (an overflow of up to kNoScroll): back to the top, not left 1-4 DIP down.
    const float maxOff = Scrollable() ? MaxOffset() : 0.0f;
    if (offset_.Target() > maxOff) offset_.Set(maxOff);
    c->Arrange({rect.x, rect.y - std::round(offset_.Get()), rect.w, std::max(contentH_, rect.h)});
}

void ScrollView::ScrollTo(float y, bool animate, bool showBar) {
    y = std::clamp(y, 0.0f, MaxOffset());
    if (animate)
        offset_.To(y, 180, Ease::Out);
    else
        offset_.Set(y);
    if (!showBar) {
        if (onScroll) onScroll(y);
        InvalidateLayout();
        return;
    }
    barVisible_.To(1, 80);
    Host* host = GetHost();
    if (host) {
        if (hideTimer_) host->CancelTimer(hideTimer_);
        timerHost_ = host;
        hideTimer_ = host->StartTimer(900, [this] {
            hideTimer_ = 0;
            if (!dragging_ && !overBar_) FadeBarStep();
        });
    }
    if (onScroll) onScroll(y);
    InvalidateLayout();
}

// The thumb fades out in four 50-ms steps: four frames instead of a 250-ms animation that
// rendered every vsync (36 frames at 144 Hz) for a 4-DIP bar after each wheel notch.
void ScrollView::FadeBarStep() {
    const float v = std::max(0.0f, barVisible_.Get() - 0.25f);
    barVisible_.Set(v);
    Invalidate();
    if (v <= 0 || !timerHost_) return;
    hideTimer_ = timerHost_->StartTimer(50, [this] {
        hideTimer_ = 0;
        if (!dragging_ && !overBar_) FadeBarStep();
    });
}

void ScrollView::ScrollIntoView(const Rect& r) {
    if (!Scrollable()) return;
    const float top = offset_.Target();
    if (r.y < rect.y)
        ScrollTo(top - (rect.y - r.y) - 8);
    else if (r.Bottom() > rect.Bottom())
        ScrollTo(top + (r.Bottom() - rect.Bottom()) + 8);
}

bool ScrollView::OnWheel(const WheelEvent& e) {
    if (e.horizontal || !Scrollable()) return false;
    ScrollTo(offset_.Target() - e.delta * 96.0f);
    return true;
}

Rect ScrollView::ThumbRect() const {
    if (contentH_ <= rect.h || rect.h <= 0) return {};
    const float h = std::max(32.0f, rect.h * rect.h / contentH_);
    const float t = offset_.Get() / MaxOffset();
    const float w = overBar_ || dragging_ ? 8.0f : 4.0f;
    return {rect.Right() - w - 3, rect.y + 2 + (rect.h - 4 - h) * t, w, h};
}

Widget* ScrollView::HitTest(float x, float y) {
    if (!visible || !rect.Contains(x, y)) return nullptr;
    // The scrollbar strip takes the pointer only where the thumb is, or while the bar is shown
    // (paging clicks): otherwise the right edge of every row stayed dead (no click, no menu).
    if (Scrollable() && x >= rect.Right() - 14) {
        const Rect thumb = ThumbRect();
        if (barVisible_.Get() > 0.5f || dragging_ || (y >= thumb.y - 6 && y < thumb.Bottom() + 6)) return this;
    }
    for (auto it = Children().rbegin(); it != Children().rend(); ++it)
        if (Widget* hit = (*it)->HitTest(x, y)) return hit;
    return this;
}

bool ScrollView::OnMouseDown(const MouseEvent& e) {
    if (!Scrollable() || e.x < rect.Right() - 14) return false;
    const Rect thumb = ThumbRect();
    if (!thumb.Contains(e.x, e.y) && !(e.y >= thumb.y && e.y < thumb.Bottom())) {
        // Click on the track pages toward the click.
        ScrollTo(offset_.Target() + (e.y < thumb.y ? -rect.h : rect.h) * 0.9f);
        return true;
    }
    dragging_ = true;
    dragStartY_ = e.y;
    dragStartOffset_ = offset_.Get();
    return true;
}

bool ScrollView::OnMouseMove(const MouseEvent& e) {
    const bool over = Scrollable() && e.x >= rect.Right() - 14;
    if (over != overBar_) {
        overBar_ = over;
        if (over) barVisible_.To(1, 80);
        Invalidate();
    }
    if (!dragging_) return false;
    const Rect thumb = ThumbRect();
    const float range = rect.h - 4 - thumb.h;
    if (range > 0) ScrollTo(dragStartOffset_ + (e.y - dragStartY_) / range * MaxOffset(), false);
    return true;
}

bool ScrollView::OnMouseUp(const MouseEvent&) {
    // Only a thumb drag ends here: a click on a blank part neither flashes the bar nor cuts a
    // smooth scroll (or a track page) short.
    if (!dragging_) return true;
    dragging_ = false;
    ScrollTo(offset_.Target(), false);
    return true;
}

void ScrollView::OnMouseLeave() {
    overBar_ = false;
    if (!dragging_) barVisible_.To(0, 250);
    Invalidate();
}

void ScrollView::PaintOver(Painter& p) {
    if (!Scrollable()) return;
    const float max = MaxOffset();
    const float off = offset_.Get();
    Color bg;
    for (const Widget* w = Parent(); w; w = w->Parent())
        if (w->Backdrop(bg)) {
            // Stacked bands, no gradient brush: 7 fills of 4 DIP per edge (as the menu's fade),
            // densest at the cut edge.
            constexpr int kSteps = 7;
            constexpr float kCueH = 28, kBand = kCueH / kSteps;
            for (int i = 0; i < kSteps; ++i) {
                const float t = static_cast<float>(i + 1) / kSteps;
                const Color band = bg.Fade(0.92f * t * t);
                if (off < max - 1) p.Fill({rect.x, rect.Bottom() - kCueH + kBand * static_cast<float>(i), rect.w, kBand}, band);
                if (off > 1) p.Fill({rect.x, rect.y + kBand * static_cast<float>(kSteps - 1 - i), rect.w, kBand}, band);
            }
            break;
        }
    // The resting thumb: half as wide as the full one, which covers it while scrolling.
    const Rect thumb = ThumbRect();
    p.Fill({rect.Right() - 5, thumb.y, 2, thumb.h}, P().lineStrong, 1);
    const float k = std::max(barVisible_.Get(), overBar_ ? 1.0f : 0.0f);
    if (k <= 0.01f) return;
    p.Fill(thumb, (overBar_ || dragging_ ? P().textTertiary : P().lineStrong).Fade(k), thumb.w / 2);
}

// --- Panel ------------------------------------------------------------------------------

void Panel::Paint(Painter& p) {
    if (shadow) p.Shadow(rect, radius);
    if (fill) p.Fill(rect, fill(), radius);
    if (border) p.Stroke(rect, border(), radius);
}

bool Panel::Backdrop(Color& out) const {
    if (!fill) return false;
    out = fill();
    return true;
}

// --- Menu -------------------------------------------------------------------------------

namespace {

// Long menus scroll, and typing filters the items (as in uosc): the query shows in a row on
// top, Backspace edits it, Esc clears it before closing the menu.
class MenuPopup : public Widget {
public:
    MenuPopup(std::vector<MenuItem> items, float minWidth, MenuPopup* parentMenu)
        : items_(std::move(items)), minWidth_(minWidth), parentMenu_(parentMenu) {
        interactive = true;
        focusable = true;
        hasIcons_ = std::any_of(items_.begin(), items_.end(), [](const MenuItem& m) { return !m.icon.empty(); });
        hasChecks_ = std::any_of(items_.begin(), items_.end(), [](const MenuItem& m) { return m.checked; });
        Flatten(items_, L"");
        Refilter();
    }

    ~MenuPopup() override {
        // Clicking an item within 180 ms of hovering a submenu row closes the menu first.
        if (!submenuTimer_ || !timerHost_) return;
        const auto& hosts = Host::All();
        if (std::find(hosts.begin(), hosts.end(), timerHost_) != hosts.end()) timerHost_->CancelTimer(submenuTimer_);
    }

    MenuPopup* Root() {
        MenuPopup* m = this;
        while (m->parentMenu_) m = m->parentMenu_;
        return m;
    }

    // The width is at least that of the full list (the menu only grows sideways, for submenu
    // matches with their path); the height follows the matches.
    Size MeasureContent(float, float availH) override {
        float w = minWidth_;
        auto fit = [&](const MenuItem& m) {
            if (m.separator) return;
            float iw = (hasIcons_ ? 52.0f : 16.0f) + gfx::Text::Get().Measure(m.label, TextStyle::Body).width + 16;
            if (!m.shortcut.empty()) iw += 32 + gfx::Text::Get().Measure(m.shortcut, TextStyle::Body).width;
            if (!m.submenu.empty() || hasChecks_ || m.checked) iw += 32;
            w = std::max(w, iw);
        };
        for (const MenuItem& m : items_) fit(m);
        if (!filter_.empty())
            for (int i : visible_) fit(At(i));
        float h = kMenuPad * 2 + HeaderH() + (visible_.empty() ? MenuItemH() : ContentH());
        return {std::min(w, 560.0f), std::min(h, availH)};
    }

    int IndexAt(float y) const {
        if (y < rect.y + kMenuPad + HeaderH() || y >= rect.Bottom() - kMenuPad) return -1;
        float cy = Top();
        for (int i : visible_) {
            const float h = RowH(i);
            if (y >= cy && y < cy + h) return At(i).separator ? -1 : i;
            cy += h;
        }
        return -1;
    }

    Rect ItemRect(int index) const {
        float cy = Top();
        for (int i : visible_) {
            if (i == index) break;
            cy += RowH(i);
        }
        return {rect.x + 4, cy, rect.w - 8, MenuItemH()};
    }

    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Shadow(rect, kRadiusPopup);
        p.Fill(rect, c.bgMenu, kRadiusPopup);
        p.Stroke(rect, c.line, kRadiusPopup);
        if (!filter_.empty()) {
            const Rect row{rect.x + 4, rect.y + kMenuPad, rect.w - 8, MenuItemH()};
            p.Icon("search", {row.x + 12, row.Cy() - 10, 20, 20}, c.textTertiary);
            p.Text(filter_, {row.x + 48, row.y, row.w - 60, row.h}, TextStyle::Body, c.textPrimary, HAlign::Leading,
                   VAlign::Center, gfx::kTextEllipsis);
            p.Fill({rect.x + 12, row.Bottom() + kMenuSepH / 2, rect.w - 24, 1}, c.lineStrong);
            if (visible_.empty())
                p.Text(g_menuNoMatch, {row.x + 12, row.Bottom() + kMenuSepH, row.w - 24, MenuItemH()}, TextStyle::Body,
                       c.textTertiary, HAlign::Leading, VAlign::Center, gfx::kTextEllipsis);
        }
        p.PushClip({rect.x, rect.y + kMenuPad + HeaderH(), rect.w, rect.h - kMenuPad * 2 - HeaderH()});
        float cy = Top();
        for (size_t k = 0; k < visible_.size(); ++k) {
            const int i = visible_[k];
            const MenuItem& m = At(i);
            if (m.separator) {
                p.Fill({rect.x + 12, cy + kMenuSepH / 2, rect.w - 24, 1}, c.lineStrong);
                cy += kMenuSepH;
                continue;
            }
            if (cy > rect.Bottom() || cy + MenuItemH() < rect.y) {
                cy += MenuItemH();
                continue;
            }
            const Rect row{rect.x + 4, cy, rect.w - 8, MenuItemH()};
            const bool hot = i == highlight_ && m.enabled;
            if (hot) p.Fill(row, c.hover, 4);
            // A note is information, so it keeps text contrast; a disabled command may fade.
            const Color fg = m.note ? c.textSecondary : !m.enabled ? c.textDisabled : hot ? c.textPrimary : c.textSecondary;
            float x = row.x + 12;
            if (hasIcons_) {
                const float s = SmallGlyph(m.icon) ? 28.0f : 20.0f;
                if (!m.icon.empty()) p.Icon(m.icon, {x + 10 - s / 2, row.Cy() - s / 2, s, s}, fg, m.enabled ? c.accentMid : fg);
                x += 36;
            }
            float right = row.Right() - 12;
            if (!m.submenu.empty()) {
                p.Icon("chevron_right", {right - 20, row.Cy() - 10, 20, 20}, fg);
                right -= 28;
            } else if (m.checked) {
                // Radio and check items both show a tick (a lone dot reads as decoration).
                p.Icon("check_selected", {right - 20, row.Cy() - 10, 20, 20}, c.accent);
                right -= 28;
            } else if (hasChecks_) {
                right -= 28;
            }
            if (!m.shortcut.empty()) {
                const float sw = gfx::Text::Get().Measure(m.shortcut, TextStyle::Body).width;
                p.Text(m.shortcut, {right - sw, row.y, sw + 1, row.h}, TextStyle::Body, m.enabled ? c.textTertiary : fg,
                       HAlign::Leading, VAlign::Center, 0);
                right -= sw + 16;
            }
            p.Text(m.label, {x, row.y, right - x, row.h}, TextStyle::Body, m.checked && m.enabled ? c.textPrimary : fg);
            // The typed text, underlined in the label in the accent color (visible on the hover
            // fill too); positions are measured once per keystroke, in Refilter.
            if (k < marks_.size() && marks_[k].second > 0 && marks_[k].first < right - x) {
                const float mx = x + marks_[k].first;
                p.Fill({mx, row.Cy() + 9, std::min(marks_[k].second, right - mx), 2}, m.enabled ? c.accent : fg, 1);
            }
            cy += MenuItemH();
        }
        // A menu taller than a small window scrolls: fade the cut edge so the last row reads as
        // "more below" (or above), not as the end of the menu.
        if (MaxScroll() > 0) {
            const float top = rect.y + kMenuPad + HeaderH(), bottom = rect.Bottom() - kMenuPad;
            constexpr int kSteps = 6;
            constexpr float kStep = 4;
            for (int s = 0; s < kSteps; ++s) {
                const Color fade = c.bgMenu.Fade(static_cast<float>(kSteps - s) / kSteps);
                if (scroll_ < MaxScroll() - 0.5f) p.Fill({rect.x + 1, bottom - (s + 1) * kStep, rect.w - 2, kStep}, fade);
                if (scroll_ > 0.5f) p.Fill({rect.x + 1, top + s * kStep, rect.w - 2, kStep}, fade);
            }
        }
        p.PopClip();
        if (MaxScroll() > 0) {  // thin position marker, like ScrollView's
            const float trackH = rect.h - kMenuPad * 2 - HeaderH();
            const float thumbH = std::max(24.0f, trackH * trackH / (trackH + MaxScroll()));
            const float y = rect.y + kMenuPad + HeaderH() + (trackH - thumbH) * scroll_ / MaxScroll();
            p.Fill({rect.Right() - 5, y, 3, thumbH}, c.lineStrong, 1.5f);
        }
    }

    bool OnMouseMove(const MouseEvent& e) override {
        SetHighlight(IndexAt(e.y), false);
        return true;
    }

    bool OnWheel(const WheelEvent& e) override {
        if (MaxScroll() <= 0) return true;
        scroll_ = std::clamp(scroll_ - e.delta * MenuItemH() * 2, 0.0f, MaxScroll());
        SetHighlight(IndexAt(GetHost()->MousePos().y), false);
        Invalidate();
        return true;
    }

    void OnMouseLeave() override {
        if (!child_) SetHighlight(-1, false);
    }

    void OnClick() override {
        const int i = IndexAt(GetHost()->MousePos().y);
        Activate(i);
    }

    bool OnKeyDown(const KeyEvent& e) override {
        auto step = [&](int delta) {
            const int n = static_cast<int>(visible_.size());
            if (n == 0) return;
            int pos = static_cast<int>(std::find(visible_.begin(), visible_.end(), highlight_) - visible_.begin());
            if (pos >= n) pos = delta > 0 ? -1 : n;
            for (int k = 0; k < n; ++k) {
                pos = (pos + delta + n) % n;
                const MenuItem& m = At(visible_[static_cast<size_t>(pos)]);
                if (!m.separator && m.enabled) break;
            }
            SetHighlight(visible_[static_cast<size_t>(pos)], true);
        };
        switch (e.key) {
            case VK_DOWN: step(1); return true;
            case VK_UP: step(-1); return true;
            case VK_HOME: highlight_ = -1; step(1); return true;
            case VK_END: highlight_ = -1; step(-1); return true;
            case VK_RETURN: Activate(highlight_); return true;
            case VK_SPACE:
                if (!filter_.empty()) return true;  // typed into the query (WM_CHAR)
                Activate(highlight_);
                return true;
            case VK_BACK:
                if (!filter_.empty()) {
                    filter_.pop_back();
                    Refilter();
                }
                return true;
            case VK_ESCAPE:
                if (filter_.empty()) return false;
                filter_.clear();
                Refilter();
                return true;
            case VK_RIGHT:
                if (highlight_ >= 0 && !At(highlight_).submenu.empty()) OpenSubmenu(highlight_, true);
                return true;
            case VK_LEFT:
                if (parentMenu_) {
                    GetHost()->ClosePopup(this);
                    return true;
                }
                return false;
            default:
                // Letters, digits and punctuation go to the query (OnChar), not to the
                // window's single-key shortcuts.
                return !e.mods.ctrl && !e.mods.alt &&
                       ((e.key >= '0' && e.key <= 'Z') || (e.key >= VK_OEM_1 && e.key <= VK_OEM_8) ||
                        (e.key >= VK_NUMPAD0 && e.key <= VK_DIVIDE));
        }
    }

    void Describe(A11y& a) override {
        a.role = Role::Menu;
        a.items = static_cast<int>(visible_.size());
        a.item = [this](int k, A11y::Item& it) {
            const int i = visible_[static_cast<size_t>(k)];
            const MenuItem& m = At(i);
            if (m.separator) {
                it.role = Role::Skip;
                return;
            }
            it.role = m.note ? Role::Text : Role::MenuItem;
            it.name = m.label;
            it.keys = m.shortcut;
            it.rect = ItemRect(i);
            it.enabled = m.enabled || m.note;
            if (!m.submenu.empty()) it.expanded = child_ && i == highlight_ ? 1 : 0;
            if (hasChecks_ || m.checked) it.toggle = m.checked ? 1 : 0;
        };
        a.invokeItem = [this](int k) { Activate(visible_[static_cast<size_t>(k)]); };
        const auto at = std::find(visible_.begin(), visible_.end(), highlight_);
        if (highlight_ >= 0 && at != visible_.end()) a.focusedItem = static_cast<int>(at - visible_.begin());
    }

    bool OnChar(wchar_t ch) override {
        if (ch < 0x20 || filter_.size() >= 40) return false;
        if (ch == L' ' && filter_.empty()) return false;
        filter_ += ch;
        Refilter();
        return true;
    }

private:
    const MenuItem& At(int i) const {
        const size_t n = items_.size();
        return static_cast<size_t>(i) < n ? items_[static_cast<size_t>(i)] : flat_[static_cast<size_t>(i) - n];
    }

    // Leaf commands of the submenus, labelled with their path, for the filter.
    void Flatten(const std::vector<MenuItem>& items, const std::wstring& prefix) {
        for (const MenuItem& m : items) {
            if (m.separator) continue;
            if (!m.submenu.empty()) {
                Flatten(m.submenu, prefix + m.label + L" \u203A ");
            } else if (!prefix.empty()) {
                MenuItem copy = m;
                copy.label = prefix + m.label;
                copy.icon.clear();
                flat_.push_back(std::move(copy));
            }
        }
    }

    float HeaderH() const { return filter_.empty() ? 0.0f : MenuItemH() + kMenuSepH; }
    float RowH(int i) const { return At(i).separator ? kMenuSepH : MenuItemH(); }
    float Top() const { return rect.y + kMenuPad + HeaderH() - scroll_; }
    float ContentH() const {
        float h = 0;
        for (int i : visible_) h += RowH(i);
        return h;
    }
    float MaxScroll() const { return std::max(0.0f, ContentH() - (rect.h - kMenuPad * 2 - HeaderH())); }

    // Where `query` starts in `label` (-1 = not there); `found` gets the matched length.
    static int Find(const std::wstring& label, const std::wstring& query, int* found = nullptr) {
        return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,
                               FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC, label.c_str(),
                               static_cast<int>(label.size()), query.c_str(), static_cast<int>(query.size()),
                               found, nullptr, nullptr, 0);
    }
    static bool Matches(const std::wstring& label, const std::wstring& query) { return Find(label, query) >= 0; }

    void Refilter() {
        // The rows move: a submenu opened from one of them would stay behind, detached from it.
        if (Host* host = GetHost()) {
            if (submenuTimer_) host->CancelTimer(submenuTimer_);
            if (child_) host->ClosePopup(child_);
        }
        submenuTimer_ = 0;
        child_ = nullptr;
        highlight_ = -1;
        visible_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            const MenuItem& m = items_[i];
            if (filter_.empty() || (!m.separator && Matches(m.label, filter_))) visible_.push_back(static_cast<int>(i));
        }
        // Typing also finds commands inside submenus ("Playback › Speed › 1.5×").
        if (!filter_.empty())
            for (size_t i = 0; i < flat_.size(); ++i)
                if (Matches(flat_[i].label, filter_)) visible_.push_back(static_cast<int>(items_.size() + i));
        // Offset and width of the match in each label, for the mark painted behind it.
        marks_.assign(visible_.size(), {0.0f, 0.0f});
        if (!filter_.empty())
            for (size_t k = 0; k < visible_.size(); ++k) {
                const std::wstring& label = At(visible_[k]).label;
                int len = 0;
                const int start = Find(label, filter_, &len);
                if (start < 0 || len <= 0) continue;
                auto& t = gfx::Text::Get();
                marks_[k] = {std::round(t.Measure(std::wstring_view(label).substr(0, static_cast<size_t>(start)), TextStyle::Body).width),
                             std::ceil(t.Measure(std::wstring_view(label).substr(static_cast<size_t>(start), static_cast<size_t>(len)),
                                                 TextStyle::Body).width)};
            }
        scroll_ = 0;
        InvalidateLayout();  // the height follows the matches
        if (!filter_.empty()) {
            int first = -1;
            for (int i : visible_)
                if (At(i).enabled) {
                    first = i;
                    break;
                }
            SetHighlight(first, true);
        }
        Invalidate();
    }

    void ScrollIntoView(int i) {
        if (i < 0 || rect.h <= 0) return;
        const Rect r = ItemRect(i);
        const float top = rect.y + kMenuPad + HeaderH(), bottom = rect.Bottom() - kMenuPad;
        if (r.y < top)
            scroll_ -= top - r.y;
        else if (r.Bottom() > bottom)
            scroll_ += r.Bottom() - bottom;
        scroll_ = std::clamp(scroll_, 0.0f, MaxScroll());
    }

    void SetHighlight(int i, bool fromKeyboard) {
        if (fromKeyboard) ScrollIntoView(i);
        if (i == highlight_) return;
        highlight_ = i;
        Invalidate();
        Host* host = GetHost();
        if (IsFocused()) a11y::FocusChanged(host);
        if (child_ && host) {
            host->ClosePopup(child_);
            child_ = nullptr;
        }
        if (submenuTimer_ && host) host->CancelTimer(submenuTimer_);
        submenuTimer_ = 0;
        if (i >= 0 && !At(i).submenu.empty() && At(i).enabled) {
            timerHost_ = host;
            submenuTimer_ = host ? host->StartTimer(fromKeyboard ? 0 : 180, [this, i, fromKeyboard] {
                submenuTimer_ = 0;
                if (highlight_ == i) OpenSubmenu(i, false, !fromKeyboard);
            }) : 0;
        }
    }

    void OpenSubmenu(int i, bool focus, bool animate = true) {
        Host* host = GetHost();
        if (!host || child_) return;
        auto sub = std::make_unique<MenuPopup>(At(i).submenu, 180.0f, this);
        MenuPopup* raw = sub.get();
        PopupOptions o;
        o.anchor = ItemRect(i).Offset(0, -kMenuPad);
        o.place = Place::Right;
        o.offset = 2;
        o.focusContent = focus;
        o.animate = animate;
        o.onDismiss = [this, raw] {
            if (child_ == raw) child_ = nullptr;
        };
        child_ = static_cast<MenuPopup*>(host->ShowPopup(std::move(sub), o));
        if (focus) raw->SetHighlight(raw->visible_.empty() ? -1 : raw->visible_.front(), true);
    }

    void Activate(int i) {
        // Typed matches inside submenus come after items_ (see Refilter).
        if (i < 0 || i >= static_cast<int>(items_.size() + flat_.size())) return;
        const MenuItem& m = At(i);
        if (m.separator || !m.enabled) return;
        if (!m.submenu.empty()) {
            OpenSubmenu(i, true);
            return;
        }
        Host* host = GetHost();
        auto action = m.action;
        host->ClosePopup(Root());  // also closes submenus stacked above it
        if (action) host->Post(action);
    }

    std::vector<MenuItem> items_;
    std::vector<MenuItem> flat_;  // submenu commands with their path (see Flatten)
    std::vector<int> visible_;    // matching indices: items_, then flat_ (items_.size() + j)
    std::vector<std::pair<float, float>> marks_;  // per visible_ row: x and width of the typed match
    std::wstring filter_;
    float minWidth_;
    MenuPopup* parentMenu_;
    MenuPopup* child_ = nullptr;
    int highlight_ = -1;
    float scroll_ = 0;
    bool hasIcons_ = false, hasChecks_ = false;
    UINT_PTR submenuTimer_ = 0;
    Host* timerHost_ = nullptr;
};

}  // namespace

Widget* ShowMenu(Host* host, const Rect& anchor, std::vector<MenuItem> items, Place place, float minWidth) {
    if (!host || items.empty()) return nullptr;
    PopupOptions o;
    o.anchor = anchor;
    o.place = place;
    return host->ShowPopup(std::make_unique<MenuPopup>(std::move(items), minWidth, nullptr), o);
}

Widget* ShowContextMenu(Host* host, Point at, std::vector<MenuItem> items) {
    if (!host || items.empty()) return nullptr;
    PopupOptions o;
    o.anchor = {at.x, at.y, 0, 0};
    o.place = Place::Below;
    o.offset = 2;
    return host->ShowPopup(std::make_unique<MenuPopup>(std::move(items), 220.0f, nullptr), o);
}

// --- Dialog --------------------------------------------------------------------------------

namespace {

class DialogHeader : public Widget {
public:
    DialogHeader(Dialog* dialog, std::wstring title) : title_(std::move(title)) {
        height = Length::Px(52);
        width = Length::Grow();
        dir = Dir::Row;
        align = Align::Center;
        pad = {20, 0, 10, 0};
        auto* label = Add<Label>(title_, TextStyle::Body, Tone::Secondary);
        label->width = Length::Grow();
        auto* close = Add<IconButton>("close", L"", 32, 20);
        close->onClick = [dialog] { dialog->Close(); };
    }

private:
    std::wstring title_;
};

}  // namespace

Dialog::Dialog(std::wstring title, int w) {
    width = Length::Px(static_cast<float>(w));
    dir = Dir::Column;
    interactive = true;  // swallow clicks on the panel background
    Add<DialogHeader>(this, std::move(title));
    body_ = Add<Widget>();
    body_->dir = Dir::Column;
    body_->width = Length::Grow();
    body_->pad = Insets::All(20);
    body_->gap = 12;
    footer_ = Add<Widget>();
    footer_->dir = Dir::Row;
    footer_->width = Length::Grow();
    footer_->pad = {20, 4, 20, 20};
    footer_->gap = 8;
    footer_->align = Align::Center;
}

void Dialog::Close() {
    if (Host* host = GetHost()) host->ClosePopup(this);
}

bool Dialog::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

void Dialog::Paint(Painter& p) {
    const Palette& c = P();
    // The look of the app's dialogs (dlg::Frame).
    p.Shadow(rect, kRadiusWindow, 15, 8);
    p.Fill(rect, c.bgPanel, kRadiusWindow);
    // Header band, as in the design (darker strip with the title).
    p.PushRoundClip(rect, kRadiusWindow);
    p.Fill({rect.x, rect.y, rect.w, 52}, c.bgDeep);
    p.PopClip();
    p.Stroke(rect, c.lineSubtle, kRadiusWindow);
}

Dialog* ShowDialog(Host* host, std::unique_ptr<Dialog> dialog) {
    if (!host) return nullptr;
    Dialog* raw = dialog.get();
    PopupOptions o;
    o.place = Place::Center;
    o.modal = true;
    o.scrim = true;
    o.dismissOnOutside = false;
    o.dismissOnEscape = true;
    host->ShowPopup(std::move(dialog), o);
    return raw;
}

// --- Toast ---------------------------------------------------------------------------------

namespace {

// The part of the window a toast centres in (Host::toastBand).
Rect ToastBandOf(Host* host) {
    return host->toastBand ? host->toastBand() : Rect{0, 0, host->ClientSize().w, 0};
}

class ToastBubble : public Widget {
public:
    ToastBubble(std::wstring text, std::string icon, std::wstring actionLabel, std::function<void()> action)
        : text_(std::move(text)), icon_(std::move(icon)) {
        dir = Dir::None;
        if (action) {
            // The message is often a path cut with an ellipsis; this toast takes the mouse, so
            // hovering it shows the whole text.
            interactive = true;
            tooltip = text_;
            action_ = Add<Button>(std::move(actionLabel), ButtonKind::Ghost);
            action_->tooltip = text_;
            action_->onClick = std::move(action);
        }
    }
    Size MeasureContent(float availW, float) override {
        // Capped here, not only by the caller: placement measures again with the window width,
        // and a long title (CJK file names) then came out wider than the box it was centred for.
        // 16 DIP inside the band on each side, so it keeps clear of the panel beside it.
        if (Host* h = GetHost()) availW = std::min(availW, std::max(240.0f, ToastBandOf(h).w - 32));
        float w = gfx::Text::Get().Measure(text_, TextStyle::Body).width + 32 + (icon_.empty() ? 0 : 28);
        if (action_) w += action_->Measure(availW, 32).w + 4;
        w = std::min({w, availW, kToastMaxW});
        // A message wider than the toast can be (a narrow window beside a panel) wraps to a second
        // line instead of losing its end; a longer one is cut there.
        const float row = FitRow(40, TextStyle::Body, 20);
        const float textW = std::max(0.0f, w - 32 - (icon_.empty() ? 0.0f : 28.0f) -
                                                (action_ ? action_->Measure(availW, 32).w + 4 : 0.0f));
        const gfx::TextMetrics m = gfx::Text::Get().Measure(text_, TextStyle::Body, textW, gfx::kTextWrap);
        wrapped_ = m.lines > 1;
        return {w, wrapped_ ? std::ceil(row + m.height / m.lines) : row};  // two lines at most
    }
    void OnLayout() override {
        if (!action_) return;
        const float bw = action_->Measure(rect.w, 32).w;
        action_->Arrange({rect.Right() - 4 - bw, rect.y + 4, bw, 32});
    }
    void Paint(Painter& p) override {
        p.Shadow(rect, kRadiusPopup, 14, 6);
        p.Fill(rect, P().bgMenu, kRadiusPopup);
        p.Stroke(rect, P().line, kRadiusPopup);
        float x = rect.x + 16;
        if (!icon_.empty()) {
            p.Icon(icon_, {x, rect.Cy() - 10, 20, 20}, P().textPrimary);
            x += 28;
        }
        const float right = action_ ? action_->rect.x - 4 : rect.Right() - 16;
        p.Text(text_, {x, rect.y, std::max(0.0f, right - x), rect.h}, TextStyle::Body, P().textPrimary, HAlign::Leading,
               VAlign::Center, wrapped_ ? gfx::kTextWrap | gfx::kTextEllipsis : gfx::kTextEllipsis);
    }

private:
    static constexpr float kToastMaxW = 600;
    bool wrapped_ = false;
    std::wstring text_;
    std::string icon_;
    Button* action_ = nullptr;
};

struct ToastSlot {
    Widget* widget = nullptr;
    uint64_t id = 0;  // the timer closes only its own toast, never one reusing the same address
};

std::map<Host*, ToastSlot>& Toasts() {
    static std::map<Host*, ToastSlot> toasts;
    return toasts;
}

void CloseToastAfter(Host* host, uint64_t id, UINT ms) {
    host->StartTimer(ms, [host, id] {
        auto it = Toasts().find(host);
        if (it == Toasts().end() || it->second.id != id) return;  // closed or replaced meanwhile
        Widget* toast = it->second.widget;
        // Its action button is being pointed at or was reached with Tab: it waits (WCAG 2.2.1).
        if (toast->IsAncestorOf(host->Hot()) || toast->IsAncestorOf(host->Focus())) {
            CloseToastAfter(host, id, 1000);
            return;
        }
        host->ClosePopup(toast);
    });
}

}  // namespace

void ShowToast(Host* host, std::wstring text, std::string icon, UINT ms, std::wstring actionLabel,
               std::function<void()> action) {
    if (!host) return;
    static uint64_t nextId = 1;
    DismissToast(host);
    const bool clickable = static_cast<bool>(action);
    if (clickable) {
        // Clicking closes the toast too; posted, as the click runs inside the toast's own widget.
        action = [host, act = std::move(action)] {
            act();
            host->Post([host] { DismissToast(host); });
        };
    }
    const std::wstring spoken = actionLabel.empty() ? text : text + L". " + actionLabel;
    auto bubble = std::make_unique<ToastBubble>(std::move(text), std::move(icon), std::move(actionLabel), std::move(action));
    PopupOptions o;
    o.place = Place::At;
    // Placed again at every layout: a panel opened (or the bar shown) after the toast moves it.
    o.at = [host](Size s) {
        const Rect band = ToastBandOf(host);
        return Point{std::round(band.x + (band.w - s.w) / 2), host->toastTop ? host->toastTop(s.h) : 56.0f};
    };
    o.passthrough = !clickable;
    o.focusContent = false;
    o.dismissOnOutside = false;
    o.dismissOnEscape = false;
    o.ownsAbove = false;  // a panel opened while it shows must outlive it
    const uint64_t id = nextId++;
    // However it closes (its timer, a newer toast, CloseAllPopups), the slot forgets it: a later
    // popup at the same address is never taken for it.
    o.onDismiss = [host, id] {
        auto it = Toasts().find(host);
        if (it != Toasts().end() && it->second.id == id) Toasts().erase(it);
    };
    Toasts()[host] = {host->ShowPopup(std::move(bubble), o), id};
    a11y::Announce(host, spoken);
    // Long enough to read (3 s at least); one with a button (Undo, Open) also to reach the button.
    CloseToastAfter(host, id, std::max<UINT>(ms, clickable ? 8000 : 3000));
}

void DismissToast(Host* host) {
    if (!host) return;
    auto it = Toasts().find(host);
    if (it != Toasts().end()) host->ClosePopup(it->second.widget);
}

bool ToastShown(Host* host) {
    return Toasts().count(host) > 0;
}

}  // namespace ui

namespace ui {

// --- tree dump state (Host::DumpTree) ----------------------------------------------------

void IconView::DebugInfo(std::string& out) const {
    AppendIcon(out, icon);
}

void Button::DebugInfo(std::string& out) const {
    AppendIcon(out, icon);
}

void IconButton::DebugInfo(std::string& out) const {
    AppendIcon(out, icon);
    if (active) out += " active";
}

void Toggle::DebugInfo(std::string& out) const {
    out += on_ ? " on" : " off";
}

void Checkbox::DebugInfo(std::string& out) const {
    if (checked_) out += " checked";
}

void Segmented::DebugInfo(std::string& out) const {
    AppendNumber(out, "selected", selected_);
}

void Dropdown::DebugInfo(std::string& out) const {
    AppendNumber(out, "selected", selected_);
}

void Slider::DebugInfo(std::string& out) const {
    AppendNumber(out, "value", value_);
}

void TextBox::DebugInfo(std::string& out) const {
    out += " input";
    if (text_.empty()) out += " empty";  // the text shown is the placeholder
    if (password) out += " password";
    if (readOnly) out += " readonly";
}

void ScrollView::DebugInfo(std::string& out) const {
    out += " scroll";
    char buf[64];
    snprintf(buf, sizeof buf, " offset=%.0f/%.0f", offset_.Get(), MaxOffset());
    out += buf;
}

}  // namespace ui
