#include "app/player/PanelKit.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/player/LiveState.h"
#include "gfx/Text.h"

#include <cwchar>
#include <map>

namespace app::panel {

using namespace ui;

Color RowText() {
    return Color::Mix(P().textSecondary, P().textPrimary, 0.08f);
}

Color SectionLine() {
    return Color::Mix(P().lineSubtle, P().line, 0.6f);
}

std::wstring LocalDecimal(std::wstring s) {
    if (CurrentLanguage() == Language::Russian)
        for (wchar_t& ch : s)
            if (ch == L'.') ch = L',';
    return s;
}

std::wstring FormatSeconds(double s, int decimals) {
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f %s", decimals, std::abs(s) < 0.0005 ? 0.0 : s, Tr(L"s"));
    return LocalDecimal(buf);
}

std::wstring FormatNumber(double v, int maxDecimals) {
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f", maxDecimals, v);
    std::wstring s = buf;
    if (s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if (s == L"-0") s = L"0";
    return LocalDecimal(std::move(s));
}

// --- TabBar ------------------------------------------------------------------------------

TabBar::TabBar(std::vector<Tab> tabs, int selected) : tabs_(std::move(tabs)), selected_(selected) {
    interactive = true;
    focusable = true;
    width = Length::Grow();
    height = Length::Px(kHeaderH);
}

float TabBar::ContentWidth(const Tab& t) const {
    return (showIcons_ ? 28.0f : 0.0f) + gfx::Text::Get().Measure(Tr(t.label.c_str()), TextStyle::Small).width;
}

void TabBar::OnLayout() {
    // Tabs share the width (6 DIP inset on both sides) in proportion to their content. When
    // icon + label leave too little air (four Russian tabs), the tabs show labels only.
    const float avail = rect.w - 12;
    const float n = tabs_.empty() ? 1.0f : static_cast<float>(tabs_.size());
    std::vector<float> content;
    float total = 0;
    for (int pass = 0; pass < 2; ++pass) {
        showIcons_ = pass == 0;
        content.clear();
        total = 0;
        for (const Tab& t : tabs_) {
            const float w = ContentWidth(t);
            content.push_back(w);
            total += w;
        }
        if ((avail - total) / n >= 28) break;
    }
    widths_.clear();
    const float extra = tabs_.empty() ? 0 : (avail - total) / n;
    for (float w : content) widths_.push_back(std::max(40.0f, w + extra));
    const Rect r = TabRect(selected_);
    const float lw = std::max(24.0f, r.w - (tabs_.size() > 3 ? 14.0f : 16.0f));
    if (!placed_) {
        lineX_.Set(r.Cx() - lw / 2);
        lineW_.Set(lw);
        placed_ = true;
    } else if (std::abs(lineX_.Target() - (r.Cx() - lw / 2)) > 0.5f || std::abs(lineW_.Target() - lw) > 0.5f) {
        lineX_.Set(r.Cx() - lw / 2);
        lineW_.Set(lw);
    }
}

Rect TabBar::TabRect(int i) const {
    float x = rect.x + 6;
    for (int k = 0; k < i && k < static_cast<int>(widths_.size()); ++k) x += widths_[static_cast<size_t>(k)];
    const float w = i >= 0 && i < static_cast<int>(widths_.size()) ? widths_[static_cast<size_t>(i)] : 0;
    return {x, rect.y, w, rect.h};
}

int TabBar::IndexAt(float x) const {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const Rect r = TabRect(i);
        if (x >= r.x && x < r.Right()) return i;
    }
    return -1;
}

void TabBar::SetSelected(int i, bool animate) {
    if (i < 0 || i >= static_cast<int>(tabs_.size())) return;
    selected_ = i;
    const Rect r = TabRect(i);
    const float lw = std::max(24.0f, r.w - (tabs_.size() > 3 ? 14.0f : 16.0f));
    if (animate && placed_) {
        lineX_.To(r.Cx() - lw / 2, 180, Ease::Out);
        lineW_.To(lw, 180, Ease::Out);
    } else {
        lineX_.Set(r.Cx() - lw / 2);
        lineW_.Set(lw);
    }
    Invalidate();
}

void TabBar::Select(int i) {
    if (i < 0 || i >= static_cast<int>(tabs_.size()) || i == selected_) return;
    SetSelected(i);
    if (onSelect) onSelect(i);
}

void TabBar::Paint(Painter& p) {
    const Palette& c = P();
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const Tab& t = tabs_[static_cast<size_t>(i)];
        const Rect r = TabRect(i);
        const bool sel = i == selected_;
        const bool hot = i == hover_ && IsHot();
        const std::wstring label = Tr(t.label.c_str());
        const float tw = gfx::Text::Get().Measure(label, TextStyle::Small).width;
        const float x = std::round(r.Cx() - ContentWidth(t) / 2);
        // Without the two-tone icon the selected label itself carries the state.
        const Color fg = sel ? (showIcons_ ? c.textSecondary : c.textPrimary) : hot ? c.textSecondary : c.textTertiary;
        const std::string& icon = sel && !t.iconActive.empty() ? t.iconActive : t.icon;
        if (hot && !sel) p.Fill({r.x + 4, r.y + 8, r.w - 8, r.h - 16}, c.hover, 4);
        if (showIcons_) p.Icon(icon, {x, r.y + 14, 20, 20}, sel ? c.textSecondary : fg, c.accent);
        p.Text(label, {showIcons_ ? x + 28 : x, r.y + 14, tw + 2, 20}, TextStyle::Small, fg, HAlign::Leading,
               VAlign::Center, 0);
    }
    if (!tabs_.empty()) p.Fill({std::round(lineX_.Get()), rect.Bottom() - 2, std::round(lineW_.Get()), 2}, c.accent, 1);
    if (ShowFocusRing()) {
        const Rect r = TabRect(selected_);
        PaintFocusRing(p, {r.x + 2, r.y + 6, r.w - 4, r.h - 12}, 4);
    }
}

