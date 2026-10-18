// Schema rows of the settings window: one control per settings::ItemType, laid out as in
// design/ref/Settings-*.png (label column on the left, 320-wide control column on the right).
#include "app/App.h"
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "gfx/Gpu.h"

#include <commdlg.h>
#include <shobjidl.h>

#include <cmath>
#include <cwchar>

#pragma comment(lib, "comdlg32.lib")

namespace app::sui {

using namespace ui;
using settings::Item;
using settings::ItemType;

// --- helpers --------------------------------------------------------------------------------

settings::Lang Lang() {
    return CurrentLanguage() == Language::Russian ? settings::Lang::Ru : settings::Lang::En;
}

Config& Cfg() {
    return TheApp().config();
}

Color HeadingColor() {
    return Color::Mix(P().textPrimary, P().textSecondary, 0.36f);
}

void SyncTree(Widget* w) {
    if (!w) return;
    if (auto* s = dynamic_cast<Syncable*>(w)) s->Sync();
    for (auto& c : w->Children()) SyncTree(c.get());
}

int Decimals(const Item& item) {
    const double step = item.step;
    if (step <= 0) return 1;
    for (int d = 0; d < 3; ++d) {
        const double scaled = step * std::pow(10.0, d);
        if (std::abs(scaled - std::round(scaled)) < 1e-6) return d;
    }
    return 2;
}

std::wstring FormatValue(const Item& item, double v, bool withUnit) {
    const int d = Decimals(item);
    wchar_t buf[64];
    swprintf_s(buf, L"%.*f", d, v);
    std::wstring s = buf;
    if (d >= 2) {  // 1.00 -> 1, 0.50 -> 0.5
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if (s == L"-0") s = L"0";
    const std::wstring& unit = item.Unit(Lang());
    if (!withUnit || unit.empty()) return s;
    if (unit == L"%" || unit == L"×") return s + unit;
    return s + L" " + unit;
}

namespace {

bool IsWideAction(const std::string& action) {
    return action == "edit-shortcuts" || action == "reset-shortcuts" || action == "rescan-plugins";
}

// Label text followed by the (i) icon; the text is shortened when space runs out.
class InfoIcon : public Widget {
public:
    explicit InfoIcon(std::wstring text) {
        tooltip = std::move(text);
        interactive = true;
        width = Length::Px(18);
        height = Length::Px(18);
    }
    void Paint(Painter& p) override {
        p.Icon("info", rect, IsHot() ? P().textSecondary : P().textTertiary);
    }
};

class RowLabel : public Widget {
public:
    RowLabel(std::wstring text, std::wstring description) {
        width = Length::Grow();
        label_ = Add<Label>(std::move(text), TextStyle::Body, Tone::Secondary);
        if (!description.empty()) info_ = Add<InfoIcon>(std::move(description));
    }
    Size MeasureContent(float availW, float) override {
        const Size l = label_->Measure(availW, kInf);
        return {l.w + (info_ ? 28.0f : 0.0f), std::max(l.h, 18.0f)};
    }
    void OnLayout() override {
        const float reserve = info_ ? 28.0f : 0.0f;
        const Size l = label_->Measure(kInf, kInf);
        const float lw = std::min(l.w, std::max(0.0f, rect.w - reserve));
        label_->Arrange({rect.x, rect.Cy() - l.h / 2, lw, l.h});
        if (info_) info_->Arrange({rect.x + lw + 10, rect.Cy() - 9, 18, 18});
    }

private:
    Label* label_ = nullptr;
    InfoIcon* info_ = nullptr;
};

// --- simple controls ------------------------------------------------------------------------

class BoolToggle : public Toggle, public Syncable {
public:
    explicit BoolToggle(const Item& it) : Toggle(Cfg().GetBool(it.key)), item(it) {
        width = Length::Px(68);
        height = Length::Px(32);
        onChange = [this](bool on) { Cfg().SetBool(item.key, on); };
    }
    void Sync() override { SetValue(Cfg().GetBool(item.key)); }
    const Item& item;
};

std::vector<ui::Choice> ChoicesOf(const Item& item) {
    std::vector<ui::Choice> out;
    for (const settings::Choice& c : item.choices) {
        ui::Choice ch{c.Label(Lang()), c.value, {}};
        if (!c.extra.empty() && c.extra[0] != '#') ch.icon = c.extra;
        out.push_back(std::move(ch));
    }
    return out;
}

int IndexOf(const std::vector<ui::Choice>& choices, const std::string& value) {
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i].value == value) return static_cast<int>(i);
    return -1;
}

class ChoiceDropdown : public Dropdown, public Syncable {
public:
    explicit ChoiceDropdown(const Item& it) : Dropdown(ChoicesOf(it), -1), item(it) {
        width = Length::Px(kControlW);
        height = Length::Px(36);
        if (item.dynamicChoices) Refresh();
        SetSelected(IndexOf(Choices(), Cfg().GetString(item.key)));
        onChange = [this](int i) {
            if (i < 0 || i >= static_cast<int>(Choices().size())) return;
            const std::string value = Choices()[static_cast<size_t>(i)].value;
            if (item.key == "audio.eqPreset" && settings::FindEqPreset(value))
                Cfg().ApplyEqPreset(value);
            else
                Cfg().SetString(item.key, value);
        };
    }
    void OnClick() override {
        if (item.dynamicChoices) Refresh();
        Dropdown::OnClick();
    }
    void Sync() override {
        const std::string v = Cfg().GetString(item.key);
        int i = IndexOf(Choices(), v);
        if (i < 0 && item.dynamicChoices && !v.empty()) {
            Refresh();
            i = IndexOf(Choices(), v);
        }
        if (i != Selected()) SetSelected(i);
    }
    const Item& item;

private:
    // Output devices reported by mpv ("audio-device-list"); the current value stays listed even
    // when the device is unplugged, so the setting is never silently replaced.
    void Refresh() {
        std::vector<ui::Choice> list = ChoicesOf(item);
        Player& player = TheApp().player();
        if (player.Ready()) {
            if (auto doc = json::Parse(player.GetPropertyString("audio-device-list"))) {
                for (const json::Value& d : doc->AsArray()) {
                    const std::string name = d.GetString("name");
                    if (name.empty() || name == "auto" || IndexOf(list, name) >= 0) continue;
                    std::wstring desc = d.GetWString("description");
                    list.push_back({desc.empty() ? json::ToWide(name) : desc, name, {}});
                }
            }
        }
        const std::string cur = Cfg().GetString(item.key);
        if (!cur.empty() && IndexOf(list, cur) < 0) list.push_back({json::ToWide(cur), cur, {}});
        SetChoices(std::move(list), -1);
        SetSelected(IndexOf(Choices(), cur));
    }
};

class ChoiceSegmented : public Segmented, public Syncable {
public:
    explicit ChoiceSegmented(const Item& it) : Segmented(ChoicesOf(it), 0), item(it) {
        width = Length::Px(kControlW);
        height = Length::Px(36);
        Sync();
        onChange = [this](int i) {
            if (i >= 0 && i < static_cast<int>(item.choices.size()))
                Cfg().SetString(item.key, item.choices[static_cast<size_t>(i)].value);
        };
    }
    void Sync() override {
        const std::string v = Cfg().GetString(item.key);
        for (size_t i = 0; i < item.choices.size(); ++i)
            if (item.choices[i].value == v && static_cast<int>(i) != Selected()) SetSelected(static_cast<int>(i));
    }
    const Item& item;
};

// Accent tones spread over the 320-wide box (the design's nine hues became five greys).
class AccentSwatches : public Widget, public Syncable {
public:
    explicit AccentSwatches(const Item& it) : item(it) {
        interactive = true;
        focusable = true;
        width = Length::Px(kControlW);
        height = Length::Px(36);
        Sync();
    }
    void Sync() override {
        const std::string v = Cfg().GetString(item.key);
        for (size_t i = 0; i < item.choices.size(); ++i)
            if (item.choices[i].value == v) selected_ = static_cast<int>(i);
        Invalidate();
    }
    Rect Cell(int i) const {
        const float n = static_cast<float>(Count());
        const float w = (rect.w - 4 - 4 * (n - 1)) / n;
        return {rect.x + 2 + i * (w + 4), rect.y + 2, w, rect.h - 4};
    }
    int Count() const { return std::min(static_cast<int>(item.choices.size()), kAccentCount); }
    int At(float x) const {
        for (int i = 0; i < Count(); ++i)
            if (x >= Cell(i).x - 2 && x < Cell(i).Right() + 2) return i;
        return -1;
    }
    void Select(int i) {
        if (i < 0 || i >= Count()) return;
        selected_ = i;
        Cfg().SetString(item.key, item.choices[static_cast<size_t>(i)].value);
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        Select(At(e.x));
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const int h = At(e.x);
        if (h != hover_) {
            hover_ = h;
            tooltip = h >= 0 ? item.choices[static_cast<size_t>(h)].Label(Lang()) : std::wstring();
            Invalidate();
        }
        return false;
    }
    void OnMouseLeave() override {
        hover_ = -1;
        Invalidate();
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key == VK_LEFT) Select(std::max(0, selected_ - 1));
        else if (e.key == VK_RIGHT) Select(std::min(Count() - 1, selected_ + 1));
        else return false;
        return true;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, c.bgDeep, 6);
        PaintFocusRing(p, rect, 6);
        for (int i = 0; i < Count(); ++i) {
            const Rect cell = Cell(i);
            if (i == selected_)
                p.Fill(cell, c.accentDim, 4);
            else if (i == hover_ && IsHot())
                p.Fill(cell, c.hover, 4);
            p.Fill(cell.Center(20, 20), Theme::Get().AccentSwatch(i), 2);
        }
    }
    const Item& item;

