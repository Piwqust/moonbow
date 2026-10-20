// Settings > Audio equalizer: preamp + 10 vertical band sliders (design/ref/components/EQ_3178-16769.png).
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"

#include <cmath>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

constexpr float kColumnW = 60;     // design width of a band column
constexpr float kMinColumnW = 52;  // narrowest that still shows "-20,0" in the value box
constexpr float kScaleW = 30;      // "+20 / 0 / −20" left of the bands
constexpr float kBandH = 320;

class EqBody;
// Keeps a focused band (or value box) visible when the body scrolls sideways.
void RevealInBody(Widget* w);

// Vertical slider: 16-wide track, 24×8 thumb, fill from the bottom to the thumb.
class EqBand : public Widget, public Syncable {
public:
    explicit EqBand(const Item& it) : item(it) {
        interactive = true;
        focusable = true;
        width = Length::Px(24);
        height = Length::Px(kBandH);
        tooltip = item.Label(Lang());
        value_ = Cfg().GetDouble(item.key);
    }
    ~EqBand() override {
        if (timer_)
            if (Host* h = GetHost()) h->CancelTimer(timer_);
    }
    std::function<void(double)> onValue;

    void Sync() override {
        if (dragging_ || pending_) return;
        const double v = Cfg().GetDouble(item.key);
        if (v != value_) {
            value_ = v;
            if (onValue) onValue(v);
            Invalidate();
        }
    }
    Rect Track() const { return {rect.Cx() - 8, rect.y, 16, rect.h}; }
    float YOf(double v) const {
        const Rect t = Track();
        const double k = (v - item.min) / std::max(1e-9, item.max - item.min);
        return t.Bottom() - 6 - static_cast<float>(k) * (t.h - 12);
    }
    void SetFromY(float y) {
        const Rect t = Track();
        const double k = std::clamp((t.Bottom() - 6 - y) / std::max(1.0f, t.h - 12), 0.0f, 1.0f);
        Change(item.min + (item.max - item.min) * k, false);
    }
    void Change(double v, bool now) {
        v = std::clamp(v, item.min, item.max);
        if (item.step > 0) v = std::round(v / item.step) * item.step;
        if (v != value_) {
            value_ = v;
            pending_ = true;
            if (onValue) onValue(v);
            Invalidate();
        }
        if (now) return Commit();
        Host* h = GetHost();
        if (pending_ && h && !timer_)
            timer_ = h->StartTimer(60, [this] {
                timer_ = 0;
                Commit();
            });
    }
    void Commit() {
        if (timer_) {
            if (Host* h = GetHost()) h->CancelTimer(timer_);
            timer_ = 0;
        }
        if (!pending_) return;
        pending_ = false;
        Cfg().SetDouble(item.key, value_);
    }
    bool OnMouseDown(const MouseEvent& e) override {
        dragging_ = true;
        SetFromY(e.y);
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        if (dragging_) SetFromY(e.y);
        return dragging_;
    }
    bool OnMouseUp(const MouseEvent&) override {
        dragging_ = false;
        Commit();
        Invalidate();
        return true;
    }
    bool OnDoubleClick(const MouseEvent&) override {
        Change(0, true);
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        double v = value_;
        switch (e.key) {
            case VK_UP: v += 0.5; break;
            case VK_DOWN: v -= 0.5; break;
            case VK_PRIOR: v += 3; break;
            case VK_NEXT: v -= 3; break;
            case VK_HOME: v = item.max; break;
            case VK_END: v = item.min; break;
            case VK_DELETE:
            case VK_BACK: v = 0; break;
            default: return false;
        }
        Change(v, true);
        return true;
    }
    bool OnWheel(const WheelEvent& e) override {
        if (!IsFocused() || e.horizontal || e.mods.shift) return false;
        Change(value_ + 0.5 * e.delta, true);
        return true;
    }
    void OnFocusChanged(bool focused) override {
        if (focused) RevealInBody(this);
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const Rect t = Track();
        p.Fill(t, c.bgDeep, 6);
        const float y = YOf(value_);
        if (t.Bottom() - 2 - y > 0) p.Fill({t.x + 2, y, 12, t.Bottom() - 2 - y}, c.accentDim, 4);
        // Ticks every 5 dB, the 0 dB one a little longer.
        for (int db = static_cast<int>(item.min) + 5; db < static_cast<int>(item.max); db += 5) {
            const float ty = std::round(YOf(db));
            if (std::abs(ty - y) < 6) continue;
            const float w = db == 0 ? 8.0f : 4.0f;
            p.Fill({t.Cx() - w / 2, ty, w, 1}, ty > y ? c.accentMid.Fade(0.35f) : c.lineStrong, 0);
        }
        const Rect thumb{rect.x, y - 4, 24, 8};
        p.Fill(thumb, !IsEnabled() ? c.textDisabled : (dragging_ || IsHot()) ? c.accent : c.accentMid, 4);
        if (ShowFocusRing()) p.Stroke(thumb.Inset(-3), c.focusRing, 6, 2.0f);
    }
    const Item& item;

private:
    double value_ = 0;
    bool dragging_ = false;
    bool pending_ = false;
    UINT_PTR timer_ = 0;
};