bool TabBar::OnMouseMove(const MouseEvent& e) {
    const int i = IndexAt(e.x);
    if (i != hover_) {
        hover_ = i;
        Invalidate();
    }
    return true;
}

bool TabBar::OnMouseDown(const MouseEvent& e) {
    Select(IndexAt(e.x));
    return true;
}

void TabBar::OnMouseLeave() {
    hover_ = -1;
    Invalidate();
}

bool TabBar::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_LEFT) Select(std::max(0, selected_ - 1));
    else if (e.key == VK_RIGHT) Select(std::min(static_cast<int>(tabs_.size()) - 1, selected_ + 1));
    else if (e.key == VK_HOME) Select(0);
    else if (e.key == VK_END) Select(static_cast<int>(tabs_.size()) - 1);
    else return false;
    return true;
}

// --- Popover -------------------------------------------------------------------------------

Popover::Popover() {
    dir = Dir::None;
    interactive = true;  // clicks on empty parts stay inside
    focusable = false;
}

Popover::~Popover() = default;

void Popover::SetTabs(std::vector<Tab> tabs, int selected) {
    if (tabs_) {
        RemoveChild(tabs_);
        tabs_ = nullptr;
    }
    tab_ = selected;
    auto bar = std::make_unique<TabBar>(std::move(tabs), selected);
    bar->onSelect = [this](int i) { ShowTab(i); };
    tabs_ = static_cast<TabBar*>(AddChild(std::move(bar), 0));
}

void Popover::SetTitle(std::wstring title) {
    title_ = std::move(title);
}

void Popover::SetFooter(std::unique_ptr<Widget> footer) {
    if (footer_) RemoveChild(footer_);
    footer_ = footer ? AddChild(std::move(footer)) : nullptr;
    InvalidateLayout();
}

void Popover::ShowTab(int tab) {
    tab_ = tab;
    if (tabs_ && tabs_->Selected() != tab) tabs_->SetSelected(tab);
    if (body_) {
        RemoveChild(body_);
        body_ = nullptr;
    }
    if (build) {
        std::unique_ptr<Widget> b = build(tab);
        if (b) body_ = AddChild(std::move(b), tabs_ ? 1 : 0);
    }
    if (onTab) onTab(tab);
    InvalidateLayout();
    if (Host* h = GetHost()) h->RequestRender();
}