private:
    int selected_ = 0;
    int hover_ = -1;
};

// --- composite controls ---------------------------------------------------------------------

std::unique_ptr<Widget> MakePathControl(const Item& item, PageHost& page) {
    auto box = std::make_unique<Widget>();
    box->dir = Dir::Row;
    box->gap = 10;
    box->align = Align::Center;
    box->width = Length::Px(kControlW);
    auto* field = box->Add<SettingField>(item);
    field->width = Length::Grow();
    auto* browse = box->Add<Button>(item.ActionLabel(Lang()).empty() ? std::wstring(Tr(L"Browse")) : item.ActionLabel(Lang()),
                                    ButtonKind::Secondary);
    browse->width = Length::Px(kSmallButtonW);
    Host* host = page.host;
    const Item* it = &item;
    browse->onClick = [host, it, field] {
        const std::wstring path = PickPath(host ? host->Hwnd() : nullptr, it->pathKind, Cfg().GetWString(it->key));
        if (path.empty()) return;
        Cfg().SetWString(it->key, path);
        field->Sync();
    };
    return box;
}

bool ParseHex(const std::string& s, uint32_t* rgb) {
    if (s.size() != 7 || s[0] != '#') return false;
    uint32_t v = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
        else return false;
    }
    *rgb = v;
    return true;
}

std::string HexOf(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
    return buf;
}

