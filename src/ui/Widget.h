#pragma once
#include "ui/Painter.h"
#include "ui/Types.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class Host;

// Size rule along one axis.
struct Length {
    enum Kind : uint8_t { Hug, Fixed, Fill };
    Kind kind = Hug;
    float value = 0;  // pixels for Fixed, weight for Fill

    static constexpr Length Auto() { return {Hug, 0}; }
    static constexpr Length Px(float v) { return {Fixed, v}; }
    static constexpr Length Grow(float weight = 1) { return {Fill, weight}; }
};

enum class Dir : uint8_t { None, Row, Column, Stack };
enum class Align : uint8_t { Start, Center, End, Stretch };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween };
// How the window frame treats a widget during WM_NCHITTEST.
enum class NcRole : uint8_t { None, Caption, Maximize };

struct Mods {
    bool ctrl = false, shift = false, alt = false;
};
enum class MouseButton : uint8_t { Left, Right, Middle, X1, X2 };
struct MouseEvent {
    float x = 0, y = 0;
    MouseButton button = MouseButton::Left;
    Mods mods;
    bool cancelled = false;  // OnMouseUp after a lost capture (Alt+Tab, a dialog): undo, don't commit
};
struct WheelEvent {
    float x = 0, y = 0;
    float delta = 0;  // notches, positive = away from the user / right
    bool horizontal = false;
    Mods mods;
};
struct KeyEvent {
    UINT key = 0;  // virtual-key code
    Mods mods;
    bool repeat = false;
};

// Retained-mode UI node. Rects are absolute window DIPs assigned by the parent's
// layout. Containers lay children out as a small flexbox (Row/Column), overlap
// them (Stack) or leave positioning to an OnLayout override (None).
class Widget {
public:
    Widget();
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- tree -------------------------------------------------------------
    template <class T, class... A>
    T* Add(A&&... args) {
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T* raw = p.get();
        AddChild(std::move(p));
        return raw;
    }
    Widget* AddChild(std::unique_ptr<Widget> child, int index = -1);
    std::unique_ptr<Widget> RemoveChild(Widget* child);
    void ClearChildren();
    Widget* Parent() const { return parent_; }
    const std::vector<std::unique_ptr<Widget>>& Children() const { return children_; }
    Host* GetHost() const;
    bool IsAncestorOf(const Widget* w) const;

    // --- layout properties ----------------------------------------------------
    Length width, height;
    float minW = 0, minH = 0, maxW = kInf, maxH = kInf;
    Dir dir = Dir::None;
    float gap = 0;
    Insets pad;
    Align align = Align::Start;  // cross axis placement of children
    Justify justify = Justify::Start;
    bool clipChildren = false;

    // --- state ----------------------------------------------------------------
    bool visible = true;
    bool enabled = true;
    bool interactive = false;  // receives mouse events itself
    bool focusable = false;    // reachable with Tab, receives keys
    NcRole ncRole = NcRole::None;
    std::wstring tooltip;
    bool tooltipBeside = false;  // show the tooltip to the right (icon rails), not below
    LPCWSTR cursor = IDC_ARROW;
    std::string id;  // for debugging and tests
    Rect rect;

    bool IsHot() const;
    bool IsPressed() const;
    bool IsFocused() const;
    bool IsEnabled() const;  // false if this or any ancestor is disabled
    bool ShowFocusRing() const;

    // --- layout ---------------------------------------------------------------
    // Desired size within the available space, honoring width/height/min/max.
    Size Measure(float availW, float availH);
    // Assigns the rect and lays out children.
    void Arrange(const Rect& r);
    virtual Size MeasureContent(float availW, float availH);
    virtual void OnLayout();

    // --- painting -------------------------------------------------------------
    virtual void Paint(Painter& p);      // before children
    virtual void PaintOver(Painter& p);  // after children
    virtual void PaintChildren(Painter& p);
    void PaintTree(Painter& p);
    // Painter::Clip() while painting; lets virtualized widgets skip offscreen items.
    void PaintFocusRing(Painter& p, const Rect& r, float radius) const;
    // The colour a widget fills its whole rect with under its children, if any: a scroll view
    // fades its cut edges into the nearest one.
    virtual bool Backdrop(Color& /*out*/) const { return false; }

    // --- input ----------------------------------------------------------------
    // Deepest visible widget under the point (interactive or not).
    virtual Widget* HitTest(float x, float y);
    virtual void OnMouseEnter() {}
    virtual void OnMouseLeave() {}
    virtual bool OnMouseDown(const MouseEvent&) { return false; }
    virtual bool OnMouseUp(const MouseEvent&) { return false; }
    virtual bool OnMouseMove(const MouseEvent&) { return false; }
    virtual bool OnDoubleClick(const MouseEvent&) { return false; }
    virtual bool OnWheel(const WheelEvent&) { return false; }
    virtual bool OnKeyDown(const KeyEvent&) { return false; }
    virtual bool OnChar(wchar_t) { return false; }
    virtual void OnFocusChanged(bool /*focused*/) {}
    // Text input widgets report the caret so the IME window follows it.
    virtual bool WantsTextInput() const { return false; }
    virtual Rect CaretRect() const { return rect; }
    // Called on a left click (press and release over the widget).
    virtual void OnClick();

    std::function<void()> onClick;
    std::function<void(const MouseEvent&)> onContextMenu;
    std::function<void()> onDoubleClick;

    // --- debugging --------------------------------------------------------------
    // Tree dumps (Host::DumpTree) only, never per frame: appends state flags such as " scroll",
    // " on" or " icon=play" (UTF-8, each with a leading space) to the widget's dump line. The
    // text a widget shows is recorded from its Paint calls, so overrides don't repeat it.
    virtual void DebugInfo(std::string& out) const;

    // --- helpers ----------------------------------------------------------------
    void Invalidate() const;
    void InvalidateLayout() const;
    // The root widget of a window or popup knows its host.
    void SetHost(Host* host) { host_ = host; }

protected:
    Size FlexMeasure(float availW, float availH);
    void FlexLayout();

private:
    Widget* parent_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    Host* host_ = nullptr;
};

}  // namespace ui