Size Popover::MeasureContent(float, float) {
    const Rect r = place ? place() : Rect{0, 0, kPanelW, 400};
    return {r.w, r.h};
}

void Popover::OnLayout() {
    if (place) rect = place();
    float top = rect.y;
    const bool header = tabs_ || !title_.empty();
    if (tabs_) tabs_->Arrange({rect.x, rect.y, rect.w, kHeaderH});
    if (header) top += kHeaderH;
    float bottom = rect.Bottom();
    if (footer_) {
        if (fitFooter) fitFooter(rect.h);
        const Size fs = footer_->Measure(rect.w, rect.h);
        footer_->Arrange({rect.x, bottom - fs.h, rect.w, fs.h});
        bottom -= fs.h;
    }
    if (body_) body_->Arrange({rect.x, top, rect.w, std::max(0.0f, bottom - top)});
}

void Popover::Paint(Painter& p) {
    const Palette& c = P();
    p.Shadow(rect, radius, 15, 8);
    // Opaque: a translucent frame picks up the colors of the video under it.
    p.Fill(rect, fill ? fill() : c.bgBase, radius);
    if (tabs_ || !title_.empty()) {
        p.PushRoundClip(rect, radius);
        const Rect h{rect.x, rect.y, rect.w, kHeaderH};
        p.Fill(h, tabs_ ? c.bgDeep : c.bgChrome);
        if (!title_.empty()) p.Text(title_, h, TextStyle::Body, c.textSecondary, HAlign::Center);
        p.PopClip();
    }
}

void Popover::PaintOver(Painter& p) {
    p.Stroke(rect, P().line, radius);
}

bool Popover::OnKeyDown(const KeyEvent& e) {
    if (tabs_ && e.key == VK_TAB && e.mods.ctrl) {
        tabs_->OnKeyDown(KeyEvent{static_cast<UINT>(e.mods.shift ? VK_LEFT : VK_RIGHT), {}, false});
        return true;
    }
    return false;
}

// --- SyncPage ------------------------------------------------------------------------------

SyncPage::SyncPage() {
    dir = Dir::Column;
    width = Length::Grow();
    pad = {0, 0, 0, 12};
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & ~kChangeTime) SyncAll();
    });
    liveConn_ = Connect(Live().changed, [this] { SyncAll(); });
    queueConn_ = Connect(TheApp().queue().changed, [this] { SyncAll(); });
    // Config listeners run inside Set(); defer so a control can finish its own change first.
    configToken_ = TheApp().config().Subscribe("", [this](const std::string&) {
        if (posted_) return;
        Host* h = GetHost();
        if (!h) return;
        posted_ = true;
        std::weak_ptr<bool> alive = alive_;
        h->Post([this, alive] {
            if (alive.expired()) return;
            posted_ = false;
            SyncAll();
        });
    });
}

SyncPage::~SyncPage() {
    TheApp().config().Unsubscribe(configToken_);
}

void SyncPage::SyncAll() {
    for (auto& fn : syncers_) fn();
    Invalidate();
}

// --- rows ------------------------------------------------------------------------------------

Widget* AddSection(Widget* page) {
    auto* s = page->Add<Widget>();
    s->dir = Dir::Column;
    s->width = Length::Grow();
    s->pad = {20, 0, 20, 0};
    return s;
}

void AddSectionLine(Widget* page) {
    class Line : public Widget {
    public:
        Line() {
            width = Length::Grow();
            height = Length::Px(1);
        }
        void Paint(Painter& p) override { p.Fill(rect, SectionLine()); }
    };
    page->Add<Line>();
}

