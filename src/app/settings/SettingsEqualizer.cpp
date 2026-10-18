// Settings > Audio equalizer: preamp + 10 vertical band sliders (design/ref/components/EQ_3178-16769.png).
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"

#include <cmath>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

constexpr float kColumnW = 60;
constexpr float kBandH = 320;

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
        if (!IsFocused()) return false;
        Change(value_ + 0.5 * e.delta, true);
        return true;
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
        if (ShowFocusRing()) p.Stroke(thumb.Inset(-3), c.accentMid, 6, 1.5f);
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
};

class EqBody : public Widget, public Syncable {
public:
    explicit EqBody(const Item& condition) : condition_(condition) {
        dir = Dir::Row;
        gap = 8;
        justify = Justify::Center;
        width = Length::Grow();
        enabled = Cfg().IsEnabled(condition_);
    }
    void Sync() override {
        const bool on = Cfg().IsEnabled(condition_);
        if (on != enabled) {
            enabled = on;
            Invalidate();
        }
    }
    void Paint(Painter& p) override {
        if (!enabled) p.PushOpacity(0.6f);
    }
    void PaintOver(Painter& p) override {
        if (!enabled) p.PopOpacity();
    }

private:
    const Item& condition_;
};

class Equalizer : public Widget {
public:
    Equalizer(const std::vector<const Item*>& items, PageHost& page) : page_(page) {
        dir = Dir::Column;
        width = Length::Grow();
        align = Align::Center;
        open_ = !page.state || page.state->Open("equalizer", true);
        header_ = Add<Header>(this);
        body_ = Add<EqBody>(*items.front());
        body_->pad = {0, 24, 0, 20};
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
            if (preamp) name->tooltip = it->Label(Lang());
            col->Add<Widget>()->height = Length::Px(10);
            auto* value = col->Add<EqValue>(*it);
            band->onValue = [value](double v) { value->Show(v); };
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