// Swatch grid for subtitle colors. These are values the user picks for the video overlay, not
// UI colors, so they are listed as data.
class ColorPalette : public Widget {
public:
    ColorPalette(const Item& it, std::function<void()> custom) : item(it), custom_(std::move(custom)) {
        interactive = true;
        focusable = true;
        width = Length::Px(kCols * 30 + 24);
        height = Length::Px(2 * 30 + 24 + 40);
    }
    static constexpr int kCols = 8;
    static constexpr uint32_t kColors[16] = {0xFFFFFF, 0xE6E6E6, 0xBFBFBF, 0x808080, 0x404040, 0x000000, 0xFFFF00, 0xFFD24D,
                                             0xFF9900, 0xFF3B30, 0xFF66CC, 0xB266FF, 0x3399FF, 0x00E5FF, 0x33CC66, 0x99FF33};
    Rect Cell(int i) const {
        return {rect.x + 12 + (i % kCols) * 30.0f, rect.y + 12 + (i / kCols) * 30.0f, 26, 26};
    }
    Rect CustomRect() const { return {rect.x + 12, rect.Bottom() - 44, rect.w - 24, 32}; }
    bool OnMouseDown(const MouseEvent& e) override {
        for (int i = 0; i < 16; ++i)
            if (Cell(i).Contains(e.x, e.y)) {
                Cfg().SetString(item.key, HexOf(kColors[i]));
                if (Host* h = GetHost()) h->ClosePopup(this);
                return true;
            }
        if (CustomRect().Contains(e.x, e.y)) {
            auto custom = custom_;
            if (Host* h = GetHost()) h->ClosePopup(this);
            if (custom) custom();
        }
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        int h = -1;
        for (int i = 0; i < 16; ++i)
            if (Cell(i).Contains(e.x, e.y)) h = i;
        if (CustomRect().Contains(e.x, e.y)) h = 16;
        if (h != hover_) {
            hover_ = h;
            Invalidate();
        }
        return false;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Shadow(rect, 8);
        p.Fill(rect, c.bgMenu, 8);
        p.Stroke(rect, c.line, 8);
        uint32_t cur = 0;
        const bool has = ParseHex(Cfg().GetString(item.key), &cur);
        for (int i = 0; i < 16; ++i) {
            const Rect r = Cell(i);
            p.Fill(r.Inset(2), Color::Hex(kColors[i]), 3);
            p.Stroke(r.Inset(2), c.textPrimary.Fade(0.2f), 3);
            if ((has && cur == kColors[i]) || hover_ == i) p.Stroke(r, hover_ == i ? c.textSecondary : c.accent, 4, 1.5f);
        }
        const Rect cr = CustomRect();
        p.Fill(cr, hover_ == 16 ? c.hover : c.bgPanel, 4);
        p.Text(Tr(L"Custom color…"), cr, TextStyle::Body, hover_ == 16 ? c.textPrimary : c.textSecondary, HAlign::Center);
    }
    const Item& item;

private:
    std::function<void()> custom_;
    int hover_ = -1;
};

class ColorField : public SettingField {
public:
    explicit ColorField(const Item& it) : SettingField(it) {}
    Rect Swatch() const { return {rect.Right() - 27, rect.Cy() - 11, 22, 22}; }
    void Paint(Painter& p) override {
        SettingField::Paint(p);
        uint32_t rgb = 0;
        if (ParseHex(Cfg().GetString(item.key), &rgb)) {
            const Rect s = Swatch();
            p.Fill(s, Color::Hex(rgb), 2);
            p.Stroke(s, P().textPrimary.Fade(0.21f), 2, 1.5f);
        }
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (Swatch().Inset(-3).Contains(e.x, e.y)) {
            OpenPalette();
            return true;
        }
        return SettingField::OnMouseDown(e);
    }
    void OpenPalette() {
        Host* host = GetHost();
        if (!host) return;
        const Item* it = &item;
        HWND owner = host->Hwnd();
        auto palette = std::make_unique<ColorPalette>(item, [it, owner] {
            static COLORREF custom[16] = {};
            uint32_t rgb = 0xFFFFFF;
            ParseHex(Cfg().GetString(it->key), &rgb);
            CHOOSECOLORW cc{};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = owner;
            cc.rgbResult = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc))
                Cfg().SetString(it->key, HexOf((static_cast<uint32_t>(GetRValue(cc.rgbResult)) << 16) |
                                               (static_cast<uint32_t>(GetGValue(cc.rgbResult)) << 8) |
                                               GetBValue(cc.rgbResult)));
        });
        PopupOptions o;
        o.anchor = rect;
        o.place = Place::BelowRight;
        host->ShowPopup(std::move(palette), o);
    }
    void OnClick() override {}
};