void AddSectionTitle(Widget* page, const std::wstring& text) {
    class Title : public Widget {
    public:
        explicit Title(std::wstring t) : text_(std::move(t)) {
            width = Length::Grow();
            height = Length::Px(39);
        }
        void Paint(Painter& p) override {
            // Same x as the row labels and controls of the sections (20 DIP).
            p.Text(text_, {rect.x + 20, rect.y + 18, rect.w - 40, 22}, TextStyle::Heading, RowText());
        }

    private:
        std::wstring text_;
    };
    page->Add<Title>(text);
}

void AddCaption(Widget* section, const std::wstring& text, float h) {
    auto* l = section->Add<Label>(text, TextStyle::Body, Tone::Tertiary);
    l->width = Length::Grow();
    l->height = Length::Px(h);
    l->pad = {4, 0, 0, 0};
}

Widget* AddRow(Widget* section, const std::wstring& label, std::unique_ptr<Widget> control, float h) {
    auto* row = section->Add<Widget>();
    row->dir = Dir::Row;
    row->align = Align::Center;
    row->width = Length::Grow();
    row->height = Length::Px(h);
    row->gap = 8;  // labels paint to their right edge (pad only affects measuring)
    auto* l = row->Add<Label>(label, TextStyle::Body, Tone::Tertiary);
    l->width = Length::Grow();
    l->pad = {4, 0, 8, 0};
    // Long (Russian) labels take a second line instead of being cut off. The label spans the
    // row height: it is measured at the full row width, so its own height would be one line.
    l->wrap = true;
    l->maxLines = 2;
    l->height = Length::Grow();
    if (control) row->AddChild(std::move(control));
    return row;
}

Label* AddValueRow(Widget* section, const std::wstring& label, float h) {
    auto* row = section->Add<Widget>();
    row->dir = Dir::Row;
    row->align = Align::Center;
    row->width = Length::Grow();
    row->height = Length::Px(h);
    auto* l = row->Add<Label>(label, TextStyle::Body, Tone::Tertiary);
    l->width = Length::Grow();
    l->pad = {4, 0, 8, 0};
    auto* v = row->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
    v->halign = HAlign::Trailing;
    v->pad = {0, 0, 4, 0};
    v->width = Length::Px(140);
    return v;
}

// --- Range ------------------------------------------------------------------------------------

Range::Range(float mn, float mx, float v, float st) : min(mn), max(mx), step(st), value_(v) {
    interactive = true;
    focusable = true;
    width = Length::Grow();
}

Size Range::MeasureContent(float availW, float) {
    return {availW, 24 + (labels.empty() ? 0.0f : 20.0f)};
}

Rect Range::TrackRect() const {
    return {rect.x, rect.y + (24 - trackH) / 2, rect.w, trackH};
}

float Range::Snap(float v) const {
    v = std::clamp(v, min, max);
    if (step > 0) v = min + std::round((v - min) / step) * step;
    return std::clamp(v, min, max);
}

void Range::SetValue(float v) {
    if (dragging_) return;
    v = Snap(v);
    if (v == value_) return;
    value_ = v;
    Invalidate();
}

void Range::SetFromX(float x) {
    const Rect t = TrackRect();
    const float k = std::clamp((x - t.x - 4) / std::max(1.0f, t.w - 8), 0.0f, 1.0f);
    const float v = Snap(min + (max - min) * k);
    if (v == value_) return;
    value_ = v;
    if (onChange) onChange(value_);
    Invalidate();
}

