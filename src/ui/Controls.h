#pragma once
#include "ui/Anim.h"
#include "ui/Host.h"
#include "ui/Widget.h"

#include <functional>
#include <string>
#include <vector>

// Common controls styled after design/DESIGN.md. Every control reads colors
// from the theme at paint time, so theme changes apply instantly.
namespace ui {

enum class Tone : uint8_t { Primary, Secondary, Tertiary, Disabled, Accent };
Color ToneColor(Tone t);

// --- static content --------------------------------------------------------------------

class Label : public Widget {
public:
    Label() = default;
    Label(std::wstring text, TextStyle style = TextStyle::Body, Tone tone = Tone::Secondary);
    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }

    TextStyle style = TextStyle::Body;
    Tone tone = Tone::Secondary;
    HAlign halign = HAlign::Leading;
    VAlign valign = VAlign::Center;
    bool wrap = false;
    int maxLines = 0;  // with wrap; 0 = unlimited
    std::function<Color()> color;  // overrides tone when set

    Size MeasureContent(float availW, float availH) override;
    void Paint(Painter& p) override;

private:
    std::wstring text_;
};

class IconView : public Widget {
public:
    explicit IconView(std::string icon = {}, int size = 24, Tone tone = Tone::Secondary);
    std::string icon;
    float size;
    Tone tone;
    std::function<Color()> color;
    Size MeasureContent(float, float) override { return {size, size}; }
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
};

class Divider : public Widget {
public:
    explicit Divider(bool vertical = false);
    bool vertical;
    void Paint(Painter& p) override;
};

class Spacer : public Widget {
public:
    Spacer() { width = Length::Grow(); }
};

// --- buttons ---------------------------------------------------------------------------

enum class ButtonKind : uint8_t { Primary, Secondary, Ghost, Outline };

class Button : public Widget {
public:
    Button(std::wstring text, ButtonKind kind = ButtonKind::Secondary, std::string icon = {});
    void SetText(std::wstring text);
    ButtonKind kind;
    std::string icon;
    TextStyle style = TextStyle::BodyStrong;
    Size MeasureContent(float availW, float availH) override;
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;

private:
    std::wstring text_;
};

// Square icon button with hover background. `active` paints the icon in the
// accent color (toggled state, e.g. shuffle on).
class IconButton : public Widget {
public:
    explicit IconButton(std::string icon, std::wstring tooltipText = {}, int box = 32, int iconSize = 24);
    std::string icon;
    float iconSize;
    bool active = false;
    bool round = false;
    Tone tone = Tone::Secondary;
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
};

// Single-line text link / text-only button.
class LinkButton : public Widget {
public:
    explicit LinkButton(std::wstring text, TextStyle style = TextStyle::Body);
    std::wstring text;
    TextStyle style;
    std::string trailingIcon;  // e.g. "chevron_right" after section titles
    Size MeasureContent(float availW, float availH) override;
    void Paint(Painter& p) override;
};

// --- inputs -----------------------------------------------------------------------------

// ON/OFF switch from the design: square knob in a dark box.
class Toggle : public Widget {
public:
    explicit Toggle(bool on = false);
    bool Value() const { return on_; }
    void SetValue(bool on, bool animate = true);
    std::function<void(bool)> onChange;
    Size MeasureContent(float, float) override { return {66, 30}; }
    void Paint(Painter& p) override;
    void OnClick() override;
    void DebugInfo(std::string& out) const override;

private:
    bool on_;
    AnimFloat knob_;
};

// The captions inside every Toggle track ("ON"/"OFF"); the app sets translated ones.
void SetToggleCaptions(std::wstring on, std::wstring off);
// Shown by a menu whose typed filter matches nothing.
void SetMenuNoMatchCaption(std::wstring text);

class Checkbox : public Widget {
public:
    Checkbox(std::wstring text, bool checked = false);
    bool Value() const { return checked_; }
    void SetValue(bool v) { checked_ = v; Invalidate(); }
    std::function<void(bool)> onChange;
    Size MeasureContent(float availW, float availH) override;
    void Paint(Painter& p) override;
    void OnClick() override;
    void DebugInfo(std::string& out) const override;

private:
    std::wstring text_;
    bool checked_;
};