// "Startup volume": two stacked options, the second with its own number field
// (Settings-audio_3073-15879).
class StartupVolume : public Widget, public Syncable {
public:
    StartupVolume(const Item& mode, const Item* number) : mode_(mode) {
        dir = Dir::Column;
        gap = 2;
        pad = Insets::All(2);
        width = Length::Px(kControlW);
        for (size_t i = 0; i < mode.choices.size() && i < 2; ++i) {
            auto* opt = Add<Option>(this, static_cast<int>(i), mode.choices[i].Label(Lang()));
            if (i == 1 && number) {
                field_ = opt->Add<SettingField>(*number);
                field_->compact = true;
                field_->width = Length::Px(120);
                field_->height = Length::Px(32);
            }
        }
        Sync();
    }
    void Sync() override {
        const std::string v = Cfg().GetString(mode_.key);
        selected_ = !mode_.choices.empty() && mode_.choices.size() > 1 && v == mode_.choices[1].value ? 1 : 0;
        Invalidate();
    }
    void Paint(Painter& p) override { p.Fill(rect, P().bgDeep, 6); }
    void Select(int i) {
        if (i < 0 || i >= static_cast<int>(mode_.choices.size())) return;
        Cfg().SetString(mode_.key, mode_.choices[static_cast<size_t>(i)].value);
        Sync();
    }

private:
    class Option : public Widget {
    public:
        Option(StartupVolume* owner, int index, std::wstring text) : owner_(owner), index_(index), text_(std::move(text)) {
            interactive = true;
            focusable = true;
            width = Length::Grow();
            height = Length::Px(36);
            dir = Dir::Row;
            align = Align::Center;
            justify = Justify::End;
            pad = {12, 2, 2, 2};
        }
        void Paint(Painter& p) override {
            const Palette& c = P();
            const bool sel = owner_->selected_ == index_;
            p.Fill(rect, sel ? c.accentDim : (IsHot() ? Color::Mix(c.bgPanel, c.bgSelected, 0.6f) : c.bgPanel), 4);
            PaintFocusRing(p, rect, 4);
            float right = rect.Right() - 12;
            for (auto& ch : Children())
                if (ch->visible) right = std::min(right, ch->rect.x - 10);
            p.Text(text_, {rect.x + 12, rect.y, right - rect.x - 12, rect.h}, TextStyle::Body,
                   sel ? c.accent : IsHot() ? c.textSecondary : c.textTertiary);
        }
        void OnClick() override { owner_->Select(index_); }

    private:
        StartupVolume* owner_;
        int index_;
        std::wstring text_;
    };

    const Item& mode_;
    SettingField* field_ = nullptr;
    int selected_ = 0;
};

// Label + value above a slider, inside effect cards (components/Audio-*_Settings).
class ValueLabel : public Label, public Syncable {
public:
    explicit ValueLabel(const Item& it) : Label({}, TextStyle::Body, Tone::Tertiary), item(it) {
        halign = HAlign::Trailing;
        Sync();
    }
    void Sync() override { SetText(FormatValue(item, Cfg().GetDouble(item.key))); }
    const Item& item;
};

std::unique_ptr<Widget> MakeParamCard(const std::string& key, const std::vector<const Item*>& items, PageHost& page) {
    auto card = std::make_unique<Card>(key, page);
    Widget* body = card->Body();
    for (const Item* it : items) {
        auto* block = body->Add<Widget>();
        block->dir = Dir::Column;
        block->width = Length::Grow();
        block->gap = 6;
        auto* top = block->Add<Widget>();
        top->dir = Dir::Row;
        top->width = Length::Grow();
        top->align = Align::Center;
        auto* name = top->Add<Label>(it->Label(Lang()), TextStyle::Body, Tone::Tertiary);
        name->width = Length::Grow();
        if (!it->Description(Lang()).empty()) name->tooltip = it->Description(Lang());
        auto* value = top->Add<ValueLabel>(*it);
        auto* slider = block->Add<TickSlider>(*it, false);
        slider->width = Length::Grow();
        slider->onValue = [value, it](double v) { value->SetText(FormatValue(*it, v)); };
    }
    return card;
}

std::unique_ptr<Widget> MakeCropCard(const std::vector<const Item*>& items, PageHost& page) {
    auto card = std::make_unique<Card>("crop", page);
    Widget* body = card->Body();
    body->gap = 12;
    // Pairs: top/bottom, left/right, each with a link button that keeps both sides equal.
    for (size_t row = 0; row * 2 < items.size(); ++row) {
        auto* line = body->Add<Widget>();
        line->dir = Dir::Row;
        line->gap = 8;
        line->align = Align::Center;
        line->width = Length::Grow();
        std::vector<SettingField*> fields;
        for (size_t k = row * 2; k < std::min(items.size(), row * 2 + 2); ++k) {
            auto* box = line->Add<Panel>();
            box->fill = [] { return P().bgDeep; };
            box->radius = 6;
            box->dir = Dir::Row;
            box->align = Align::Center;
            box->width = Length::Grow();
            box->height = Length::Px(32);
            box->pad = {12, 0, 0, 0};
            auto* caption = box->Add<Label>(items[k]->Label(Lang()), TextStyle::Body, Tone::Tertiary);
            caption->width = Length::Px(58);
            auto* f = box->Add<SettingField>(*items[k]);
            f->compact = true;
            f->width = Length::Grow();
            f->height = Length::Px(32);
            fields.push_back(f);
        }
        if (fields.size() == 2) {
            auto* link = line->Add<IconButton>("link", Tr(L"Keep both sides equal"), 28, 20);
            const std::string stateKey = "crop.link" + std::to_string(row);
            UiState* state = page.state;
            link->active = state && state->Open(stateKey);
            SettingField* a = fields[0];
            SettingField* b = fields[1];
            link->onClick = [link, state, stateKey, a, b] {
                link->active = !link->active;
                if (state) state->open[stateKey] = link->active;
                if (link->active) Cfg().SetDouble(b->item.key, Cfg().GetDouble(a->item.key));
                link->Invalidate();
            };
            a->onCommitted = [link, a, b] {
                if (link->active) Cfg().SetDouble(b->item.key, Cfg().GetDouble(a->item.key));
            };
            b->onCommitted = [link, a, b] {
                if (link->active) Cfg().SetDouble(a->item.key, Cfg().GetDouble(b->item.key));
            };
        }
    }
    return card;
}

}  // namespace

// --- TickSlider -------------------------------------------------------------------------------

