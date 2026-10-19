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
    // The app's main window: --automation commands go to it and its tree dump is also written
    // to the old %TEMP%\lumen-tree-<pid>.txt.
    bool mainWindow = false;
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
    bool animate = true;             // short fade/slide in (never when opened from the keyboard)
    // A click outside that lands on another enabled control (a bar button, the sidebar, a caption
    // button) closes the popup and also presses it. A click on a plain surface (the video) or on
    // the control that opened the popup only closes it, as with a menu. For panels, not menus.
    bool clickThrough = false;
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
    // A menu, panel or dialog: not a toast or tooltip, which take no focus and stay on outside clicks.
    bool HasInteractivePopup() const;
    size_t PopupCount() const { return popups_.size(); }
    Widget* PopupAt(size_t i) const { return i < popups_.size() ? popups_[i].content.get() : nullptr; }
    bool IsPopupOpen(const Widget* content) const;
    // Re-measures and re-places a popup after its content changed size.

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
    // DIP-to-pixel factor: the monitor's DPI / 96, times the --scale test override.
    float Scale() const { return static_cast<float>(dpi_) / 96.0f * ScaleOverride(); }
    UINT Dpi() const { return dpi_; }  // the monitor's real DPI (window frame metrics)
    Size ClientSize() const;
    // Called when the shared GPU device becomes available.
    void OnDeviceReady();
    // Pass to gfx::Gpu::CreateDeviceAsync: the host that receives it finishes the
    // device and attaches every window's surface.
    static constexpr UINT kDeviceReadyMessage = WM_APP + 42;
    // Debug aid, --automation runs only: sending or posting this to a host window writes its
    // widget tree (type, rect, state, the text it shows) to %TEMP%\lumen-tree-<pid>-<hwnd>.txt,
    // and for the main window also to %TEMP%\lumen-tree-<pid>.txt. Handled synchronously:
    // SendMessage returns once the file is written. See tools/dev/dump-tree.ps1 for the format.
    static constexpr UINT kDumpTreeMessage = WM_APP + 43;
    // Any thread may post this to ask the window for a repaint.
    static constexpr UINT kRepaintMessage = WM_APP + 44;
    // --automation only: wParam = held modifiers (1 Ctrl, 2 Shift, 4 Alt) for the
    // keys and clicks that follow, since posted input doesn't change GetKeyState.
    static constexpr UINT kAutomationModsMessage = WM_APP + 45;
    // Draws one frame that records every text it paints, then writes the tree to `path`.
    void DumpTree(const std::wstring& path);
    // Writes the dump file(s) named above; returns the per-window path.
    std::wstring WriteTreeDump();
    bool IsMainWindow() const { return options_.mainWindow; }
    // The window is on screen (Reveal): cloaked only until its first GPU frame (the background),
    // so never a blank or skeleton frame. --automation windows stay cloaked; tests read the
    // LumenRevealed window property (or revealed=1 in the tree dump) instead.
    bool Revealed() const { return revealed_; }
    // The widget tree is drawn, at ContentAppear() opacity (0 to 1 over 140 ms, once; ContentReady).
    bool ContentShown() const { return contentShown_; }
    float ContentAppear() const { return contentAppear_.Get(); }
    // Nothing waits to be laid out, drawn or run, and no animation is running.
    bool Idle() const;
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
    // A one-shot timer for work the user already asked for (a debounced search): Idle() is false
    // until it has run or been cancelled, so tests that wait for idle see its result.
    UINT_PTR StartWorkTimer(UINT ms, std::function<void()> fn);
    void CancelTimer(UINT_PTR id);

    std::function<bool(const KeyEvent&)> onKey;               // unhandled keys
    std::function<bool()> onCloseRequest;                     // false cancels closing
    std::function<void()> onDestroyed;
    std::function<void(std::vector<std::wstring>)> onDropFiles;
    // Top edge (DIPs) for a toast of height h; default 56 (below the title bar).
    std::function<float(float h)> toastTop;
    // The part of the window (x and w) a toast centres in, beside open side panels; default the whole width.
    std::function<Rect()> toastBand;
    // Text shown in the overlay while files are dragged over the window.
    std::wstring dropHint;
    // A card floating over the content (the library's mini player), empty when none: scroll views
    // it covers let their last rows scroll up above it.
    Rect floatingCard;

    // Every live host, e.g. to repaint all windows after a theme change.
    static const std::vector<Host*>& All();
    // Test runs drive windows with posted messages while the real cursor is elsewhere: they
    // skip TrackMouseEvent, so hover stays until a test posts WM_MOUSELEAVE itself.
    // --automation: test instances run in the background. Their windows are cloaked
    // (invisible but still composed, so PrintWindow captures them), never activate,
    // have no taskbar button and stay at the bottom of the z-order.
    static void SetAutomation(bool on);
    static bool Automation();
    // --automation --scale <factor>: lays out and draws every window as if the monitor were at
    // factor x its real scale (1.5 on a 100 % screen looks like 150 %). Set before any window.
    static void SetScaleOverride(float factor);
    static float ScaleOverride();
    // Windows high contrast is on (SPI_GETHIGHCONTRAST). onHighContrastChanged runs when it
    // changes (WM_SETTINGCHANGE), once for all windows.
    static bool HighContrast();
    static std::function<void()> onHighContrastChanged;
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
    virtual void OnFramePainted() {}  // after the tree and popups are drawn, before Present
    // Opaque window background under the widget tree. Everything the frame leaves transparent
    // shows the GDI surface beneath the composition layer (stale or see-through after a resize).
    virtual void PaintBackground(Painter& p);
    virtual bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
    virtual void OnResize() {}
    // GDI fill used before the GPU device exists and behind DComp content.
    virtual void PaintSkeleton(HDC dc);
    // Asked after each frame until the content shows: does this frame show the first screen
    // complete (its images, the file it opens)? The window itself is on screen at once with its
    // background; the content fades in from it with the first frame that is ready, or
    // ContentCapMs after the show at the latest, so a slow disk never hides it.
    virtual bool ContentReady() { return true; }
    virtual UINT ContentCapMs() { return 400; }
    void Reveal();
    void ShowContent();

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
    float RenderDpi() const { return Scale() * 96.0f; }  // DPI of the Direct2D surface
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
    bool ClicksThrough(const Popup& popup, float x, float y) const;
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
    Widget* lastLeftDown_ = nullptr;  // target of the last left press, only compared (may be gone)
    Widget* focus_ = nullptr;
    bool focusVisible_ = false;
    Point mouse_{-1, -1};
    bool mouseInside_ = false;
    bool trackingLeave_ = false;
    // The pointer is over a caption (HTCAPTION) widget with a tooltip, a cut window title: hot_
    // is that widget, and mouse_ follows WM_NCMOUSEMOVE until it leaves.
    bool ncHover_ = false;
    bool cursorHidden_ = false;
    bool active_ = true;
    Widget* wheeled_ = nullptr;  // took the last wheel notch; hover is re-hit while it scrolls

    std::vector<TextRecord>* textSink_ = nullptr;  // set while DumpTree draws its frame
    bool renderPending_ = false;
    bool gpuPainted_ = false;  // a GPU frame is on screen: WM_PAINT skips the GDI skeleton
    bool revealed_ = false;
    bool contentShown_ = false;
    bool contentDue_ = false;      // ContentCapMs has passed: the next frame shows it, ready or not
    UINT_PTR contentTimer_ = 0;    // set on the first show
    AnimFloat contentAppear_{0.0f};
    bool layoutDirty_ = true;
    bool fullscreen_ = false;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};

    // Tooltip state.
    Widget* tooltipFor_ = nullptr;
    Widget* tooltipWidget_ = nullptr;
    UINT_PTR tooltipTimer_ = 0;
    std::wstring tooltipText_;  // what the bubble (or its timer) shows: a change re-arms it
    Rect tooltipRect_;          // tooltipFor_'s rect then: the bubble goes when the widget moves
    double tooltipHiddenAt_ = -1e9;  // AnimFloat::Now() when a shown bubble last went away

    std::map<UINT_PTR, std::pair<std::function<void()>, bool>> timers_;
    std::vector<UINT_PTR> workIds_;  // StartWorkTimer timers still pending
    UINT_PTR nextTimer_ = 100;
    std::vector<std::function<void()>> posted_;
};

}  // namespace ui