struct Choice {
    std::wstring label;
    std::string value;
    std::string icon;
};

// Segmented control ("Dark | Light | System").
class Segmented : public Widget {
public:
    explicit Segmented(std::vector<Choice> choices, int selected = 0);
    int Selected() const { return selected_; }
    void SetSelected(int i);
    std::function<void(int)> onChange;
    float segmentWidth = 104;
    Size MeasureContent(float, float) override;
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnKeyDown(const KeyEvent& e) override;
    void OnMouseLeave() override { hoverIndex_ = -1; Invalidate(); }
    bool OnMouseMove(const MouseEvent& e) override;

private:
    int IndexAt(float x) const;
    std::vector<Choice> choices_;
    int selected_;
    int hoverIndex_ = -1;
};

// Field showing the current choice; opens a menu with all choices.
class Dropdown : public Widget {
public:
    explicit Dropdown(std::vector<Choice> choices, int selected = 0);
    int Selected() const { return selected_; }
    void SetSelected(int i);
    void SetChoices(std::vector<Choice> choices, int selected);
    const std::vector<Choice>& Choices() const { return choices_; }
    std::function<void(int)> onChange;
    std::wstring placeholder;
    Size MeasureContent(float availW, float availH) override;
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
    void OnClick() override;
    bool OnKeyDown(const KeyEvent& e) override;

private:
    std::vector<Choice> choices_;
    int selected_;
    Widget* menu_ = nullptr;
};

// Horizontal slider. Value in [min, max], optionally snapped to `step`.
class Slider : public Widget {
public:
    Slider(float min, float max, float value, float step = 0);
    float Value() const { return value_; }
    void SetValue(float v);
    float min, max, step;
    bool showFill = true;
    bool centerFill = false;  // fill from the middle (balance, EQ gain)
    std::function<void(float)> onChange;      // while dragging
    std::function<void(float)> onCommit;      // on release / key
    std::function<std::wstring(float)> format; // tooltip value text
    Size MeasureContent(float availW, float) override { return {std::min(availW, 200.0f), 24}; }
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyDown(const KeyEvent& e) override;
    bool OnWheel(const WheelEvent& e) override;

private:
    void SetFromX(float x);
    float value_;
    bool dragging_ = false;
};

// Single-line text input with caret, selection, clipboard and IME positioning.
class TextBox : public Widget {
public:
    explicit TextBox(std::wstring placeholder = {}, std::string leadingIcon = {});
    ~TextBox() override;
    const std::wstring& Text() const { return text_; }
    void SetText(std::wstring text, bool notify = false);
    void SelectAll();
    std::wstring placeholder;
    std::string leadingIcon;
    bool password = false;
    bool readOnly = false;
    // Fill: an explicit colour, else the palette's bgRaised (raised = true) or bgDeep,
    // read at paint time so theme switches apply.
    Color background = Color::Clear();
    bool raised = false;
    std::function<void(const std::wstring&)> onChange;
    std::function<void(const std::wstring&)> onSubmit;
    std::function<void()> onEscape;

    Size MeasureContent(float availW, float) override { return {std::min(availW, 240.0f), 34}; }
    void Paint(Painter& p) override;
    void DebugInfo(std::string& out) const override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnDoubleClick(const MouseEvent& e) override;
    bool OnKeyDown(const KeyEvent& e) override;
    bool OnChar(wchar_t ch) override;
    void OnFocusChanged(bool focused) override;
    bool WantsTextInput() const override { return true; }
    Rect CaretRect() const override;

private:
    Rect TextRect() const;
    size_t IndexAt(float x) const;
    float XOf(size_t index) const;
    void Insert(std::wstring_view s);
    void DeleteSelection();
    void MoveCaret(size_t pos, bool extend);
    void Changed();
    void ResetBlink();
    std::wstring Shown() const;

