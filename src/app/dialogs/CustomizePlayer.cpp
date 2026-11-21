// "Customize Player" (design/ref/Player-customize_*.png): a palette of every control
// bar item and an editable copy of the control bar. Items are dragged from the
// palette into the bar, along the bar to reorder, and out of it to remove them.
//
// With media loaded the dialog opens over the player screen and the editable bar sits
// exactly over the real one (dashed outlines on every item, as in the design); otherwise
// a preview bar is shown inside the dialog. Save applies the layout via SetControlLayout.
#include "app/Dialogs.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "app/player/ControlBar.h"
#include "app/player/PlayerView.h"
#include "gfx/Text.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace app {

using namespace ui;
using namespace dlg;

namespace {

constexpr ControlId kSeek = ControlId::Count;  // marker for the seek bar in a sequence
// Cells fit two-line labels at word breaks ("Вперёд на / 10 с"); 24 DIP between them.
constexpr float kCellW = 104, kCellGap = 16, kRowPitch = 120;
constexpr float kBarH = 56;

bool Repeatable(ControlId id) {
    return id == ControlId::Spacer || id == ControlId::Divider;
}

std::vector<ControlId> ToSeq(const ControlLayout& l) {
    std::vector<ControlId> s = l.left;
    s.push_back(kSeek);
    s.insert(s.end(), l.right.begin(), l.right.end());
    return s;
}

ControlLayout FromSeq(const std::vector<ControlId>& s) {
    ControlLayout l;
    bool right = false;
    for (ControlId id : s) {
        if (id == kSeek) right = true;
        else (right ? l.right : l.left).push_back(id);
    }
    return l;
}

const wchar_t* LabelOf(ControlId id) {
    if (id == kSeek) return L"Seek bar";
    return ControlInfoFor(id).label;
}

// The palette's shorter caption where the full one needs three lines in a cell.
const wchar_t* PaletteLabelOf(ControlId id) {
    if (id == ControlId::Speed) return L"Speed";
    return LabelOf(id);
}

void NoFocus(Widget* w) {
    w->focusable = false;
    for (auto& c : w->Children()) NoFocus(c.get());
}

// Icon of a palette entry inside its dashed box (Spacer and Divider have none).
void PaintItemIcon(Painter& p, ControlId id, const Rect& box, Color c) {
    if (id == ControlId::Divider) {
        p.Fill({std::round(box.Cx()), box.y + 6, 1, box.h - 12}, c);
        return;
    }
    if (id == ControlId::Spacer) return;
    const char* icon = ControlInfoFor(id).icon;
    if (icon && *icon) p.Icon(icon, box.Center(24, 24), c);
}

class Editor;

// --- palette ---------------------------------------------------------------------------------

class PaletteCell : public Widget {
public:
    PaletteCell(Editor* e, ControlId id) : editor_(e), id_(id) {
        interactive = true;
        focusable = true;
        tooltip = Tr(LabelOf(id));
    }
    ControlId Id() const { return id_; }
    void Paint(Painter& p) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyDown(const KeyEvent& e) override;

private:
    Editor* editor_;
    ControlId id_;
};

class PaletteGrid : public Widget {
public:
    PaletteGrid() { width = Length::Grow(); }
    int Columns(float w) const { return std::max(1, static_cast<int>((w + kCellGap) / (kCellW + kCellGap))); }
    Size MeasureContent(float availW, float) override {
        const int n = static_cast<int>(Children().size());
        const int rows = (n + Columns(availW) - 1) / Columns(availW);
        return {availW, rows > 0 ? rows * kRowPitch - 38 : 0};
    }
    void OnLayout() override {
        const int cols = Columns(rect.w);
        int i = 0;
        for (auto& c : Children()) {
            const int r = i / cols, col = i % cols;
            c->Arrange({rect.x + col * (kCellW + kCellGap), rect.y + r * kRowPitch, kCellW, 82});
            ++i;
        }
    }
};

// --- bar ---------------------------------------------------------------------------------------

// One item of the editable bar: the real control (inert) with a dashed outline.
class BarSlot : public Widget {
public:
    BarSlot(Editor* e, ControlId id, int index) : editor_(e), id_(id), index_(index) {
        interactive = true;
        focusable = id != kSeek;
        dir = Dir::Row;
        align = Align::Center;
        pad = {3, 3, 3, 3};
        if (id == kSeek) {
            width = Length::Grow();
            auto seek = std::make_unique<SeekBar>();
            seek->pad = {6, 0, 6, 0};
            AddChild(std::move(seek));
        } else {
            AddChild(CreateControl(id));
        }
        NoFocus(Children().front().get());
        focusable = id != kSeek;
        tooltip = id == kSeek ? Tr(L"The seek bar always stays") : Tr(LabelOf(id));
    }
    ControlId Id() const { return id_; }
    int Index() const { return index_; }
    Widget* HitTest(float x, float y) override { return visible && rect.Contains(x, y) ? this : nullptr; }
    void PaintChildren(Painter& p) override;
    void PaintOver(Painter& p) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyDown(const KeyEvent& e) override;

private:
    Editor* editor_;
    ControlId id_;
    int index_;
};

class BarEditor : public Widget {
public:
    explicit BarEditor(bool rounded) : rounded_(rounded) {
        dir = Dir::Row;
        align = Align::Center;
        pad = {20, 0, 20, 0};
        gap = 6;
        width = Length::Grow();
        height = Length::Px(kBarH);
        interactive = true;
    }
    void Paint(Painter& p) override {
        p.Fill(rect, P().bgChrome, rounded_ ? 6.0f : 0.0f);
        if (rounded_) p.Stroke(rect, P().line, 6);
    }
    void PaintOver(Painter& p) override;
    Editor* editor = nullptr;

private:
    bool rounded_;
};

// --- the dialog ----------------------------------------------------------------------------------

class Editor : public Widget {
public:
    Editor(MainWindow* w, bool overlay) : window_(w), overlay_(overlay) {
        dir = Dir::None;
        interactive = true;
        seq_ = ToSeq(CurrentControlLayout());
        original_ = seq_;
        Build();
    }