TickSlider::TickSlider(const Item& it, bool showScale) : item(it), showScale_(showScale) {
    interactive = true;
    focusable = true;
    width = Length::Px(kControlW);
    value_ = Cfg().GetDouble(item.key);
    enabled = Cfg().IsEnabled(item);
}

TickSlider::~TickSlider() {
    if (timer_)
        if (Host* h = GetHost()) h->CancelTimer(timer_);
}

Size TickSlider::MeasureContent(float availW, float) {
    return {std::min(availW, kControlW), showScale_ ? 47.0f : 24.0f};
}

Rect TickSlider::Track() const {
    return {rect.x, rect.y + 4, rect.w, 16};
}

double TickSlider::Snap(double v) const {
    v = std::clamp(v, item.min, item.max);
    if (item.step > 0) v = item.min + std::round((v - item.min) / item.step) * item.step;
    return std::clamp(v, item.min, item.max);
}

void TickSlider::Sync() {
    enabled = Cfg().IsEnabled(item);
    if (dragging_ || pending_) return;
    const double v = Cfg().GetDouble(item.key);
    if (v != value_) {
        value_ = v;
        Invalidate();
    }
}

void TickSlider::SetFromX(float x) {
    const Rect t = Track();
    const float left = t.x + 6, w = std::max(1.0f, t.w - 12);
    const double k = std::clamp((x - left) / w, 0.0f, 1.0f);
    Change(item.min + (item.max - item.min) * k, false);
}

void TickSlider::Change(double v, bool commitNow) {
    v = Snap(v);
    if (v != value_) {
        value_ = v;
        pending_ = true;
        if (onValue) onValue(v);
        Invalidate();
    }
    if (commitNow) {
        Commit();
        return;
    }
    // Coalesce drags: mpv filters (EQ, compressor) are rebuilt on every write.
    Host* h = GetHost();
    if (pending_ && h && !timer_)
        timer_ = h->StartTimer(60, [this] {
            timer_ = 0;
            Commit();
        });
}

void TickSlider::Commit() {
    if (timer_) {
        if (Host* h = GetHost()) h->CancelTimer(timer_);
        timer_ = 0;
    }
    if (!pending_) return;
    pending_ = false;
    Cfg().SetDouble(item.key, value_);
}

bool TickSlider::OnMouseDown(const MouseEvent& e) {
    dragging_ = true;
    SetFromX(e.x);
    return true;
}

bool TickSlider::OnMouseMove(const MouseEvent& e) {
    if (dragging_) SetFromX(e.x);
    return dragging_;
}

bool TickSlider::OnMouseUp(const MouseEvent&) {
    dragging_ = false;
    Commit();
    Invalidate();
    return true;
}

bool TickSlider::OnDoubleClick(const MouseEvent&) {
    if (item.def.IsNumber()) Change(item.def.AsNumber(), true);
    return true;
}

bool TickSlider::OnKeyDown(const KeyEvent& e) {
    const double step = item.step > 0 ? item.step : (item.max - item.min) / 100;
    double v = value_;
    switch (e.key) {
        case VK_LEFT:
        case VK_DOWN: v -= step; break;
        case VK_RIGHT:
        case VK_UP: v += step; break;
        case VK_PRIOR: v += step * 10; break;
        case VK_NEXT: v -= step * 10; break;
        case VK_HOME: v = item.min; break;
        case VK_END: v = item.max; break;
        default: return false;
    }
    Change(v, true);
    return true;
}

bool TickSlider::OnWheel(const WheelEvent& e) {
    if (!IsFocused()) return false;
    const double step = item.step > 0 ? item.step : (item.max - item.min) / 100;
    Change(value_ + step * e.delta, true);
    return true;
}

void TickSlider::Paint(Painter& p) {
    const Palette& c = P();
    const Rect t = Track();
    const bool on = IsEnabled();
    const double range = item.max - item.min;
    const float k = range > 0 ? static_cast<float>((value_ - item.min) / range) : 0.0f;
    const float left = t.x + 6, w = t.w - 12;
    const float cx = left + w * k;

    p.Fill(t, c.bgDeep, 6);
    if (cx - t.x - 2 > 0) p.Fill({t.x + 2, t.y + 2, cx - t.x - 2, 12}, c.accentDim, 4);
    const int steps = item.step > 0 ? static_cast<int>(std::lround(range / item.step)) : 0;
    if (steps >= 2 && steps <= 40)
        for (int i = 1; i < steps; ++i) {
            const float x = std::round(left + w * static_cast<float>(i) / static_cast<float>(steps));
            if (std::abs(x - cx) < 6) continue;
            p.Fill({x, t.Cy() - 3, 1, 6}, x < cx ? c.accentMid.Fade(0.35f) : c.lineStrong, 0);
        }
    const Rect thumb{cx - 4, rect.y, 8, 24};
    p.Fill(thumb, !on ? c.textDisabled : (dragging_ || IsHot()) ? c.accent : c.accentMid, 4);
    if (ShowFocusRing()) p.Stroke(thumb.Inset(-3), c.accentMid, 6, 1.5f);

    if (!showScale_) return;
    const Rect scale{rect.x, rect.y + 28, rect.w, 15};
    const Color sc = c.textTertiary;
    if (item.Unit(Lang()) == L"×" && steps >= 1 && steps <= 12) {
        for (int i = 0; i <= steps; ++i) {
            const std::wstring s = FormatValue(item, item.min + item.step * i, false);
            const float tw = p.Measure(s, TextStyle::Small).width;
            float x = left + w * static_cast<float>(i) / static_cast<float>(steps) - tw / 2;
            x = std::clamp(x, rect.x, rect.Right() - tw);
            p.Text(s, {x, scale.y, tw + 1, scale.h}, TextStyle::Small, sc, HAlign::Leading, VAlign::Center, 0);
        }
    } else {
        // Scale ends read "0 s ... 10 s", without the decimals the value itself shows.
        auto label = [&](double v) {
            if (std::abs(v - std::round(v)) > 1e-9) return FormatValue(item, v);
            Item whole = item;
            whole.step = 1;
            return FormatValue(whole, v);
        };
        p.Text(label(item.min), scale, TextStyle::Small, sc, HAlign::Leading);
        p.Text(label(Snap((item.min + item.max) / 2)), scale, TextStyle::Small, sc, HAlign::Center);
        p.Text(label(item.max), scale, TextStyle::Small, sc, HAlign::Trailing);
    }
}

