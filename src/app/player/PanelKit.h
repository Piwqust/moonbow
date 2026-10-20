#pragma once
// Building blocks of the player popovers (design/ref/components/Controls_491-1780.png):
// the tab header, the popover frame, setting rows and the "range" slider, value boxes and
// "Expand Settings" boxes used by the Playback Settings panel.
#include "Player.h"
#include "app/Signal.h"
#include "ui/Controls.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app::panel {

inline constexpr float kPanelW = 400;
inline constexpr float kHeaderH = 50;
inline constexpr float kRadius = ui::kRadiusPopup;
inline constexpr float kGap = 20;  // between the bar and the popover, and to the window edge

// Text color of list rows (#969696 in the design: a touch brighter than textSecondary).
ui::Color RowText();
// Hairline between sections (#222222).
ui::Color SectionLine();

// --- tab header ------------------------------------------------------------------------

struct Tab {
    std::wstring label;
    std::string icon;        // normal icon
    std::string iconActive;  // selected (two-tone) icon; falls back to `icon`
};

// Tabs on bgDeep with an accent underline under the selected one. Left/Right switch tabs.
class TabBar : public ui::Widget {
public:
    TabBar(std::vector<Tab> tabs, int selected);
    int Selected() const { return selected_; }
    void SetSelected(int i, bool animate = true);
    std::function<void(int)> onSelect;

    void OnLayout() override;
    void Paint(ui::Painter& p) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    void OnMouseLeave() override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    void Describe(ui::A11y& a) override;

private:
    ui::Rect TabRect(int i) const;
    float ContentWidth(const Tab& t) const;
    int IndexAt(float x) const;
    void Select(int i);
    std::vector<Tab> tabs_;
    std::vector<float> widths_;
    int selected_;
    int hover_ = -1;
    ui::AnimFloat lineX_, lineW_;
    bool placed_ = false;
    bool showIcons_ = true;
};

// --- popover frame ---------------------------------------------------------------------

// The popover: optional tab header, a body that is rebuilt when the tab changes, and an
// optional footer. It positions itself on every layout through `place` (so it follows the
// control bar when the window resizes).
class Popover : public ui::Widget {
public:
    Popover();
    ~Popover() override;

    // Returns the popover rect in window DIPs.
    std::function<ui::Rect()> place;
    // Builds the body for a tab (or tab 0 when there are no tabs).
    std::function<std::unique_ptr<ui::Widget>(int tab)> build;
    // Called after the tab changed (by click or code).
    std::function<void(int tab)> onTab;
    // Called with the popover height before the footer is measured, so it can shrink in short windows.
    std::function<void(float height)> fitFooter;
    // Fill of the frame (defaults to opaque bgBase).
    std::function<ui::Color()> fill;
    float radius = kRadius;

    void SetTabs(std::vector<Tab> tabs, int selected);
    void SetTitle(std::wstring title);  // plain centered header instead of tabs (mini player queue)
    void SetFooter(std::unique_ptr<ui::Widget> footer);
    int CurrentTab() const { return tab_; }
    void ShowTab(int tab);
    void Rebuild() { ShowTab(tab_); }

    ui::Size MeasureContent(float availW, float availH) override;
    void OnLayout() override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;
    bool Backdrop(ui::Color& out) const override;
    bool OnKeyDown(const ui::KeyEvent& e) override;

private:
    TabBar* tabs_ = nullptr;
    std::wstring title_;
    ui::Widget* body_ = nullptr;
    ui::Widget* footer_ = nullptr;
    int tab_ = 0;
};

// --- setting rows ---------------------------------------------------------------------