    Size MeasureContent(float availW, float availH) override { return {availW, availH}; }

    void OnLayout() override {
        const Size client = GetHost() ? GetHost()->ClientSize() : Size{rect.w, rect.h};
        Rect area{0, 0, client.w, client.h};
        if (overlay_) {
            barRect_ = RealBarRect();
            if (!barRect_.Empty()) {
                bar_->Arrange(barRect_);
                area.h = barRect_.y;
            }
        }
        // Narrower than the design on small windows (the palette grid reflows to the width).
        const float fw = std::floor(std::min(area.w - 32, 1048.0f));
        if (frame_->width.value != fw) frame_->width = Length::Px(fw);
        const Size s = frame_->Measure(fw, area.h - 24);
        frame_->Arrange({std::round(area.x + (area.w - s.w) / 2), std::round(std::max(12.0f, area.y + (area.h - s.h) / 2)),
                         s.w, s.h});
    }

    Widget* HitTest(float x, float y) override {
        if (!visible) return nullptr;
        if (overlay_ && bar_)
            if (Widget* hit = bar_->HitTest(x, y)) return hit;
        if (Widget* hit = frame_->HitTest(x, y)) return hit;
        return this;  // modal: swallow everything else
    }

    void Paint(Painter& p) override {
        const Size client = GetHost() ? GetHost()->ClientSize() : Size{rect.w, rect.h};
        const Color scrim = P().scrim;
        if (overlay_ && !barRect_.Empty()) {
            // Dim everything but the control bar being edited.
            p.Fill({0, 0, client.w, barRect_.y}, scrim);
            p.Fill({0, barRect_.Bottom(), client.w, client.h - barRect_.Bottom()}, scrim);
        } else {
            p.Fill({0, 0, client.w, client.h}, scrim);
        }
    }

    void PaintChildren(Painter& p) override {
        frame_->PaintTree(p);
        if (overlay_ && bar_) bar_->PaintTree(p);
    }