void TickSlider::PaintOver(Painter& p) {
    if (!(dragging_ || (IsHot() && IsEnabled()))) return;
    const Rect t = Track();
    const double range = item.max - item.min;
    const float k = range > 0 ? static_cast<float>((value_ - item.min) / range) : 0.0f;
    const float cx = t.x + 6 + (t.w - 12) * k;
    const std::wstring s = FormatValue(item, value_);
    const float tw = p.Measure(s, TextStyle::Small).width + 14;
    Rect bubble{std::clamp(cx - tw / 2, rect.x - 8, rect.Right() + 8 - tw), rect.y - 26, tw, 22};
    p.Shadow(bubble, 4, 8, 3);
    p.Fill(bubble, P().bgMenu, 4);
    p.Stroke(bubble, P().line, 4);
    p.Text(s, bubble, TextStyle::Small, P().textPrimary, HAlign::Center);
}

// --- SettingField ------------------------------------------------------------------------------

SettingField::SettingField(const Item& it) : item(it) {
    width = Length::Px(kControlW);
    height = Length::Px(36);
    password = item.secret;
    placeholder = item.Placeholder(Lang());
    enabled = Cfg().IsEnabled(item);
    SetText(ValueText());
    onChange = [this](const std::wstring&) {
        edited_ = true;
        Schedule();
    };
    onSubmit = [this](const std::wstring&) { Commit(); };
}

SettingField::~SettingField() {
    if (timer_)
        if (Host* h = GetHost()) h->CancelTimer(timer_);
}

std::wstring SettingField::ValueText() const {
    if (item.type == ItemType::Number || item.type == ItemType::Slider)
        return FormatValue(item, Cfg().GetDouble(item.key), false);
    return Cfg().GetWString(item.key);
}

void SettingField::Sync() {
    enabled = Cfg().IsEnabled(item);
    if (IsFocused() && edited_) return;
    const std::wstring v = ValueText();
    if (v != Text()) SetText(v);
}

void SettingField::Schedule() {
    // Only free text applies while typing; numbers and colors would jump mid-edit.
    if (item.type != ItemType::Text && item.type != ItemType::Path) return;
    Host* h = GetHost();
    if (!h) return;
    if (timer_) h->CancelTimer(timer_);
    timer_ = h->StartTimer(700, [this] {
        timer_ = 0;
        Commit();
    });
}

void SettingField::Commit() {
    if (timer_) {
        if (Host* h = GetHost()) h->CancelTimer(timer_);
        timer_ = 0;
    }
    if (!edited_) {
        const std::wstring v = ValueText();
        if (v != Text()) SetText(v);
        return;
    }
    edited_ = false;
    std::wstring text = Text();
    switch (item.type) {
        case ItemType::Number:
        case ItemType::Slider: {
            for (wchar_t& ch : text)
                if (ch == L',') ch = L'.';
            wchar_t* end = nullptr;
            const double v = std::wcstod(text.c_str(), &end);
            const bool ok = end && end != text.c_str() && std::isfinite(v);
            if (ok) Cfg().SetDouble(item.key, v);
            break;
        }
        case ItemType::Color: {
            std::string s = json::ToUtf8(text);
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (!s.empty() && s[0] != '#') s = "#" + s;
            for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            uint32_t rgb = 0;
            if (ParseHex(s, &rgb)) Cfg().SetString(item.key, s);
            break;
        }
        default:
            if (text != Cfg().GetWString(item.key)) Cfg().SetWString(item.key, text);
            break;
    }
    // Show the stored (validated, clamped) value.
    const std::wstring v = ValueText();
    if (v != Text()) SetText(v);
    if (onCommitted) onCommitted();
}

void SettingField::OnFocusChanged(bool focused) {
    TextBox::OnFocusChanged(focused);
    if (!focused) Commit();
}

bool SettingField::OnKeyDown(const KeyEvent& e) {
    const bool number = item.type == ItemType::Number || item.type == ItemType::Slider;
    if (number && (e.key == VK_UP || e.key == VK_DOWN) && !e.mods.ctrl) {
        Commit();
        const double step = item.step > 0 ? item.step : 1;
        Cfg().SetDouble(item.key, Cfg().GetDouble(item.key) + (e.key == VK_UP ? step : -step));
        edited_ = false;
        SetText(ValueText());
        SelectAll();
        return true;
    }
    if (e.key == VK_ESCAPE && edited_) {
        edited_ = false;
        SetText(ValueText());
        return true;
    }
    return TextBox::OnKeyDown(e);
}

bool SettingField::OnWheel(const WheelEvent& e) {
    const bool number = item.type == ItemType::Number || item.type == ItemType::Slider;
    if (!number || !IsFocused()) return false;
    Commit();
    const double step = item.step > 0 ? item.step : 1;
    Cfg().SetDouble(item.key, Cfg().GetDouble(item.key) + step * e.delta);
    edited_ = false;
    SetText(ValueText());
    return true;
}