// Editable "0.0 dB" box under each band (type a value, or Up/Down/wheel while focused).
class EqValue : public SettingField {
public:
    explicit EqValue(const Item& it) : SettingField(it) {
        width = Length::Px(kColumnW);
        height = Length::Px(32);
    }
    // Follows the band while it is dragged (the setting is written when the drag settles).
    void Show(double v) {
        if (!IsFocused()) SetText(FormatValue(item, v, false));
    }
    void OnFocusChanged(bool focused) override {
        SettingField::OnFocusChanged(focused);
        if (focused) RevealInBody(this);
    }
};

// dB axis next to the first band: the same Y mapping as EqBand.
class EqScale : public Widget {
public:
    explicit EqScale(const Item& band) : item_(band) {
        width = Length::Px(kScaleW);
        height = Length::Px(kBandH);
    }
    void Paint(Painter& p) override {
        for (const double db : {item_.max, 0.0, item_.min}) {
            const double k = (db - item_.min) / std::max(1e-9, item_.max - item_.min);
            const float y = rect.Bottom() - 6 - static_cast<float>(k) * (rect.h - 12);
            const int v = static_cast<int>(std::lround(db));
            const std::wstring s = v > 0 ? L"+" + std::to_wstring(v) : v < 0 ? L"−" + std::to_wstring(-v) : L"0";
            p.Text(s, {rect.x, y - 9, rect.w - 4, 18}, TextStyle::Small, P().textTertiary, HAlign::Trailing);
        }
    }

private:
    const Item& item_;
};

// The scale and the band columns. Columns narrow from 60 to 52 DIP with the page; below that the
// body scrolls sideways (wheel with Shift, a horizontal wheel or the bar under the columns) and
// clips them, so no band is cut by the window or drawn over the page's scrollbar.
class EqBody : public Widget, public Syncable {
public:
    explicit EqBody(const Item& condition) : condition_(condition) {
        dir = Dir::None;
        width = Length::Grow();
        clipChildren = true;
        interactive = true;
        on_ = Cfg().IsEnabled(condition_);
    }
    // Off: the columns are disabled, the body itself stays live so it still scrolls sideways.
    void Sync() override {
        const bool on = Cfg().IsEnabled(condition_);
        if (on != on_) {
            on_ = on;
            for (const Column& c : cols_) c.box->enabled = on_;
            Invalidate();
        }
    }
    struct Column {
        Widget* box;
        Widget* name;
        Widget* value;
    };
    void AddColumn(Column c) {
        c.box->enabled = on_;
        cols_.push_back(c);
    }
    EqScale* scale = nullptr;