    void PaintOver(Painter& p) override {
        if (!drag_.active) return;
        const Palette& c = P();
        const Rect ghost{drag_.pos.x - 22, drag_.pos.y - 20, 44, 40};
        p.Shadow(ghost, 6, 12, 6);
        p.Fill(ghost, c.bgMenu, 6);
        p.Stroke(ghost, c.accentMid, 6);
        PaintItemIcon(p, drag_.id, ghost.Center(30, 32), c.textPrimary);
        if (drag_.id == ControlId::Spacer) DashedRect(p, ghost.Center(24, 30), c.textSecondary);
        if (drag_.fromBar && !OverBar(drag_.pos) && !ControlInfoFor(drag_.id).removable) {
            p.Text(Tr(L"Play/Pause can't be removed"), {ghost.x - 80, ghost.Bottom() + 6, 204, 20}, TextStyle::Small,
                   c.textSecondary, HAlign::Center);
        }
    }

    // --- drag & drop --------------------------------------------------------------------------
    void Press(ControlId cid, bool fromBar, int index, Point at) {
        drag_ = {};
        drag_.pending = true;
        drag_.id = cid;
        drag_.fromBar = fromBar;
        drag_.index = index;
        drag_.start = drag_.pos = at;
    }
    void Move(Point at) {
        if (!drag_.pending && !drag_.active) return;
        drag_.pos = at;
        if (drag_.pending && std::hypot(at.x - drag_.start.x, at.y - drag_.start.y) > 4) {
            drag_.pending = false;
            drag_.active = true;
        }
        if (drag_.active) {
            insertAt_ = OverBar(at) ? InsertIndex(at.x) : -1;
            Invalidate();
        }
    }
    void Release(Point at) {
        if (!drag_.active) {
            drag_ = {};
            return;
        }
        drag_.pos = at;
        const bool over = OverBar(at);
        std::vector<ControlId> next = seq_;
        int focusIndex = -1;
        if (over) {
            int to = InsertIndex(at.x);
            if (drag_.fromBar) {
                next.erase(next.begin() + drag_.index);
                if (to > drag_.index) --to;
            }
            to = std::clamp(to, 0, static_cast<int>(next.size()));
            next.insert(next.begin() + to, drag_.id);
            focusIndex = to;
        } else if (drag_.fromBar && ControlInfoFor(drag_.id).removable) {
            next.erase(next.begin() + drag_.index);
        }
        drag_ = {};
        insertAt_ = -1;
        Commit(std::move(next), focusIndex);
    }
    bool Dragging(ControlId cid, bool fromBar, int index) const {
        return drag_.active && drag_.fromBar == fromBar && (fromBar ? drag_.index == index : drag_.id == cid);
    }
    bool DraggingFromBar() const { return drag_.active && drag_.fromBar; }
    bool Dragging() const { return drag_.active; }
    int InsertAt() const { return insertAt_; }
    const std::vector<BarSlot*>& Slots() const { return slots_; }

    // --- keyboard -------------------------------------------------------------------------------
    void AddFromPalette(ControlId cid) {
        std::vector<ControlId> next = seq_;
        const auto seek = std::find(next.begin(), next.end(), kSeek);
        const int at = static_cast<int>(seek - next.begin());
        next.insert(seek, cid);
        Commit(std::move(next), at);
    }
    void RemoveAt(int index) {
        if (!ControlInfoFor(seq_[static_cast<size_t>(index)]).removable) return;
        std::vector<ControlId> next = seq_;
        next.erase(next.begin() + index);
        Commit(std::move(next), std::min(index, static_cast<int>(next.size()) - 1));
    }
    void MoveBy(int index, int delta) {
        const int to = index + delta;
        if (to < 0 || to >= static_cast<int>(seq_.size())) return;
        std::vector<ControlId> next = seq_;
        std::swap(next[static_cast<size_t>(index)], next[static_cast<size_t>(to)]);
        Commit(std::move(next), to);
    }

private:
    struct Drag {
        bool pending = false, active = false, fromBar = false;
        ControlId id = ControlId::Spacer;
        int index = -1;
        Point start, pos;
    };

    Rect RealBarRect() const {
        PlayerView* pv = window_->playerView();
        if (!pv || !TheApp().PlayerShown() || window_->IsFullscreen() || window_->UiHidden()) return {};
        ControlBar* bar = pv->Bar();
        return bar && bar->visible ? bar->rect : Rect{};
    }

