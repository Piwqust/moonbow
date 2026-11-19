#pragma once
// Internal header of the settings window (src/app/settings). Rows are generated from the schema
// (core/SettingsSchema.h); every widget that shows a setting implements Syncable and re-reads
// Config in Sync(), which the window calls after any setting changed.
#include "core/SettingsSchema.h"
#include "ui/Controls.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Config;

namespace app::sui {

// Layout constants from design/ref/Settings-*.png (DIP).
inline constexpr float kControlW = 320;  // right column: dropdowns, fields, sliders
// A narrow window shrinks the control column down to this before it cuts a row label.
inline constexpr float kControlMinW = 200;
inline constexpr float kRowPad = 5;
inline constexpr float kSmallButtonW = 100;

settings::Lang Lang();
Config& Cfg();
// Settings shows only the basic items, with icons and summaries (interface.settingsMode).
bool BasicMode();
ui::Color HeadingColor();  // section headings (between primary and secondary text)
ui::Color DisabledTextColor();  // labels of disabled rows: text.disabled raised to at least 3:1

// Implemented by widgets that display a setting.
class Syncable {
public:
    virtual ~Syncable() = default;
    virtual void Sync() = 0;
};
// Calls Sync() on every Syncable in the subtree.
void SyncTree(ui::Widget* w);

// Per-window UI state that survives page rebuilds (expanders, cards).
struct UiState {
    std::map<std::string, bool> open;
    bool Open(const std::string& key, bool fallback = false) const {
        auto it = open.find(key);
        return it == open.end() ? fallback : it->second;
    }
};

// What a row may ask the window to do.
struct PageHost {
    ui::Host* host = nullptr;
    UiState* state = nullptr;
    std::function<void()> relayout;       // content changed size
    std::function<void(const std::string& action, const settings::Item&)> runAction;  // own buttons
};

// "1.5 s", "-11.0 dB", "4.0 : 1", "250 %"; decimals follow the item's step, Russian uses a comma.
std::wstring FormatValue(const settings::Item& item, double v, bool withUnit = true);
int Decimals(const settings::Item& item);

// --- building blocks (SettingsRows.cpp) ---------------------------------------------------

// A labelled row: label (+ (i) tooltip) on the left, `control` on the right. Dims and disables
// itself when the item's enabledWhen condition is false.
class SettingRow : public ui::Widget, public Syncable {
public:
    SettingRow(const settings::Item& item, std::unique_ptr<ui::Widget> control, float minHeight);
    // For composite rows: the condition comes from `item`, the label from `label`.
    SettingRow(const settings::Item& item, std::wstring label, std::wstring description,
               std::unique_ptr<ui::Widget> control, float minHeight);
    void Sync() override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;
    ui::Size MeasureContent(float availW, float availH) override;
    void OnLayout() override;
    const settings::Item& item;

private:
    void Build(std::wstring label, std::wstring description, std::unique_ptr<ui::Widget> control, float minHeight);
    void UpdateDisabledHint();
    void ApplyEnabled();
    bool on_ = true;
    // Narrows a control-column control (designed 320 wide) so the label keeps its text.
    void FitControl(float innerW);
    ui::Widget* control_ = nullptr;
    float controlW_ = 0;  // the control's design width; 0 = it never shrinks
};

// ui::Label that honours `pad` (the stock one ignores it).
class PadLabel : public ui::Label {
public:
    using ui::Label::Label;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;
};

// Section heading ("Appearance").
std::unique_ptr<ui::Widget> MakeHeading(const std::wstring& text);

// One row (or a full-width widget) for a schema item; nullptr for hidden items.
std::unique_ptr<ui::Widget> MakeItemRow(const settings::Item& item, PageHost& page);

// A row for a group of items drawn as one widget ("equalizer", "compressor", "crop"...).
std::unique_ptr<ui::Widget> MakeGroupRow(const std::string& group, const std::vector<const settings::Item*>& items,
                                         PageHost& page);

// Horizontal slider from the design: 16-high track, 8×24 thumb. Rows pair it with an editable
// value box (MakeItemRow), effect cards with a value label above it.
class TickSlider : public ui::Widget, public Syncable {
public:
    explicit TickSlider(const settings::Item& item);
    ~TickSlider() override;
    void Sync() override;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    bool OnDoubleClick(const ui::MouseEvent& e) override;
    void Describe(ui::A11y& a) override;
    std::function<void(double)> onValue;  // live, while dragging

