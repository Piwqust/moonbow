// Settings > Controls: keyboard shortcut editor built on app/Actions.h. Click a binding (or focus
// it and press Enter) to record a new key: Esc cancels, Backspace clears. A key that is already
// used elsewhere shows a warning with "Reassign". A preset row on top switches the whole layout
// (YouTube keys) after a confirmation.
#include "app/Actions.h"
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "app/settings/ShortcutStore.h"

namespace app::sui {

using namespace ui;

namespace {

constexpr float kChipW = 200;

bool IsModifier(UINT vk) {
    switch (vk) {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_CAPITAL:
        case VK_NUMLOCK: return true;
        default: return false;
    }
}

std::wstring BindingText(const std::vector<KeyBinding>& keys) {
    std::wstring s;
    for (const KeyBinding& k : keys) s += (s.empty() ? L"" : L", ") + KeyText(k.first, k.second);
    return s;
}

class KeyChip : public Widget {
public:
    KeyChip() {
        interactive = true;
        focusable = true;
        width = Length::Px(kChipW);
        height = Length::Px(32);
    }
    std::function<void(const KeyBinding&)> onRecord;
    std::function<void()> onClear;

    void SetKeys(std::vector<KeyBinding> keys) {
        keys_ = std::move(keys);
        tooltip = BindingText(keys_);
        Invalidate();
    }
    bool Recording() const { return recording_; }
    void SetWarning(bool w) {
        warning_ = w;
        Invalidate();
    }
    void StartRecording() {
        recording_ = true;
        if (Host* h = GetHost()) h->SetFocus(this);
        Invalidate();
    }
    void StopRecording() {
        recording_ = false;
        Invalidate();
    }
    void OnClick() override {
        if (recording_) StopRecording();
        else StartRecording();
    }
    void OnFocusChanged(bool focused) override {
        if (!focused) StopRecording();
        Invalidate();
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (!recording_) {
            if ((e.key == VK_BACK || e.key == VK_DELETE) && !keys_.empty()) {
                if (onClear) onClear();
                return true;
            }
            return false;
        }
        if (IsModifier(e.key)) return true;  // wait for the key itself
        if (e.key == VK_TAB && !e.mods.ctrl && !e.mods.alt) {
            StopRecording();
            return false;  // let focus move on
        }
        const bool plain = !e.mods.ctrl && !e.mods.alt && !e.mods.shift;
        if (e.key == VK_ESCAPE && plain) {
            StopRecording();
            return true;
        }
        if (e.key == VK_BACK && plain) {
            StopRecording();
            if (onClear) onClear();
            return true;
        }
        StopRecording();
        if (onRecord) onRecord({e.key, e.mods});
        return true;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = IsHot();
        Color bg = recording_ ? c.accentDim : hot ? Color::Mix(c.bgDeep, c.bgPanel, 0.7f) : c.bgDeep;
        p.Fill(rect, bg, 4);
        if (warning_) p.Stroke(rect, c.danger, 4, 1.5f);
        else if (recording_) p.Stroke(rect, c.accentMid, 4);
        PaintFocusRing(p, rect, 4);
        const Rect text{rect.x + 10, rect.y, rect.w - 44, rect.h};
        if (recording_)
            p.Text(Tr(L"Press a key…"), text, TextStyle::Body, c.accent, HAlign::Center);
        else if (keys_.empty())
            p.Text(L"—", text, TextStyle::Body, c.textDisabled, HAlign::Center);
        else
            p.Text(BindingText(keys_), text, TextStyle::Mono, hot ? c.textPrimary : c.textSecondary, HAlign::Center);
        p.Icon("edit", {rect.Right() - 30, rect.Cy() - 10, 20, 20}, hot || recording_ ? c.textSecondary : c.textTertiary);
    }

private:
    std::vector<KeyBinding> keys_;
    bool recording_ = false;
    bool warning_ = false;
};

class ShortcutEditor;

class ShortcutRow : public Widget {
public:
    ShortcutRow(ShortcutEditor* editor, Action a);
    void Refresh();
    void ShowConflict(int slot, const KeyBinding& key, Action other);
    void HideConflict();
    Action action;

private:
    void Record(int slot, const KeyBinding& key);
    void Apply(int slot, std::optional<KeyBinding> key);
    ShortcutEditor* editor_;
    KeyChip* chips_[2] = {};
    IconButton* reset_ = nullptr;
    Widget* warning_ = nullptr;
    Label* warningText_ = nullptr;
    int pendingSlot_ = -1;
    KeyBinding pendingKey_{};
    Action pendingOther_ = Action::None;
};

class ShortcutEditor : public Widget, public Syncable {
public:
    ShortcutEditor() {
        dir = Dir::Column;
        width = Length::Grow();
        pad = {0, 4, 0, 8};
        id = "shortcut-editor";
        auto* presets = Add<Widget>();
        presets->dir = Dir::Row;
        presets->align = Align::Center;
        presets->width = Length::Grow();
        presets->gap = 12;
        presets->pad = {0, 6, 0, 10};
        auto* hint = presets->Add<Label>(Tr(L"Key layout of another player"), TextStyle::Body, Tone::Secondary);
        hint->width = Length::Grow();
        auto* youtube = presets->Add<Button>(Tr(L"YouTube keys"), ButtonKind::Secondary);
        youtube->id = "preset-youtube";
        youtube->width = Length::Px(320);  // like the buttons of the rows above
        youtube->tooltip = Tr(L"K, J, L, 0–9, Shift+, and Shift+., C");
        youtube->onClick = [this] {
            Host* host = GetHost();
            Confirm(host, Tr(L"Use YouTube keys?"),
                    Tr(L"K or Space plays and pauses, J and L seek 10 s, 0–9 jump to 0–90%, Shift+, and Shift+. change the speed, C turns subtitles on and off. Your other changes to shortcuts are reset."),
                    Tr(L"Use YouTube keys"), [this, host] {
                        ApplyKeyPreset("youtube");
                        HideConflicts(nullptr);
                        Sync();
                        ShowToast(host, Tr(L"YouTube keys are on"), "hotkeys");
                    });
        };
        // Column titles.
        auto* head = Add<Widget>();
        head->dir = Dir::Row;
        head->width = Length::Grow();
        head->gap = 12;
        head->pad = {0, 6, 40, 0};
        auto* a = head->Add<Label>(Tr(L"Action"), TextStyle::Small, Tone::Tertiary);
        a->width = Length::Grow();
        for (const wchar_t* t : {L"Shortcut", L"Alternative"}) {
            auto* l = head->Add<Label>(Tr(t), TextStyle::Small, Tone::Tertiary);
            l->width = Length::Px(kChipW);
            l->halign = HAlign::Center;
        }
        std::wstring group;
        for (const ActionInfo& info : AllActions()) {
            if (info.action == Action::None) continue;
            if (group != info.group) {
                group = info.group;
                auto* g = Add<PadLabel>(Tr(info.group), TextStyle::Large, Tone::Tertiary);
                g->pad = {0, 18, 0, 6};
            }
            rows_.push_back(Add<ShortcutRow>(this, info.action));
        }
    }
    void Sync() override {
        for (ShortcutRow* r : rows_) r->Refresh();
    }
    void HideConflicts(ShortcutRow* except) {
        for (ShortcutRow* r : rows_)
            if (r != except) r->HideConflict();
    }

private:
    std::vector<ShortcutRow*> rows_;
};

ShortcutRow::ShortcutRow(ShortcutEditor* editor, Action a) : action(a), editor_(editor) {
    dir = Dir::Column;
    width = Length::Grow();
    auto* line = Add<Widget>();
    line->dir = Dir::Row;
    line->align = Align::Center;
    line->width = Length::Grow();
    line->gap = 12;
    line->pad = {0, 4, 0, 4};
    auto* label = line->Add<Label>(Tr(Info(a).label), TextStyle::Body, Tone::Secondary);
    label->width = Length::Grow();
    for (int slot = 0; slot < 2; ++slot) {
        chips_[slot] = line->Add<KeyChip>();
        chips_[slot]->onRecord = [this, slot](const KeyBinding& k) { Record(slot, k); };
        chips_[slot]->onClear = [this, slot] { Apply(slot, std::nullopt); };
    }
    // Fixed slot, so the columns stay aligned whether or not the button shows.
    auto* resetSlot = line->Add<Widget>();
    resetSlot->dir = Dir::Stack;
    resetSlot->width = Length::Px(28);
    resetSlot->height = Length::Px(28);
    reset_ = resetSlot->Add<IconButton>("refresh", Tr(L"Restore the default shortcut"), 28, 20);
    reset_->onClick = [this] {
        HideConflict();
        ResetBindings(action);
        Refresh();
    };

    warning_ = Add<Widget>();
    warning_->dir = Dir::Row;
    warning_->align = Align::Center;
    warning_->gap = 8;
    warning_->width = Length::Grow();
    warning_->pad = {0, 2, 36, 8};
    warning_->visible = false;
    auto* icon = warning_->Add<IconView>("info", 18, Tone::Secondary);
    icon->color = [] { return P().danger; };
    warningText_ = warning_->Add<Label>(L"", TextStyle::Small, Tone::Secondary);
    warningText_->width = Length::Grow();
    auto* reassign = warning_->Add<Button>(Tr(L"Reassign"), ButtonKind::Secondary);
    reassign->onClick = [this] {
        if (pendingSlot_ < 0) return;
        const int slot = pendingSlot_;
        const KeyBinding key = pendingKey_;
        // Take the key away from the other action, then assign it here.
        std::vector<KeyBinding> rest;
        for (const KeyBinding& k : ActiveBindings(pendingOther_))
            if (!SameBinding(k, key)) rest.push_back(k);
        AssignBindings(pendingOther_, rest);
        HideConflict();
        Apply(slot, key);
        editor_->Sync();
    };
    auto* cancel = warning_->Add<Button>(Tr(L"Cancel"), ButtonKind::Ghost);
    cancel->onClick = [this] { HideConflict(); };
    Refresh();
}

void ShortcutRow::Refresh() {
    const std::vector<KeyBinding> keys = ActiveBindings(action);
    chips_[0]->SetKeys(keys.empty() ? std::vector<KeyBinding>{} : std::vector<KeyBinding>{keys.front()});
    chips_[1]->SetKeys(keys.size() > 1 ? std::vector<KeyBinding>(keys.begin() + 1, keys.end()) : std::vector<KeyBinding>{});
    const bool custom = !IsDefaultBinding(action);
    if (reset_->visible != custom) {
        reset_->visible = custom;
        InvalidateLayout();
    }
    reset_->enabled = custom;
}

void ShortcutRow::Record(int slot, const KeyBinding& key) {
    editor_->HideConflicts(this);
    for (const ActionInfo& info : AllActions()) {
        if (info.action == Action::None || info.action == action) continue;
        for (const KeyBinding& k : ActiveBindings(info.action))
            if (SameBinding(k, key)) return ShowConflict(slot, key, info.action);
    }
    Apply(slot, key);
}

void ShortcutRow::Apply(int slot, std::optional<KeyBinding> key) {
    std::vector<KeyBinding> keys = ActiveBindings(action);
    std::vector<KeyBinding> first, rest;
    if (!keys.empty()) first.push_back(keys.front());
    if (keys.size() > 1) rest.assign(keys.begin() + 1, keys.end());
    if (slot == 0) first = key ? std::vector<KeyBinding>{*key} : std::vector<KeyBinding>{};
    else rest = key ? std::vector<KeyBinding>{*key} : std::vector<KeyBinding>{};
    first.insert(first.end(), rest.begin(), rest.end());
    AssignBindings(action, first);
    Refresh();
}

void ShortcutRow::ShowConflict(int slot, const KeyBinding& key, Action other) {
    pendingSlot_ = slot;
    pendingKey_ = key;
    pendingOther_ = other;
    warningText_->SetText(Fmt(Tr(L"{0} is already used by “{1}”."), {KeyText(key.first, key.second), Tr(Info(other).label)}));
    warning_->visible = true;
    chips_[slot]->SetWarning(true);
    InvalidateLayout();
}

void ShortcutRow::HideConflict() {
    if (!warning_->visible) return;
    warning_->visible = false;
    pendingSlot_ = -1;
    for (KeyChip* c : chips_) c->SetWarning(false);
    InvalidateLayout();
}

}  // namespace

std::unique_ptr<Widget> MakeShortcutEditor(PageHost&) {
    return std::make_unique<ShortcutEditor>();
}

}  // namespace app::sui
