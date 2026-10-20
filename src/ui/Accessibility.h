#pragma once
// UI Automation for the widget tree: screen readers (Narrator, NVDA) and other clients see every
// window as a tree of buttons, fields, sliders, lists and menus. Each element is a widget, or an
// entry a widget paints itself (a grid card, a menu row, a tab). Widgets are described on demand
// (Widget::Describe); nothing here runs until a client asks for a window.
#include "ui/Types.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace ui {

class Host;
class Widget;
struct TextRecord;

enum class Role : uint8_t {
    Auto,  // worked out from the class and flags: interactive = Button, painted text = Text
    Skip,  // not an element; its children belong to its parent
    Group, Text, Button, CheckBox, ComboBox, Edit, Slider, List, ListItem, Menu, MenuItem, Tab, TabItem, Window
};

// What a widget reports. An empty name is filled from the text the widget paints (Text and the
// clickable roles), the label before it (fields, sliders, switches), then its tooltip.
struct A11y {
    Role role = Role::Auto;
    std::wstring name;
    std::wstring help;   // HelpText (a field's placeholder); unset: the tooltip
    std::wstring value;  // Edit / ComboBox; a Slider's value as read ("0:03 of 0:30", "1.25×")
    std::wstring keys;   // AcceleratorKey: the shortcut that does the same
    int heading = 0;     // 1-9: a section title of that level
    bool password = false;
    int toggle = -1;  // CheckBox: 0 off, 1 on
    int selected = -1;  // a ListItem / TabItem widget that is one of several: 0 no, 1 the current one
    double min = 0, max = 0, now = 0, step = 0;  // Slider
    std::function<void(double)> setRange;            // Slider; unset = read-only
    std::function<void(const std::wstring&)> setValue;  // Edit; unset = read-only
    std::function<void()> invoke;  // unset: OnClick, the path a click takes
    // Edit: the Text pattern (caret and selection as offsets into `value`) when `select` is set.
    size_t caret = 0, anchor = 0;
    std::function<void(size_t anchor, size_t caret)> select;
    std::function<Rect(size_t from, size_t to)> textRect;  // window DIPs of that part of the text

    // Entries painted without child widgets.
    struct Item {
        Role role = Role::ListItem;  // Skip leaves the entry out (menu separators)
        std::wstring name;
        std::wstring keys;  // AcceleratorKey
        Rect rect;  // window DIPs; empty = scrolled out or hidden
        bool selected = false;
        int toggle = -1;
        int expanded = -1;  // a row that opens a submenu: 0 closed, 1 open
        bool enabled = true;
    };
    int items = 0;
    std::function<void(int, Item&)> item;
    std::function<void(int)> selectItem, invokeItem;
    int focusedItem = -1;  // the entry the keyboard is on while the widget has focus
};

namespace a11y {
// Hooks for ui::Host.
bool WantsRoot(LPARAM objectId);  // WM_GETOBJECT for UI Automation
LRESULT ReturnRoot(Host* host, WPARAM wp, LPARAM lp);
bool Recording();  // the next frame should record its texts (names) for UI Automation
// After a recorded frame: names, and the change events of the focused element (and of the
// last one a client acted on): its name, toggle, value, selection and caret.
void TextsPainted(Host* host, const std::vector<TextRecord>& texts);
// The language of the UI (an LCID), reported as every element's Culture.
void SetCulture(unsigned long lcid);
void Forget(const Widget* w);
void WindowDestroyed(HWND hwnd);
// Events, sent only while a client listens. Widgets call FocusChanged too when the keyboard
// moves between their entries (menu rows, grid cards).
void FocusChanged(Host* host);
void StructureChanged(Host* host);
// Has screen readers say `text` now, without moving focus: status messages (toasts, a library
// scan, the next track, a playback error). A newer message replaces one not yet spoken.
void Announce(Host* host, const std::wstring& text);
}  // namespace a11y

}  // namespace ui