    Size MeasureContent(float availW, float availH) override {
        float h = 0;
        for (auto& c : Children()) h = std::max(h, c->Measure(kColumnW, availH).h);
        return {std::min(availW, ContentW(kMinColumnW, 4)), h + pad.V()};
    }
    void OnLayout() override {
        const Rect in = rect.Inset(pad);
        const float n = static_cast<float>(cols_.size());
        float gapW = 8;
        float colW = std::floor((in.w - kScaleW - gapW * n) / std::max(1.0f, n));
        if (colW < kColumnW) {
            gapW = 4;
            colW = std::floor((in.w - kScaleW - gapW * n) / std::max(1.0f, n));
        }
        colW = std::clamp(colW, kMinColumnW, kColumnW);
        const float cw = ContentW(colW, gapW);
        maxScroll_ = std::max(0.0f, cw - in.w);
        scroll_ = std::clamp(scroll_, 0.0f, maxScroll_);
        float x = maxScroll_ > 0 ? in.x - scroll_ : std::round(in.x + (in.w - cw) / 2);
        const float h = in.h;
        if (scale) {
            scale->Arrange({x, in.y, kScaleW, kBandH});
            x += kScaleW + gapW;
        }
        for (const Column& c : cols_) {
            c.name->width = Length::Px(colW + gapW);
            c.value->width = Length::Px(colW);
            c.box->Arrange({x, in.y, colW, h});
            x += colW + gapW;
        }
    }
    void Reveal(Widget* w) {
        if (maxScroll_ <= 0) return;
        const Rect in = rect.Inset(pad);
        float s = scroll_;
        if (w->rect.x < in.x) s -= in.x - w->rect.x + 8;
        else if (w->rect.Right() > in.Right()) s += w->rect.Right() - in.Right() + 8;
        ScrollTo(s);
    }
    bool OnWheel(const WheelEvent& e) override {
        if (maxScroll_ <= 0 || !(e.horizontal || e.mods.shift)) return false;
        ScrollTo(scroll_ - e.delta * 60 * (e.horizontal ? -1.0f : 1.0f));
        return true;
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (maxScroll_ <= 0 || !Bar().Inset(Insets{0, -6, 0, -6}).Contains(e.x, e.y)) return false;
        dragging_ = true;
        const Rect t = Thumb();
        grab_ = t.Contains(e.x, e.y) ? e.x - t.x : t.w / 2;
        DragTo(e.x);
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        if (dragging_) DragTo(e.x);
        return dragging_;
    }
    bool OnMouseUp(const MouseEvent&) override {
        dragging_ = false;
        return true;
    }
    void Paint(Painter& p) override {
        if (!on_) p.PushOpacity(0.6f);
    }
    void PaintOver(Painter& p) override {
        if (!on_) p.PopOpacity();
        if (maxScroll_ > 0) {
            p.Fill(Bar(), P().bgDeep, 2);
            p.Fill(Thumb(), IsHot() || dragging_ ? P().textTertiary : P().lineStrong, 2);
        }
    }
    void DebugInfo(std::string& out) const override {
        if (maxScroll_ > 0) out += " hscroll offset=" + std::to_string(static_cast<int>(scroll_)) + "/" +
                                   std::to_string(static_cast<int>(maxScroll_));
    }

private:
    float ContentW(float colW, float gapW) const {
        const float n = static_cast<float>(cols_.size());
        return kScaleW + n * colW + gapW * n;
    }
    Rect Bar() const {
        const Rect in = rect.Inset(pad);
        return {in.x, rect.Bottom() - pad.b + 8, in.w, 4};  // in the bottom padding
    }
    Rect Thumb() const {
        const Rect b = Bar();
        const float total = b.w + maxScroll_;
        const float w = std::max(32.0f, b.w * b.w / total);
        const float x = b.x + (b.w - w) * (maxScroll_ > 0 ? scroll_ / maxScroll_ : 0);
        return {x, b.y, w, b.h};
    }
    void DragTo(float x) {
        const Rect b = Bar();
        const float w = Thumb().w;
        const float k = std::clamp((x - grab_ - b.x) / std::max(1.0f, b.w - w), 0.0f, 1.0f);
        ScrollTo(k * maxScroll_);
    }
    void ScrollTo(float s) {
        s = std::clamp(s, 0.0f, maxScroll_);
        if (s == scroll_) return;
        scroll_ = s;
        InvalidateLayout();
    }
    const Item& condition_;
    std::vector<Column> cols_;
    float scroll_ = 0, maxScroll_ = 0, grab_ = 0;
    bool dragging_ = false;
    bool on_ = true;
};

void RevealInBody(Widget* w) {
    for (Widget* p = w->Parent(); p; p = p->Parent())
        if (auto* body = dynamic_cast<EqBody*>(p)) return body->Reveal(w);
}