void SettingField::Paint(Painter& p) {
    background = P().bgDeep;
    TextBox::Paint(p);
    const bool number = item.type == ItemType::Number || item.type == ItemType::Slider;
    const std::wstring& unit = item.Unit(Lang());
    if (number && !unit.empty() && !Text().empty()) {
        const float tw = p.Measure(Text(), TextStyle::Body).width;
        const float x = rect.x + 12 + tw + (unit == L"%" ? 1.0f : 4.0f);
        if (x < rect.Right() - 8)
            p.Text(unit, {x, rect.y, rect.Right() - x - 8, rect.h}, TextStyle::Body,
                   IsEnabled() ? P().textTertiary : P().textDisabled);
    }
}

// --- Card ------------------------------------------------------------------------------------

namespace {

class CardHeader : public Widget {
public:
    explicit CardHeader(Card* card) : card_(card) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(48);
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const Color fg = IsHot() ? c.textSecondary : c.textTertiary;
        const wchar_t* text = card_->IsOpen() ? Tr(L"Collapse settings") : Tr(L"Expand settings");
        p.Text(text, {rect.x + 16, rect.y, rect.w - 56, rect.h}, TextStyle::Body, fg);
        p.Icon(card_->IsOpen() ? "chevron_down" : "chevron_right", {rect.Right() - 36, rect.Cy() - 10, 20, 20}, fg);
        PaintFocusRing(p, rect, 6);
    }
    void OnClick() override { card_->SetOpen(!card_->IsOpen()); }

private:
    Card* card_;
};

}  // namespace

Card::Card(std::string stateKey, PageHost& page) : key_(std::move(stateKey)), page_(page) {
    dir = Dir::Column;
    width = Length::Px(kControlW);
    header_ = Add<CardHeader>(this);
    body_ = Add<Widget>();
    body_->dir = Dir::Column;
    body_->width = Length::Grow();
    body_->gap = 14;
    body_->pad = {16, 0, 16, 18};
    open_ = page.state && page.state->Open(key_);
    body_->visible = open_;
}

void Card::SetOpen(bool open) {
    open_ = open;
    body_->visible = open;
    if (page_.state) page_.state->open[key_] = open;
    InvalidateLayout();
    if (page_.relayout) page_.relayout();
}

void Card::Paint(Painter& p) {
    p.Fill(rect, open_ ? P().bgPanel : P().bgDeep, 6);
}

// --- rows --------------------------------------------------------------------------------------

SettingRow::SettingRow(const Item& it, std::unique_ptr<Widget> control, float minHeight) : item(it) {
    Build(item.Label(Lang()), item.Description(Lang()), std::move(control), minHeight);
}

SettingRow::SettingRow(const Item& it, std::wstring label, std::wstring description, std::unique_ptr<Widget> control,
                       float minHeight)
    : item(it) {
    Build(std::move(label), std::move(description), std::move(control), minHeight);
}

void SettingRow::Build(std::wstring label, std::wstring description, std::unique_ptr<Widget> control, float minHeight) {
    dir = Dir::Row;
    align = Align::Center;
    width = Length::Grow();
    pad = {0, kRowPad, 0, kRowPad};
    gap = 16;
    minH = minHeight;
    id = item.key;
    Add<RowLabel>(std::move(label), std::move(description));
    if (control) AddChild(std::move(control));
    enabled = Cfg().IsEnabled(item);
    UpdateDisabledHint();
}

void SettingRow::Sync() {
    const bool on = Cfg().IsEnabled(item);
    if (on != enabled) {
        enabled = on;
        UpdateDisabledHint();
        Invalidate();
    }
}

// Disabled rows rely on the controls' own disabled colours (text.disabled); an extra
// row opacity made them unreadable. Hovering the label names the setting that unlocks it.
void SettingRow::UpdateDisabledHint() {
    if (Children().empty()) return;
    Widget* label = Children().front().get();
    label->tooltip.clear();
    label->interactive = false;
    if (enabled || !item.enabledWhen) return;
    if (const Item* parent = settings::FindItem(item.enabledWhen->key)) {
        label->tooltip = Fmt(Tr(L"Available when “{0}” is set"),
                             {Lang() == settings::Lang::Ru ? parent->labelRu : parent->label});
        label->interactive = true;
    }
}

void SettingRow::Paint(Painter&) {}

void SettingRow::PaintOver(Painter&) {}

Size PadLabel::MeasureContent(float availW, float availH) {
    const Size s = Label::MeasureContent(std::max(0.0f, availW - pad.H()), availH);
    return {s.w + pad.H(), s.h + pad.V()};
}

void PadLabel::Paint(Painter& p) {
    const Color c = color ? color() : (IsEnabled() ? ToneColor(tone) : P().textDisabled);
    p.Text(Text(), rect.Inset(pad), style, c, halign, valign, wrap ? gfx::kTextWrap : gfx::kTextEllipsis);
}

std::unique_ptr<Widget> MakeHeading(const std::wstring& text) {
    auto l = std::make_unique<PadLabel>(text, TextStyle::Heading, Tone::Primary);
    l->color = [] { return HeadingColor(); };
    l->pad = {0, 10, 0, 10};
    l->width = Length::Grow();
    return l;
}