    bool OverBar(Point at) const {
        if (!bar_) return false;
        const Rect r = bar_->rect;
        return at.x >= r.x - 8 && at.x <= r.Right() + 8 && at.y >= r.y - 24 && at.y <= r.Bottom() + 24;
    }

    // Insertion index in seq_ (before removing a dragged bar item).
    int InsertIndex(float x) const {
        for (const BarSlot* s : slots_) {
            if (drag_.fromBar && s->Index() == drag_.index) continue;
            if (x < s->rect.Cx()) return s->Index();
        }
        return static_cast<int>(seq_.size());
    }

    void Commit(std::vector<ControlId> next, int focusIndex) {
        seq_ = std::move(next);
        // Rebuilding destroys the widget that is handling the current mouse/key event.
        if (Host* h = GetHost())
            h->Post([this, focusIndex, h] {
                if (!h->IsPopupOpen(this)) return;
                RebuildBar();
                RebuildPalette();
                UpdateSave();
                if (focusIndex >= 0 && focusIndex < static_cast<int>(slots_.size()) && slots_[focusIndex]->focusable)
                    h->SetFocus(slots_[static_cast<size_t>(focusIndex)], h->FocusVisible());
                h->RequestLayout();
            });
    }

    void Build() {
        auto frame = std::make_unique<Frame>(Tr(L"Customize Player"), 1048.0f);
        frame_ = frame.get();

        Widget* body = frame_->Body();
        body->pad = Insets::All(34);
        body->gap = 24;
        // In the body, not the one-line toolbar: the Russian hint needs two lines.
        auto* desc = body->Add<Label>(
            std::wstring(overlay_
                             ? Tr(L"Drag items into the control bar below to add them, along it to reorder them, and back "
                                  L"here to remove them from the player.")
                             : Tr(L"Drag items into the preview below to add them, along it to reorder them, and back here "
                                  L"to remove them from the player.")) +
                L" " + Tr(L"With the keyboard: Enter adds an item, Delete removes it, the arrow keys move it."),
            TextStyle::Body, Tone::Tertiary);
        desc->width = Length::Grow();
        desc->wrap = true;
        scroll_ = body->Add<ScrollView>();
        scroll_->width = Length::Grow();
        grid_ = scroll_->SetContent<PaletteGrid>();

        if (overlay_) {
            auto bar = std::make_unique<BarEditor>(false);
            bar_ = bar.get();
            AddChild(std::move(bar));
        } else {
            auto* label = body->Add<Label>(Tr(L"Control bar preview"), TextStyle::Body, Tone::Tertiary);
            label->pad = {0, 0, 0, 0};
            bar_ = body->Add<BarEditor>(true);
            body->gap = 16;
        }
        bar_->editor = this;

        Widget* footer = frame_->Footer();
        footer->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [this] { frame_->Close(); };
        auto* reset = footer->Add<DButton>(Tr(L"Reset to default"), BtnStyle::Neutral);
        reset->minWidth = 140;
        reset->onClick = [this] { Commit(ToSeq(ControlLayout::Default()), -1); };
        footer->Add<Spacer>();
        save_ = footer->Add<DButton>(Tr(L"Save"), BtnStyle::Accent);
        save_->onClick = [this] { Save(); };
        frame_->onSubmit = [this] {
            if (save_->IsEnabled()) Save();
        };
        AddChild(std::move(frame));

        RebuildBar();
        RebuildPalette();
        UpdateSave();
    }

    void Save() {
        SetControlLayout(FromSeq(seq_));
        MainWindow* w = window_;
        frame_->Close();
        ShowToast(w, Tr(L"Control bar saved"), "check_active");
    }

    void UpdateSave() {
        save_->enabled = seq_ != original_;
        save_->Invalidate();
    }

    void RebuildBar() {
        bar_->ClearChildren();
        slots_.clear();
        for (size_t i = 0; i < seq_.size(); ++i) slots_.push_back(bar_->Add<BarSlot>(this, seq_[i], static_cast<int>(i)));
        bar_->InvalidateLayout();
    }