void Range::Paint(Painter& p) {
    const Palette& c = P();
    const Rect t = TrackRect();
    p.Fill(t, c.bgDeep, 6);
    const auto xOf = [&](float v) { return t.x + 4 + (t.w - 8) * ((v - min) / std::max(1e-6f, max - min)); };
    const float o = std::isnan(origin) ? min : std::clamp(origin, min, max);
    const float x0 = xOf(o), x1 = xOf(value_);
    if (std::abs(x1 - x0) > 0.5f) p.Fill({std::min(x0, x1), t.y + 2, std::abs(x1 - x0), t.h - 4}, c.accentDim, 4);
    for (float tick : ticks) {
        const float tx = std::round(xOf(tick));
        p.Fill({tx, t.Cy() - 3, 1, 6}, tick <= std::max(o, value_) && tick >= std::min(o, value_) ? c.accentFaint : c.line);
    }
    const bool hot = IsHot() || dragging_;
    const Rect thumb{std::round(x1 - 4), rect.y, 8, 24};
    p.Fill(thumb, !IsEnabled() ? c.textDisabled : hot ? c.accent : c.accentMid, 4);
    if (ShowFocusRing()) PaintFocusRing(p, thumb.Inset(-2), 5);
    for (size_t i = 0; i < labels.size(); ++i) {
        const float lx = xOf(labels[i].first);
        const Rect lr{lx - 40, rect.y + 28, 80, 16};
        HAlign h = HAlign::Center;
        Rect r = lr;
        if (i == 0 && labels[i].first <= min) {
            r = {t.x, lr.y, 80, 16};
            h = HAlign::Leading;
        } else if (i + 1 == labels.size() && labels[i].first >= max) {
            r = {t.Right() - 80, lr.y, 80, 16};
            h = HAlign::Trailing;
        }
        p.Text(labels[i].second, r, TextStyle::Small, c.textTertiary, h);
    }
}

bool Range::OnMouseDown(const MouseEvent& e) {
    dragging_ = true;
    SetFromX(e.x);
    return true;
}

bool Range::OnMouseMove(const MouseEvent& e) {
    if (dragging_) SetFromX(e.x);
    return true;
}

bool Range::OnMouseUp(const MouseEvent&) {
    if (dragging_) {
        dragging_ = false;
        if (onCommit) onCommit(value_);
    }
    Invalidate();
    return true;
}

bool Range::OnKeyDown(const KeyEvent& e) {
    const float d = step > 0 ? step : (max - min) / 20;
    float v = value_;
    if (e.key == VK_LEFT || e.key == VK_DOWN) v -= d;
    else if (e.key == VK_RIGHT || e.key == VK_UP) v += d;
    else if (e.key == VK_HOME) v = min;
    else if (e.key == VK_END) v = max;
    else return false;
    v = Snap(v);
    if (v != value_) {
        value_ = v;
        if (onChange) onChange(value_);
        if (onCommit) onCommit(value_);
        Invalidate();
    }
    return true;
}

bool Range::OnWheel(const WheelEvent& e) {
    if (!IsFocused() || e.horizontal) return false;
    const float d = step > 0 ? step : (max - min) / 20;
    const float v = Snap(value_ + d * e.delta);
    if (v != value_) {
        value_ = v;
        if (onChange) onChange(value_);
        if (onCommit) onCommit(value_);
        Invalidate();
    }
    return true;
}

// --- ValueBox ---------------------------------------------------------------------------------

ValueBox::ValueBox() {
    width = Length::Px(140);
    height = Length::Px(36);
    onSubmit = [this](const std::wstring&) {
        Commit();
        if (Host* h = GetHost()) h->SetFocus(nullptr);
    };
    onEscape = [this] {
        if (Host* h = GetHost()) h->SetFocus(nullptr);
    };
}

void ValueBox::Refresh() {
    if (IsFocused() || !text) return;
    const std::wstring t = text();
    shown_ = t;
    if (t != Text()) SetText(t);
}

void ValueBox::Commit() {
    if (Text() == shown_) return;
    if (onText) {
        onText(Text());
    } else if (onValue) {
        std::wstring s = Text();
        for (wchar_t& ch : s)
            if (ch == L',') ch = L'.';
        wchar_t* end = nullptr;
        const double v = std::wcstod(s.c_str(), &end);
        if (end != s.c_str()) onValue(v);
    }
    if (text) {
        shown_ = text();
        SetText(shown_);
    }
}

void ValueBox::OnFocusChanged(bool focused) {
    EditBox::OnFocusChanged(focused);
    if (!focused) {
        Commit();
    } else {
        SelectAll();
        selectOnUp_ = true;  // the click that focused us would otherwise drop the selection
    }
}

bool ValueBox::OnMouseUp(const MouseEvent& e) {
    const bool r = EditBox::OnMouseUp(e);
    if (selectOnUp_) {
        selectOnUp_ = false;
        SelectAll();
    }
    return r;
}