std::unique_ptr<Widget> MakeItemRow(const Item& item, PageHost& page) {
    if (item.hidden || item.type == ItemType::KeyBindings) return nullptr;
    std::unique_ptr<Widget> control;
    switch (item.type) {
        case ItemType::Toggle: control = std::make_unique<BoolToggle>(item); break;
        case ItemType::Choice: control = std::make_unique<ChoiceDropdown>(item); break;
        case ItemType::Segmented: control = std::make_unique<ChoiceSegmented>(item); break;
        case ItemType::AccentPicker: control = std::make_unique<AccentSwatches>(item); break;
        case ItemType::Slider: control = std::make_unique<TickSlider>(item, !item.Unit(Lang()).empty()); break;
        case ItemType::Number:
        case ItemType::Text: control = std::make_unique<SettingField>(item); break;
        case ItemType::Path: control = MakePathControl(item, page); break;
        case ItemType::Color: control = std::make_unique<ColorField>(item); break;
        case ItemType::Button: {
            auto b = std::make_unique<Button>(item.ActionLabel(Lang()), ButtonKind::Secondary);
            b->width = Length::Px(IsWideAction(item.action) ? kControlW : kSmallButtonW);
            b->id = item.action;
            const Item* it = &item;
            PageHost* ph = &page;
            b->onClick = [ph, it] {
                if (ph->runAction) ph->runAction(it->action, *it);
            };
            control = std::move(b);
            break;
        }
        case ItemType::Info:
            if (item.action == "addons-list") return MakeAddonsList(item, page);
            break;
        case ItemType::KeyBindings: return nullptr;
    }
    return std::make_unique<SettingRow>(item, std::move(control), 0.0f);
}

std::unique_ptr<Widget> MakeGroupRow(const std::string& group, const std::vector<const Item*>& items, PageHost& page) {
    if (items.empty()) return nullptr;
    const Item& first = *items.front();
    if (group == "equalizer") return MakeEqualizer(items, page);
    if (group == "addonsToolbar") return MakeAddonsToolbar(items, page);
    if (group == "startupVolume") {
        const Item* mode = nullptr;
        const Item* number = nullptr;
        for (const Item* it : items) {
            if (it->hidden) continue;
            if (it->type == ItemType::Choice) mode = it;
            if (it->type == ItemType::Number) number = it;
        }
        if (!mode) return number ? MakeItemRow(*number, page) : nullptr;
        return std::make_unique<SettingRow>(*mode, std::make_unique<StartupVolume>(*mode, number), 0.0f);
    }
    if (group == "crop")
        return std::make_unique<SettingRow>(first, Tr(L"Crop margins"),
                                            Tr(L"Pixels to cut from each edge of the picture."), MakeCropCard(items, page),
                                            0.0f);
    struct CardInfo {
        const char* group;
        const wchar_t* label;
        const wchar_t* description;
    };
    static const CardInfo kCards[] = {
        {"compressor", L"Compressor", L"How night mode evens out loud and quiet passages."},
        {"spatializer", L"Room simulation", L"Size and character of the simulated room."},
        {"stereoWidener", L"Stereo widener", L"How far the stereo image is pushed beyond the speakers."},
    };
    for (const CardInfo& c : kCards)
        if (group == c.group)
            return std::make_unique<SettingRow>(first, Tr(c.label), Tr(c.description), MakeParamCard(group, items, page),
                                                0.0f);
    // Unknown group: plain rows.
    auto box = std::make_unique<Widget>();
    box->dir = Dir::Column;
    box->width = Length::Grow();
    for (const Item* it : items)
        if (auto row = MakeItemRow(*it, page)) box->AddChild(std::move(row));
    return box;
}

// --- dialogs ---------------------------------------------------------------------------------

std::wstring PickPath(HWND owner, settings::PathKind kind, const std::wstring& current) {
    std::wstring out;
    gfx::ComPtr<IFileDialog> dlg;
    const bool save = kind == settings::PathKind::SaveFile;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return out;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (kind == settings::PathKind::Folder) opts |= FOS_PICKFOLDERS;
    if (save) opts |= FOS_OVERWRITEPROMPT;
    dlg->SetOptions(opts);
    if (!current.empty()) {
        std::wstring folder = current;
        if (kind != settings::PathKind::Folder) {
            const size_t slash = folder.find_last_of(L"\\/");
            if (save && slash != std::wstring::npos) dlg->SetFileName(folder.c_str() + slash + 1);
            folder = slash == std::wstring::npos ? std::wstring() : folder.substr(0, slash);
        }
        gfx::ComPtr<IShellItem> item;
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
            dlg->SetFolder(item.Get());
    }
    if (FAILED(dlg->Show(owner))) return out;
    gfx::ComPtr<IShellItem> result;
    PWSTR path = nullptr;
    if (SUCCEEDED(dlg->GetResult(&result)) && SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        out = path;
        CoTaskMemFree(path);
    }
    return out;
}

void Confirm(Host* host, std::wstring title, std::wstring text, std::wstring okText, std::function<void()> ok) {
    if (!host) return;
    if (!Cfg().GetBool("general.showDialogs")) {
        ok();
        return;
    }
    auto d = std::make_unique<Dialog>(std::move(title), 460);
    auto* body = d->Body()->Add<Label>(std::move(text), TextStyle::Body, Tone::Secondary);
    body->wrap = true;
    body->width = Length::Grow();
    Dialog* raw = d.get();
    d->Footer()->Add<Spacer>();
    d->Footer()->Add<Button>(Tr(L"Cancel"), ButtonKind::Ghost)->onClick = [raw] { raw->Close(); };
    d->Footer()->Add<Button>(std::move(okText), ButtonKind::Primary)->onClick = [raw, ok = std::move(ok)] {
        auto run = ok;  // the dialog (and this lambda) is gone after Close()
        raw->Close();
        run();
    };
    ShowDialog(host, std::move(d));
}

}  // namespace app::sui