    std::wstring text_;
    size_t caret_ = 0, anchor_ = 0;
    float scroll_ = 0;
    bool selecting_ = false;
    bool caretOn_ = true;
    UINT_PTR blinkTimer_ = 0;
    Host* blinkHost_ = nullptr;  // the host that owns blinkTimer_
};

// Row of accent swatches (monochrome tones).
class AccentPicker : public Widget {
public:
    explicit AccentPicker(int selected = 0);
    int Selected() const { return selected_; }
    void SetSelected(int i) { selected_ = i; Invalidate(); }
    std::function<void(int)> onChange;
    Size MeasureContent(float, float) override;
    void Paint(Painter& p) override;
    bool OnMouseDown(const MouseEvent& e) override;

private:
    int selected_;
};

// --- containers -------------------------------------------------------------------------

// Vertical scroller for one content child (any layout). Smooth wheel scrolling,
// auto-hiding scrollbar, drag on the thumb.
class ScrollView : public Widget {
public:
    ScrollView();
    template <class T, class... A>
    T* SetContent(A&&... args) {
        ClearChildren();
        return Add<T>(std::forward<A>(args)...);
    }
    Widget* Content() const { return Children().empty() ? nullptr : Children().front().get(); }
    float Offset() const { return offset_.Get(); }
    float MaxOffset() const { return std::max(0.0f, contentH_ - rect.h); }
    void ScrollTo(float y, bool animate = true);
    void ScrollIntoView(const Rect& r);
    std::function<void(float)> onScroll;

    Size MeasureContent(float availW, float availH) override;
    void OnLayout() override;
    void PaintOver(Painter& p) override;
    void DebugInfo(std::string& out) const override;
    Widget* HitTest(float x, float y) override;
    bool OnWheel(const WheelEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void OnMouseLeave() override;

private:
    Rect ThumbRect() const;
    AnimFloat offset_;
    AnimFloat barVisible_;
    float contentH_ = 0;
    bool dragging_ = false;
    float dragStartY_ = 0, dragStartOffset_ = 0;
    bool overBar_ = false;
    UINT_PTR hideTimer_ = 0;
};

// A panel with a background color, optional border and corner radius.
class Panel : public Widget {
public:
    Panel() = default;
    std::function<Color()> fill;   // background (none when empty)
    std::function<Color()> border;
    float radius = 0;
    bool shadow = false;
    void Paint(Painter& p) override;
};

// --- menus ------------------------------------------------------------------------------

struct MenuItem {
    std::wstring label;
    std::wstring shortcut;
    std::string icon;
    bool checked = false;
    bool enabled = true;
    bool separator = false;
    bool radio = false;  // checked items show a dot instead of a tick
    std::function<void()> action;
    std::vector<MenuItem> submenu;

    static MenuItem Separator() {
        MenuItem m;
        m.separator = true;
        return m;
    }
};

// Shows a menu popup anchored to `anchor` (window DIPs). Returns the popup widget.
Widget* ShowMenu(Host* host, const Rect& anchor, std::vector<MenuItem> items, Place place = Place::Below,
                 float minWidth = 200);
// Context menu at the mouse position.
Widget* ShowContextMenu(Host* host, Point at, std::vector<MenuItem> items);

// --- dialogs ----------------------------------------------------------------------------

// Standard modal dialog frame: title row with a close button, body, footer.
class Dialog : public Widget {
public:
    Dialog(std::wstring title, int width);
    Widget* Body() const { return body_; }
    Widget* Footer() const { return footer_; }
    void Close();
    void Paint(Painter& p) override;
    bool OnKeyDown(const KeyEvent& e) override;
    std::wstring subtitle;

private:
    Widget* body_ = nullptr;
    Widget* footer_ = nullptr;
};

// Shows `dialog` centered with a scrim. The dialog closes itself via Close().
Dialog* ShowDialog(Host* host, std::unique_ptr<Dialog> dialog);

// Transient message at the top center of the window.
void ShowToast(Host* host, std::wstring text, std::string icon = {}, UINT ms = 1600);
// Closes the toast on screen, if any (e.g. when the file it was about is replaced).
void DismissToast(Host* host);

}  // namespace ui
