#pragma once
#include "gfx/Gpu.h"
#include "ui/Anim.h"
#include "ui/Widget.h"

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ui {

struct HostOptions {
    std::wstring title = L"Lumen";
    float width = 1280, height = 800;  // DIP, client area
    float minWidth = 480, minHeight = 320;
    bool resizable = true;
    bool customFrame = true;  // draw our own title bar; Windows keeps shadow + snap
    bool roundCorners = true;
    HWND owner = nullptr;  // owned windows stay above their owner, no taskbar button
    bool center = true;
    bool showInTaskbar = true;
};

// Where a popup appears relative to its anchor rect.
enum class Place : uint8_t { Below, Above, Right, Left, Center, At, BelowRight, AboveRight };

struct PopupOptions {
    Rect anchor;  // window DIPs; for Place::At only x/y are used
    Place place = Place::Below;
    float offset = 4;
    bool modal = false;              // blocks input to the window underneath
    bool scrim = false;              // dims the window underneath (dialogs)
    bool dismissOnOutside = true;    // click outside closes it
    bool dismissOnEscape = true;
    bool focusContent = true;        // move keyboard focus into the popup
    bool passthrough = false;        // ignores the mouse entirely (tooltips, toasts)
    bool animate = true;             // short fade/slide in
    std::function<void()> onDismiss; // called when closed for any reason
};

// A top-level window that hosts a widget tree, drawn with Direct2D through
// DirectComposition. Owns hit testing, hover/press/capture, keyboard focus,
// popups and tooltips, custom-frame handling and render scheduling.
class Host {
public:
    Host();
    virtual ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    bool Create(const HostOptions& options);
    HWND Hwnd() const { return hwnd_; }
    void Show(int cmd = SW_SHOWNORMAL);
    void Close();  // like the user pressing X
    void Destroy();

    void SetRoot(std::unique_ptr<Widget> root);
    Widget* Root() const { return root_.get(); }

    // --- popups / dialogs ------------------------------------------------------------
    Widget* ShowPopup(std::unique_ptr<Widget> content, const PopupOptions& options);
    void ClosePopup(Widget* content);
    void CloseAllPopups();
    bool HasPopup() const { return !popups_.empty(); }
    bool IsPopupOpen(const Widget* content) const;
    // Re-measures and re-places a popup after its content changed size.
    void RepositionPopup(Widget* content);

    // --- focus / pointer ---------------------------------------------------------------
    void SetFocus(Widget* w, bool fromKeyboard = false);
    Widget* Focus() const { return focus_; }
    bool FocusVisible() const { return focusVisible_; }
    void SetCapture(Widget* w);
    void ReleaseCapture();
    Widget* Hot() const { return hot_; }
    Widget* Pressed() const { return pressed_; }
    Point MousePos() const { return mouse_; }
    bool MouseInside() const { return mouseInside_; }
    void SetCursorHidden(bool hidden);

    // --- rendering -------------------------------------------------------------------
    void RequestRender();
    void RequestLayout();
    void RenderNow();
    float Scale() const { return static_cast<float>(dpi_) / 96.0f; }
    UINT Dpi() const { return dpi_; }
    Size ClientSize() const;
    // Called when the shared GPU device becomes available.
    void OnDeviceReady();
    // Pass to gfx::Gpu::CreateDeviceAsync: the host that receives it finishes the
    // device and attaches every window's surface.
    static constexpr UINT kDeviceReadyMessage = WM_APP + 42;
    // Debug aid: posting this writes the widget tree (type, rect, flags) to
    // %TEMP%\lumen-tree-<pid>.txt. See tools/dev/dump-tree.ps1.
    static constexpr UINT kDumpTreeMessage = WM_APP + 43;
    // Any thread may post this to ask the window for a repaint.
    static constexpr UINT kRepaintMessage = WM_APP + 44;
    // --automation only: wParam = held modifiers (1 Ctrl, 2 Shift, 4 Alt) for the
    // keys and clicks that follow, since posted input doesn't change GetKeyState.
    static constexpr UINT kAutomationModsMessage = WM_APP + 45;
    void DumpTree(const std::wstring& path) const;
    // Re-applies DWM attributes (dark title/border) and repaints after a theme change.
    void OnThemeChanged();

    // --- window state ------------------------------------------------------------------
    bool IsWindowMaximized() const;
    bool IsFullscreen() const { return fullscreen_; }
    // `primaryScreen` = fullscreen on the primary monitor instead of the current one.
    void SetFullscreen(bool on, bool primaryScreen = false);
    void ToggleMaximize();
    void Minimize();
    void SetTitle(const std::wstring& title);
    bool IsActive() const { return active_; }

    // --- utilities ---------------------------------------------------------------------
    // Runs fn on this window's thread after the current message.
    void Post(std::function<void()> fn);
    // Calls fn after ms (once, or repeatedly). Returns an id for CancelTimer.
    UINT_PTR StartTimer(UINT ms, std::function<void()> fn, bool repeat = false);
    void CancelTimer(UINT_PTR id);

    std::function<bool(const KeyEvent&)> onKey;               // unhandled keys
    std::function<bool()> onCloseRequest;                     // false cancels closing
    std::function<void()> onDestroyed;
    std::function<void(std::vector<std::wstring>)> onDropFiles;
    // Text shown in the overlay while files are dragged over the window.
    std::wstring dropHint;
    std::function<void(bool active)> onActivate;

    // Every live host, e.g. to repaint all windows after a theme change.
    static const std::vector<Host*>& All();
    // Test runs drive windows with posted messages while the real cursor is
    // elsewhere: keep hover state instead of dropping it on WM_MOUSELEAVE.
    // --automation: test instances run in the background. Their windows are cloaked
    // (invisible but still composed, so PrintWindow captures them), never activate,
    // have no taskbar button and stay at the bottom of the z-order.
    static void SetAutomation(bool on);
    static bool Automation();
    // Brings a window forward, except in --automation runs.
    static void Activate(HWND hwnd);
    // Native windows (file/folder pickers, the colour dialog, Explorer, the browser) can't be
    // cloaked, so background test runs never open them. Same signatures as the Win32 calls.
    static HRESULT CoCreateDialog(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* out);
    static HINSTANCE ShellOpen(HWND hwnd, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show);
    static bool NativeUiAllowed() { return !Automation(); }
    static void ForgetWidget(Widget* w);
    static void ForgetSubtree(Widget* w);

protected:
    // Subclass hooks.
    virtual void OnLayoutDone() {}
    virtual void PaintBackground(Painter&) {}
    virtual bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
    virtual void OnResize() {}
    // GDI fill used before the GPU device exists and behind DComp content.
    virtual void PaintSkeleton(HDC dc);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    struct Popup {
        std::unique_ptr<Widget> content;
        PopupOptions options;
        Widget* previousFocus = nullptr;
        std::unique_ptr<AnimFloat> appear;
        bool above = false;  // flipped above the anchor (slide direction)
    };
    void ApplyDwmAttributes();
    void SetDragOver(bool over);
    void PaintDropOverlay(Painter& p);
    friend class DropTarget;
    class DropTarget* dropTarget_ = nullptr;
    bool dragOver_ = false;
    AnimFloat dragAnim_{0.0f};

    void DoLayout();
    void Render();
    void PlacePopup(Popup& popup);
    Widget* HitTestAll(float x, float y, bool* blockedByModal = nullptr) const;
    Widget* InteractiveAt(float x, float y) const;
    LRESULT NcHitTest(LPARAM lp);
    void UpdateHot(float x, float y);
    void OnMouseButton(UINT msg, WPARAM wp, LPARAM lp);
    void OnKey(UINT vk, bool down, bool repeat);
    void MoveFocus(bool backwards);
    void CollectFocusable(Widget* w, std::vector<Widget*>& out) const;
    void UpdateTooltip();
    void HideTooltip();
    Mods CurrentMods() const;
    void ClosePopupAt(size_t index);
    void Forget(Widget* w);

    HWND hwnd_ = nullptr;
    HostOptions options_;
    UINT dpi_ = 96;
    gfx::Surface surface_;
    std::unique_ptr<Widget> root_;
    std::vector<Popup> popups_;

    Widget* hot_ = nullptr;
    Widget* pressed_ = nullptr;
    Widget* capture_ = nullptr;
    Widget* focus_ = nullptr;
    bool focusVisible_ = false;
    Point mouse_{-1, -1};
    bool mouseInside_ = false;
    bool trackingLeave_ = false;
    bool cursorHidden_ = false;
    bool active_ = true;

    bool renderPending_ = false;
    bool layoutDirty_ = true;
    bool fullscreen_ = false;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};

    // Tooltip state.
    Widget* tooltipFor_ = nullptr;
    Widget* tooltipWidget_ = nullptr;
    UINT_PTR tooltipTimer_ = 0;

    std::map<UINT_PTR, std::pair<std::function<void()>, bool>> timers_;
    UINT_PTR nextTimer_ = 100;
    std::vector<std::function<void()>> posted_;
};

}  // namespace ui
