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
    std::wstring value;  // Edit / ComboBox
    bool password = false;
    int toggle = -1;  // CheckBox: 0 off, 1 on
    double min = 0, max = 0, now = 0, step = 0;  // Slider
    std::function<void(double)> setRange;            // Slider; unset = read-only
    std::function<void(const std::wstring&)> setValue;  // Edit; unset = read-only
    std::function<void()> invoke;  // unset: OnClick, the path a click takes

    // Entries painted without child widgets.
    struct Item {
        Role role = Role::ListItem;  // Skip leaves the entry out (menu separators)
        std::wstring name;
        Rect rect;  // window DIPs; empty = scrolled out or hidden
        bool selected = false;
        int toggle = -1;
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
void TextsPainted(const std::vector<TextRecord>& texts);
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