class Equalizer : public Widget {
public:
    Equalizer(const std::vector<const Item*>& items, PageHost& page) : page_(page) {
        dir = Dir::Column;
        width = Length::Grow();
        align = Align::Center;
        open_ = !page.state || page.state->Open("equalizer", true);
        header_ = Add<Header>(this);
        body_ = Add<EqBody>(*items.front());
        body_->pad = {8, 24, 8, 20};
        body_->scale = body_->Add<EqScale>(*items.front());
        for (const Item* it : items) {
            auto* col = body_->Add<Widget>();
            col->dir = Dir::Column;
            col->align = Align::Center;
            col->width = Length::Px(kColumnW);
            auto* band = col->Add<EqBand>(*it);
            col->Add<Widget>()->height = Length::Px(30);
            const bool preamp = it->key == settings::kEqPreampKey;
            auto* name = col->Add<Label>(preamp ? std::wstring(Tr(L"Preamp")) : it->Label(Lang()), TextStyle::Body,
                                         Tone::Tertiary);
            name->halign = HAlign::Center;
            name->width = Length::Px(kColumnW + 8);
            // The column says "Gain" for short; the tooltip names it in full and says what it does.
            if (preamp) name->tooltip = Tr(L"Preamp: the level before the bands");
            col->Add<Widget>()->height = Length::Px(10);
            auto* value = col->Add<EqValue>(*it);
            band->onValue = [value](double v) { value->Show(v); };
            body_->AddColumn({col, name, value});
        }
        body_->visible = open_;
        pad = {0, 6, 0, 0};
    }
    bool IsOpen() const { return open_; }
    void Toggle() {
        open_ = !open_;
        body_->visible = open_;
        if (page_.state) page_.state->open["equalizer"] = open_;
        InvalidateLayout();
        if (page_.relayout) page_.relayout();
    }
    Size MeasureContent(float availW, float availH) override {
        Size s = Widget::MeasureContent(availW, availH);
        if (!open_) s.h = 36;
        return s;
    }
    void Paint(Painter& p) override { p.Fill(rect, open_ ? P().bgPanel : P().bgDeep, open_ ? 2.0f : 4.0f); }

private:
    class Header : public Widget {
    public:
        explicit Header(Equalizer* eq) : eq_(eq) {
            interactive = true;
            focusable = true;
            width = Length::Px(220);
            height = Length::Px(24);
        }
        void Paint(Painter& p) override {
            const Color fg = IsHot() ? P().textPrimary : P().textSecondary;
            const wchar_t* text = eq_->IsOpen() ? Tr(L"Close Equalizer") : Tr(L"Open Equalizer");
            const float tw = p.Measure(text, TextStyle::Body).width;
            const float x = rect.Cx() - (tw + 34) / 2;
            p.Text(text, {x, rect.y, tw + 1, rect.h}, TextStyle::Body, fg, HAlign::Leading, VAlign::Center, 0);
            p.Icon(eq_->IsOpen() ? "chevron_up" : "chevron_down", {x + tw + 10, rect.y, 24, 24}, fg);
            PaintFocusRing(p, rect, 4);
        }
        void OnClick() override { eq_->Toggle(); }

    private:
        Equalizer* eq_;
    };

    PageHost& page_;
    Header* header_ = nullptr;
    EqBody* body_ = nullptr;
    bool open_ = true;
};

}  // namespace

std::unique_ptr<Widget> MakeEqualizer(const std::vector<const Item*>& items, PageHost& page) {
    if (items.empty()) return nullptr;
    // Preamp first, then the bands in frequency order (schema order already is).
    std::vector<const Item*> ordered;
    for (const Item* it : items)
        if (it->key == settings::kEqPreampKey) ordered.push_back(it);
    for (const Item* it : items)
        if (it->key != settings::kEqPreampKey) ordered.push_back(it);
    auto wrap = std::make_unique<Widget>();
    wrap->dir = Dir::Column;
    wrap->width = Length::Grow();
    wrap->pad = {0, kRowPad, 0, kRowPad};
    wrap->id = "equalizer";
    wrap->Add<Equalizer>(ordered, page);
    return wrap;
}

}  // namespace app::sui