    void RebuildPalette() {
        grid_->ClearChildren();
        for (const ControlInfo& c : AllControls()) {
            if (c.id == ControlId::PlayPause) continue;
            if (!Repeatable(c.id) && std::find(seq_.begin(), seq_.end(), c.id) != seq_.end()) continue;
            grid_->Add<PaletteCell>(this, c.id);
        }
        // Four rows visible, the rest scrolls.
        const Host* h = GetHost() ? GetHost() : window_;
        const float avail = h ? h->ClientSize().h : 780;
        const float reserved = (overlay_ ? kBarH : kBarH + 40) + 50 + 64 + 72 + 68 + 40;
        const float rows = std::clamp(std::floor((avail - reserved + 38) / kRowPitch), 1.0f, 4.0f);
        scroll_->height = Length::Px(rows * kRowPitch - 38 + 4);
        scroll_->InvalidateLayout();
    }

    MainWindow* window_;
    bool overlay_;
    std::vector<ControlId> seq_, original_;
    Frame* frame_ = nullptr;
    ScrollView* scroll_ = nullptr;
    PaletteGrid* grid_ = nullptr;
    BarEditor* bar_ = nullptr;
    DButton* save_ = nullptr;
    std::vector<BarSlot*> slots_;
    Rect barRect_;
    Drag drag_;
    int insertAt_ = -1;
};

// --- PaletteCell ---------------------------------------------------------------------------------

// Keeps a short last word ("10 с") on the line before it.
std::wstring GlueLastWord(std::wstring s) {
    const size_t sp = s.rfind(L' ');
    if (sp != std::wstring::npos && s.size() - sp - 1 <= 2) s[sp] = L' ';
    return s;
}

bool IsVowel(wchar_t ch) {
    return std::wcschr(L"аеёиоуыэюяАЕЁИОУЫЭЮЯaeiouyAEIOUY", ch) != nullptr;
}

// Breaks words wider than `w` with a hyphen after a vowel ("воспроизве-дения"), so every
// label keeps the same text size instead of shrinking to fit.
std::wstring Hyphenate(const std::wstring& label, TextStyle style, float w) {
    std::wstring out;
    size_t a = 0;
    while (a < label.size()) {
        size_t b = a;
        while (b < label.size() && label[b] != L' ' && label[b] != L'/') ++b;
        std::wstring word = label.substr(a, b - a);
        while (word.size() > 5 && gfx::Text::Get().Measure(word, style).width > w) {
            size_t cut = 0;
            for (size_t k = 2; k + 2 <= word.size(); ++k)
                if (IsVowel(word[k - 1]) && !std::wcschr(L"ьъйЬЪЙ", word[k]) &&
                    gfx::Text::Get().Measure(word.substr(0, k) + L"-", style).width <= w)
                    cut = k;
            if (!cut) break;
            out += word.substr(0, cut) + L"-\n";
            word = word.substr(cut);
        }
        out += word;
        if (b < label.size()) out += label[b];
        a = b + 1;
    }
    return out;
}

void PaletteCell::Paint(Painter& p) {
    const Palette& c = P();
    const bool dragging = editor_->Dragging(id_, false, -1);
    const bool hot = (IsHot() && !editor_->Dragging()) || ShowFocusRing();
    if (dragging) p.PushOpacity(0.35f);
    const Rect bg = rect.Inset(Insets{-8, -8, -8, -6});
    if (hot) p.Fill(bg, c.hover, 6);
    PaintFocusRing(p, bg, 6);
    const Rect box = id_ == ControlId::Spacer || id_ == ControlId::Divider ? Rect{rect.Cx() - 12, rect.y, 24, 32}
                                                                         : Rect{rect.Cx() - 15, rect.y, 30, 32};
    DashedRect(p, box, hot ? c.textSecondary : c.textTertiary);
    PaintItemIcon(p, id_, box, hot ? c.textPrimary : c.textSecondary);
    // The label stays inside the cell, two lines at most.
    const float w = rect.w;
    const std::wstring label = Hyphenate(GlueLastWord(Tr(PaletteLabelOf(id_))), TextStyle::Body, w);
    p.Text(label, {rect.Cx() - w / 2, rect.y + 40, w, 42}, TextStyle::Body, hot ? c.textPrimary : c.textSecondary,
           HAlign::Center, VAlign::Top, gfx::kTextWrap | gfx::kTextEllipsis);
    if (dragging) p.PopOpacity();
}

bool PaletteCell::OnMouseDown(const MouseEvent& e) {
    editor_->Press(id_, false, -1, {e.x, e.y});
    return true;
}
bool PaletteCell::OnMouseMove(const MouseEvent& e) {
    editor_->Move({e.x, e.y});
    return true;
}
bool PaletteCell::OnMouseUp(const MouseEvent& e) {
    editor_->Release({e.x, e.y});
    return true;
}
bool PaletteCell::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_RETURN || e.key == VK_SPACE) {
        editor_->AddFromPalette(id_);
        return true;
    }
    return false;
}