    const settings::Item& item;

private:
    ui::Rect Track() const;
    double Snap(double v) const;
    void SetFromX(float x);
    void Change(double v, bool commitNow);
    void Commit();
    double value_ = 0;
    bool dragging_ = false;
    bool pending_ = false;
    UINT_PTR timer_ = 0;
};

// Single-line field bound to a Text / Path / Number / Color setting. Commits on Enter, on blur and
// shortly after typing stops. Number fields show the unit after the value and step with ↑/↓; a
// number with a placeholder ("Auto") shows it for the minimum, and an empty field stores the minimum.
class SettingField : public ui::TextBox, public Syncable {
public:
    explicit SettingField(const settings::Item& item);
    ~SettingField() override;
    void Sync() override;
    void Paint(ui::Painter& p) override;
    void OnFocusChanged(bool focused) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    void Commit();
    bool compact = false;  // borderless box inside cards
    std::function<void()> onCommitted;
    const settings::Item& item;

private:
    void Schedule();
    std::wstring ValueText() const;
    UINT_PTR timer_ = 0;
    bool edited_ = false;  // the user typed since the text was last set from Config
};

// Collapsible card used by the audio effect groups and the crop margins
// (components/Audio-*_Settings, Video-Crop).
class Card : public ui::Widget {
public:
    Card(std::string stateKey, PageHost& page);
    ui::Widget* Body() const { return body_; }
    void Paint(ui::Painter& p) override;
    void SetOpen(bool open);
    bool IsOpen() const { return open_; }

private:
    std::string key_;
    PageHost& page_;
    ui::Widget* body_ = nullptr;
    ui::Widget* header_ = nullptr;
    bool open_ = false;
};

// --- composite widgets ---------------------------------------------------------------------

std::unique_ptr<ui::Widget> MakeEqualizer(const std::vector<const settings::Item*>& items, PageHost& page);
std::unique_ptr<ui::Widget> MakeAddonsList(const settings::Item& item, PageHost& page);
// Settings > Library: the library roots with Remove / Show in File Explorer (SettingsLibrary.cpp).
std::unique_ptr<ui::Widget> MakeLibraryFolders(const settings::Item& item, PageHost& page);
// The subtitle example on a frame with the ready-made styles under it (SettingsSubtitlePreview.cpp).
std::unique_ptr<ui::Widget> MakeSubtitlePreview(const settings::Item& item);
// Shader files in <data>\shaders with a toggle each (video.shaders), and "Open folder".
std::unique_ptr<ui::Widget> MakeShaderList(const settings::Item& item, PageHost& page);
std::unique_ptr<ui::Widget> MakeAddonsToolbar(const std::vector<const settings::Item*>& items, PageHost& page);
void RescanAddons();
// Settings > Interface: the skin list with previews and the contrast check (SettingsSkins.cpp).
std::unique_ptr<ui::Widget> MakeSkinPicker(const std::vector<const settings::Item*>& items, PageHost& page);
// Settings > Controls: grouped list of every action with its key bindings.
std::unique_ptr<ui::Widget> MakeShortcutEditor(PageHost& page);

// Folder / file pickers (IFileOpenDialog / IFileSaveDialog). Empty when cancelled.
std::wstring PickPath(HWND owner, settings::PathKind kind, const std::wstring& current);

// Confirmation dialog in the settings window. Always shown: what it guards cannot be undone.
void Confirm(ui::Host* host, std::wstring title, std::wstring text, std::wstring okText, std::function<void()> ok);

}  // namespace app::sui