// A tab body that keeps its controls in sync with the player, the live values and the
// config: every OnSync() callback runs when any of them changes.
class SyncPage : public ui::Widget {
public:
    SyncPage();
    ~SyncPage() override;
    void OnSync(std::function<void()> fn) { syncers_.push_back(std::move(fn)); }
    void SyncAll();

private:
    std::vector<std::function<void()>> syncers_;
    ScopedConnection playerConn_, liveConn_, queueConn_;
    uint64_t configToken_ = 0;
    bool posted_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Column with the design's 20 DIP side padding.
ui::Widget* AddSection(ui::Widget* page);
// Full-width hairline between sections.
void AddSectionLine(ui::Widget* page);
// "Defaults", "Filters"...: Heading in the 39 DIP title row.
void AddSectionTitle(ui::Widget* page, const std::wstring& text);
// Small caption above a control ("Flip", "Essential").
void AddCaption(ui::Widget* section, const std::wstring& text, float h = 37);
// Label on the left, control on the right.
ui::Widget* AddRow(ui::Widget* section, const std::wstring& label, std::unique_ptr<ui::Widget> control, float h = 48);
// Label on the left, value text on the right (above a slider): returns the value label.
ui::Label* AddValueRow(ui::Widget* section, const std::wstring& label, float h = 37);

// The design's range slider: 16-21 DIP dark track, accentDim fill from `origin`, 8×24 thumb.
class Range : public ui::Widget {
public:
    Range(float min, float max, float value, float step = 0);
    float Value() const { return value_; }
    void SetValue(float v);  // ignored while the user drags
    float min, max, step;
    float origin = NAN;  // fill starts here (NaN = min)
    float trackH = 16;
    std::vector<float> ticks;
    std::vector<std::pair<float, std::wstring>> labels;  // drawn under the track
    std::function<std::wstring(float)> format;  // the value as screen readers say it ("1.25×")
    std::function<void(float)> onChange;  // while dragging
    std::function<void(float)> onCommit;  // release / key
    bool Dragging() const { return dragging_; }

    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    void Describe(ui::A11y& a) override;

private:
    ui::Rect TrackRect() const;
    float Snap(float v) const;
    void SetFromX(float x);
    float value_;
    bool dragging_ = false;
};

// TextBox that stops its caret-blink timer when destroyed (ui::TextBox leaves it running
// when it dies while focused, e.g. when the popover closes during editing).
class EditBox : public ui::TextBox {
public:
    using ui::TextBox::TextBox;
    ~EditBox() override { ui::TextBox::OnFocusChanged(false); }
};

// Text field showing a formatted number; typing a value and pressing Enter commits it.
class ValueBox : public EditBox {
public:
    ValueBox();
    std::function<std::wstring()> text;           // current value as shown
    std::function<void(double)> onValue;          // parsed number on Enter / focus loss
    std::function<void(const std::wstring&)> onText;  // raw text on Enter / focus loss (instead of onValue)
    void Refresh();  // re-reads `text` unless focused
    void Paint(ui::Painter& p) override;
    void OnFocusChanged(bool focused) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;

private:
    void Commit();
    bool selectOnUp_ = false;
    std::wstring shown_;  // what Refresh/Commit last displayed; unchanged text isn't committed
};

// "Expand Settings ›" box that unfolds into "Collapse Settings ⌄" + content. With a
// `title` the header names what it unfolds instead ("Equalizer bands ›").
class Expander : public ui::Widget {
public:
    explicit Expander(std::string key);  // key remembers the state while the app runs
    ui::Widget* Body() const { return body_; }
    bool Expanded() const { return expanded_; }
    void SetExpanded(bool on);
    std::wstring title;
    void OnLayout() override;
    void Paint(ui::Painter& p) override;

private:
    class Header;
    std::string key_;
    Header* header_ = nullptr;
    ui::Widget* body_ = nullptr;
    bool expanded_ = false;
    bool reveal_ = false;  // scroll the body into view on the next layout
};

// Full-width button with an optional icon ("Show Preferences", "Open Advanced Settings").
class FlatButton : public ui::Widget {
public:
    FlatButton(std::wstring text, std::string icon = {}, bool filled = true);
    std::wstring text;
    std::string icon;
    bool filled;
    void Paint(ui::Painter& p) override;
};

// Decimal comma in Russian ("0,5"), point in English.
std::wstring LocalDecimal(std::wstring s);
// Formats "1.250 s", "-4.000 s".
std::wstring FormatSeconds(double s, int decimals = 3);
// Formats numbers without trailing zeros: 1, 1.25, 0.5.
std::wstring FormatNumber(double v, int maxDecimals = 2);

// A track as people read it, the same way the OSD names it: "Русский", "Commentary (English)",
// the sidecar file's name, or "Track 2" (`ordinal` among the tracks of its type).
std::wstring TrackName(const Track& t, int ordinal);
// Its format: "SubRip", "AAC · Stereo", "E-AC-3 · 5.1"; empty when mpv doesn't say.
// Without the codec only an audio track's channels are left ("Stereo", "5.1").
std::wstring TrackDetail(const Track& t, bool codec = true);

}  // namespace app::panel