// --- BarSlot ---------------------------------------------------------------------------------------

void BarSlot::PaintChildren(Painter& p) {
    const bool dragging = editor_->Dragging(id_, true, index_);
    if (dragging) p.PushOpacity(0.3f);
    Widget::PaintChildren(p);
    if (dragging) p.PopOpacity();
}

void BarSlot::PaintOver(Painter& p) {
    const Palette& c = P();
    const bool hot = (IsHot() && !editor_->Dragging() && id_ != kSeek) || ShowFocusRing();
    if (hot && id_ != kSeek) p.Fill(rect, c.hover, 2);
    DashedRect(p, rect, hot ? c.textSecondary : c.textTertiary);
    PaintFocusRing(p, rect, 2);
}

bool BarSlot::OnMouseDown(const MouseEvent& e) {
    if (id_ != kSeek) editor_->Press(id_, true, index_, {e.x, e.y});
    return true;
}
bool BarSlot::OnMouseMove(const MouseEvent& e) {
    editor_->Move({e.x, e.y});
    return true;
}
bool BarSlot::OnMouseUp(const MouseEvent& e) {
    editor_->Release({e.x, e.y});
    return true;
}
bool BarSlot::OnKeyDown(const KeyEvent& e) {
    switch (e.key) {
        case VK_DELETE:
        case VK_BACK: editor_->RemoveAt(index_); return true;
        case VK_LEFT: editor_->MoveBy(index_, -1); return true;
        case VK_RIGHT: editor_->MoveBy(index_, 1); return true;
        default: return false;
    }
}

// --- BarEditor -------------------------------------------------------------------------------------

void BarEditor::PaintOver(Painter& p) {
    if (!editor || !editor->Dragging()) return;
    const Palette& c = P();
    const int at = editor->InsertAt();
    if (at < 0) {
        if (editor->DraggingFromBar()) DashedRect(p, rect.Inset(2), c.textTertiary, 6, 6);
        return;
    }
    DashedRect(p, rect.Inset(2), c.accentMid, 6, 6);
    // Insertion marker between the neighbours.
    const auto& slots = editor->Slots();
    float x = rect.Right() - pad.r;
    for (const BarSlot* s : slots)
        if (s->Index() == at) {
            x = s->rect.x - gap / 2;
            break;
        }
    if (at >= static_cast<int>(slots.size()) && !slots.empty()) x = slots.back()->rect.Right() + gap / 2;
    p.Fill({std::round(x) - 1, rect.Cy() - 16, 2, 32}, c.accent, 1);
}

}  // namespace

void ShowCustomizePlayerDialog() {
    MainWindow* w = TheApp().main();
    if (!w) return;
    // Edit the real bar when there is one to look at.
    const bool overlay = TheApp().HasMedia() && !w->IsFullscreen() && !w->UiHidden();
    if (overlay && !TheApp().PlayerShown()) TheApp().ShowPlayer(true);
    auto editor = std::make_unique<Editor>(w, overlay);
    PopupOptions o;
    o.place = Place::At;
    o.anchor = {0, 0, 0, 0};
    o.modal = true;
    o.scrim = false;  // the editor dims everything but the bar itself
    o.dismissOnOutside = false;
    o.dismissOnEscape = true;
    CloseTransientPopups(w);
    w->ShowPopup(std::move(editor), o);
    w->RequestLayout();
}

}  // namespace app