void ValueBox::Paint(Painter& p) {
    // The design's text_input: bgDeep, 1 px bgPanel border, radius 6.
    background = P().bgDeep;
    TextBox::Paint(p);
    if (!IsFocused() && !IsHot()) p.Stroke(rect, P().bgPanel, 4);
}

// --- Expander ---------------------------------------------------------------------------------

namespace {
std::map<std::string, bool>& ExpandedState() {
    static std::map<std::string, bool> m;
    return m;
}
}  // namespace

class Expander::Header : public Widget {
public:
    explicit Header(Expander* owner) : owner_(owner) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(48);
        onClick = [this] { owner_->SetExpanded(!owner_->Expanded()); };
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool open = owner_->Expanded();
        if (IsHot()) p.Fill(rect, c.hover, open ? 0.0f : 6.0f);
        PaintFocusRing(p, rect, 6);
        const Color fg = IsHot() ? c.textSecondary : c.textTertiary;
        const std::wstring text =
            !owner_->title.empty() ? owner_->title : open ? Tr(L"Collapse Settings") : Tr(L"Expand Settings");
        p.Text(text, {rect.x + 16, rect.y, rect.w - 60, rect.h}, TextStyle::Body, fg);
        p.Icon(open ? "chevron_down" : "chevron_right", {rect.Right() - 34, rect.Cy() - 12, 24, 24}, fg);
    }

private:
    Expander* owner_;
};

Expander::Expander(std::string key) : key_(std::move(key)) {
    dir = Dir::Column;
    width = Length::Grow();
    header_ = Add<Header>(this);
    body_ = Add<Widget>();
    body_->dir = Dir::Column;
    body_->width = Length::Grow();
    body_->pad = {16, 0, 16, 12};
    expanded_ = ExpandedState()[key_];
    body_->visible = expanded_;
}

void Expander::SetExpanded(bool on) {
    expanded_ = on;
    ExpandedState()[key_] = on;
    body_->visible = on;
    reveal_ = on;
    InvalidateLayout();
}

void Expander::OnLayout() {
    Widget::OnLayout();
    if (!reveal_) return;
    reveal_ = false;
    // Scroll the unfolded content into view, keeping the header on screen.
    ScrollView* sv = nullptr;
    for (Widget* w = Parent(); w && !sv; w = w->Parent()) sv = dynamic_cast<ScrollView*>(w);
    if (!sv) return;
    const float d = std::min(rect.Bottom() + 8 - sv->rect.Bottom(), rect.y - 8 - sv->rect.y);
    if (d > 0) sv->ScrollTo(sv->Offset() + d);
}

void Expander::Paint(Painter& p) {
    p.Fill(rect, expanded_ ? P().bgChrome : P().bgDeep, 6);
}

// --- FlatButton -------------------------------------------------------------------------------

FlatButton::FlatButton(std::wstring t, std::string i, bool f) : text(std::move(t)), icon(std::move(i)), filled(f) {
    interactive = true;
    focusable = true;
    width = Length::Grow();
    height = Length::Px(32);
}

void FlatButton::Paint(Painter& p) {
    const Palette& c = P();
    const bool hot = IsHot(), down = IsPressed();
    if (filled) p.Fill(rect, c.bgDeep, 4);
    if (down) p.Fill(rect, c.press, 4);
    else if (hot) p.Fill(rect, c.hover, 4);
    PaintFocusRing(p, rect, 4);
    const Color fg = hot ? c.textSecondary : c.textTertiary;
    const float tw = gfx::Text::Get().Measure(text, TextStyle::Body).width;
    const float iw = icon.empty() ? 0.0f : text.empty() ? 20.0f : 26.0f;
    const float x = std::round(rect.Cx() - (tw + iw) / 2);
    if (!icon.empty()) p.Icon(icon, {x, rect.Cy() - 10, 20, 20}, fg);
    p.Text(text, {x + iw, rect.y, tw + 2, rect.h}, TextStyle::Body, fg, HAlign::Leading, VAlign::Center, 0);
}

}  // namespace app::panel
