#include "ui/Host.h"

#include "Trace.h"
#include "gfx/Text.h"
#include "ui/Theme.h"

#include <dwmapi.h>
#include <imm.h>
#include <shellapi.h>
#include <windowsx.h>

#include <oleidl.h>

#include <algorithm>
#include <cstdio>
#include <typeinfo>
#include <cmath>
#include <unordered_map>

namespace ui {

namespace {

constexpr wchar_t kHostClass[] = L"LumenHost";
constexpr UINT WM_APP_POST = WM_APP + 41;
constexpr float kResizeBorder = 6.0f;
constexpr UINT kTooltipDelayMs = 550;

bool g_automation = false;

std::vector<Host*>& Registry() {
    static std::vector<Host*> hosts;
    return hosts;
}

// Small bubble shown for Widget::tooltip.
class TooltipBubble : public Widget {
public:
    explicit TooltipBubble(std::wstring text) : text_(std::move(text)) {}
    Size MeasureContent(float availW, float) override {
        auto m = gfx::Text::Get().Measure(text_, TextStyle::Small, std::min(availW, 320.0f) - 20, gfx::kTextWrap);
        return {m.width + 20, m.height + 12};
    }
    void Paint(Painter& p) override {
        p.Shadow(rect, 4, 10, 4, 0.35f);
        p.Fill(rect, P().bgPanel, 4);
        p.Stroke(rect, P().line, 4);
        p.Text(text_, rect.Inset(Insets::Sym(10, 6)), TextStyle::Small, P().textPrimary, HAlign::Leading,
               VAlign::Center, gfx::kTextWrap);
    }

private:
    std::wstring text_;
};

std::vector<std::wstring> FilesFrom(IDataObject* data) {
    std::vector<std::wstring> files;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&fmt, &medium))) return files;
    if (auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(drop, i, path.data(), len + 1);
            files.push_back(std::move(path));
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return files;
}

void ForEachInTree(Widget* w, const std::function<void(Widget*)>& fn) {
    fn(w);
    for (auto& c : w->Children()) ForEachInTree(c.get(), fn);
}

}  // namespace

namespace {
unsigned g_automationMods = 0;
float g_scaleOverride = 1.0f;
}  // namespace

void Host::SetAutomation(bool on) {
    g_automation = on;
}

void Host::SetScaleOverride(float factor) {
    g_scaleOverride = std::clamp(factor, 0.5f, 4.0f);
}

float Host::ScaleOverride() {
    return g_scaleOverride;
}

bool Host::Automation() {
    return g_automation;
}

HRESULT Host::CoCreateDialog(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* out) {
    if (g_automation) {
        if (out) *out = nullptr;
        return E_ACCESSDENIED;
    }
    return CoCreateInstance(clsid, outer, context, iid, out);
}

HINSTANCE Host::ShellOpen(HWND hwnd, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show) {
    if (g_automation) return nullptr;
    return ShellExecuteW(hwnd, verb, file, params, dir, show);
}

void Host::Activate(HWND hwnd) {
    if (g_automation || !hwnd) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

// OLE drop target: shows the overlay while files hover and hands them over on drop.
class DropTarget : public IDropTarget {
public:
    explicit DropTarget(Host* host) : host_(host) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepts_ = host_->onDropFiles && data->QueryGetData(&fmt) == S_OK;
        *effect = accepts_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accepts_) host_->SetDragOver(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = accepts_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        host_->SetDragOver(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        host_->SetDragOver(false);
        *effect = DROPEFFECT_NONE;
        if (!accepts_) return S_OK;
        auto files = FilesFrom(data);
        if (!files.empty() && host_->onDropFiles) {
            *effect = DROPEFFECT_COPY;
            Host::Activate(host_->Hwnd());
            // Run after the drag source returns (opening files may show dialogs).
            Host* host = host_;
            host->Post([host, files = std::move(files)]() mutable {
                if (host->onDropFiles) host->onDropFiles(std::move(files));
            });
        }
        return S_OK;
    }

private:
    Host* host_;
    ULONG refs_ = 1;
    bool accepts_ = false;
};

void Host::SetDragOver(bool over) {
    if (over == dragOver_) return;
    dragOver_ = over;
    dragAnim_.To(over ? 1.0f : 0.0f, 140);
    RequestRender();
}

void Host::PaintDropOverlay(Painter& p) {
    const float k = dragAnim_.Get();
    if (k <= 0.01f) return;
    const Size c = ClientSize();
    const Rect all{0, 0, c.w, c.h};
    p.PushOpacity(k);
    p.Fill(all, P().scrim);
    const Rect frame = all.Inset(16);
    gfx::ComPtr<ID2D1StrokeStyle> dash;
    const float dashes[] = {4, 3};
    gfx::Gpu::Get().D2DFactory()->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_CUSTOM, 0),
        dashes, 2, &dash);
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    Color line = P().accentMid;
    line.a *= k;
    p.Ctx()->CreateSolidColorBrush(line.D2D(), &brush);
    p.Ctx()->DrawRoundedRectangle(D2D1::RoundedRect(frame.Inset(1).D2D(), 12, 12), brush.Get(), 2, dash.Get());
    const Rect icon = all.Center(48, 48).Offset(0, -24);
    p.Icon("add", icon, P().textPrimary);
    p.Text(dropHint.empty() ? L"Drop to open" : dropHint, {0, icon.Bottom() + 12, c.w, 28}, TextStyle::Heading,
           P().textPrimary, HAlign::Center);
    p.PopOpacity();
}

const std::vector<Host*>& Host::All() {
    return Registry();
}

Host::Host() {
    Registry().push_back(this);
}

Host::~Host() {
    auto& r = Registry();
    r.erase(std::remove(r.begin(), r.end(), this), r.end());
    for (auto& t : timers_)
        if (hwnd_) ::KillTimer(hwnd_, t.first);
    // Popups and root reference this host while they unwind.
    while (!popups_.empty()) popups_.pop_back();
    root_.reset();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

void Host::ForgetWidget(Widget* w) {
    for (Host* h : Registry()) h->Forget(w);
}

void Host::ForgetSubtree(Widget* w) {
    ForEachInTree(w, [](Widget* x) { ForgetWidget(x); });
}

void Host::Forget(Widget* w) {
    if (hot_ == w) hot_ = nullptr;
    if (pressed_ == w) pressed_ = nullptr;
    if (capture_ == w) capture_ = nullptr;
    if (focus_ == w) focus_ = nullptr;
    if (tooltipFor_ == w) tooltipFor_ = nullptr;
    if (tooltipWidget_ == w) tooltipWidget_ = nullptr;
    for (Popup& p : popups_)
        if (p.previousFocus == w) p.previousFocus = nullptr;
}

bool Host::Create(const HostOptions& options) {
    options_ = options;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = [inst] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Host::StaticWndProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.hCursor = nullptr;
        wc.lpszClassName = kHostClass;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return false;

    dpi_ = GetDpiForSystem();
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    if (!options.resizable) style &= ~WS_MAXIMIZEBOX;
    DWORD exStyle = options.owner || !options.showInTaskbar ? 0 : WS_EX_APPWINDOW;
    if (g_automation) exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (options.owner) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(options.owner, MONITOR_DEFAULTTONEAREST), &mi)) work = mi.rcWork;
    }
    const int w = static_cast<int>(std::lround(options.width * Scale()));
    const int h = static_cast<int>(std::lround(options.height * Scale()));
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (options.center) {
        RECT ref = work;
        if (options.owner) GetWindowRect(options.owner, &ref);
        x = ref.left + (ref.right - ref.left - w) / 2;
        y = ref.top + (ref.bottom - ref.top - h) / 2;
        x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - w));
        y = std::clamp(y, static_cast<int>(work.top), std::max<int>(work.top, work.bottom - h));
    }

    hwnd_ = CreateWindowExW(exStyle, kHostClass, options.title.c_str(), style, x, y, w, h, options.owner, nullptr,
                            inst, this);
    if (!hwnd_) return false;
    if (g_automation) {
        const BOOL cloak = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }
    const UINT actual = GetDpiForWindow(hwnd_);
    // With --scale the window may be bigger than the monitor; CreateWindowEx clamped it to the
    // default max track size (WM_GETMINMAXINFO comes before this host is attached).
    if (actual != dpi_ || g_scaleOverride != 1.0f) {
        dpi_ = actual;
        SetWindowPos(hwnd_, nullptr, 0, 0, static_cast<int>(std::lround(options.width * Scale())),
                     static_cast<int>(std::lround(options.height * Scale())), SWP_NOMOVE | SWP_NOZORDER);
    }
    ApplyDwmAttributes();
    // No SWP_FRAMECHANGED here: WM_NCCALCSIZE already ran during CreateWindowExW with
    // options_ set, and the extra frame change costs ~10 ms before the first paint.
    // Drag and drop is registered after the first frame (RegisterDragDrop costs a few ms).
    Post([this] {
        if (!hwnd_ || dropTarget_) return;
        dropTarget_ = new DropTarget(this);
        if (FAILED(RegisterDragDrop(hwnd_, dropTarget_))) DragAcceptFiles(hwnd_, TRUE);  // OLE not initialized
    });
    if (gfx::Gpu::Get().Ready()) surface_.Attach(hwnd_, RenderDpi());
    return true;
}

void Host::ApplyDwmAttributes() {
    BOOL dark = Theme::Get().Mode() == ThemeMode::Dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corners = options_.roundCorners ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    COLORREF border = P().line.Rgb();
    border = RGB((border >> 16) & 0xFF, (border >> 8) & 0xFF, border & 0xFF);
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    if (options_.customFrame) {
        MARGINS margins{0, 0, 1, 0};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
    }
}

void Host::OnThemeChanged() {
    ApplyDwmAttributes();
    InvalidateRect(hwnd_, nullptr, FALSE);
    RequestLayout();
}

void Host::OnDeviceReady() {
    if (!hwnd_) return;
    surface_.Attach(hwnd_, RenderDpi());
    RequestLayout();
    RenderNow();
}

void Host::Show(int cmd) {
    if (g_automation) {
        ShowWindow(hwnd_, cmd == SW_SHOWMAXIMIZED ? SW_MAXIMIZE : SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(hwnd_, cmd);
    }
    UpdateWindow(hwnd_);
}

void Host::Close() {
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void Host::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void Host::SetRoot(std::unique_ptr<Widget> root) {
    if (root_) ForgetSubtree(root_.get());
    root_ = std::move(root);
    if (root_) root_->SetHost(this);
    RequestLayout();
}

Size Host::ClientSize() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    return {(rc.right - rc.left) / Scale(), (rc.bottom - rc.top) / Scale()};
}

bool Host::IsWindowMaximized() const {
    return IsZoomed(hwnd_) != FALSE;
}

void Host::ToggleMaximize() {
    ShowWindow(hwnd_, IsWindowMaximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void Host::Minimize() {
    ShowWindow(hwnd_, SW_MINIMIZE);
}

void Host::SetTitle(const std::wstring& title) {
    SetWindowTextW(hwnd_, title.c_str());
}

void Host::SetFullscreen(bool on, bool primaryScreen) {
    if (on == fullscreen_) return;
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    if (on) {
        MONITORINFO mi{sizeof(mi)};
        if (!GetWindowPlacement(hwnd_, &placement_) ||
            !GetMonitorInfoW(primaryScreen ? MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY)
                                           : MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY),
                             &mi))
            return;
        fullscreen_ = true;
        SetWindowLongW(hwnd_, GWL_STYLE, static_cast<LONG>(style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd_, g_automation ? HWND_BOTTOM : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | (g_automation ? SWP_NOACTIVATE : 0u));
    } else {
        fullscreen_ = false;
        SetWindowLongW(hwnd_, GWL_STYLE, static_cast<LONG>(style | WS_OVERLAPPEDWINDOW));
        // A window that was maximized is restored to its normal rect first and maximized again, so
        // Windows computes the maximized rect for the captioned style.
        const bool maximized = placement_.showCmd == SW_SHOWMAXIMIZED;
        WINDOWPLACEMENT normal = placement_;
        if (maximized) normal.showCmd = g_automation ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL;
        SetWindowPlacement(hwnd_, &normal);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        if (maximized) ShowWindow(hwnd_, SW_MAXIMIZE);
    }
    RequestLayout();
}

// --- popups ---------------------------------------------------------------------

Widget* Host::ShowPopup(std::unique_ptr<Widget> content, const PopupOptions& options) {
    HideTooltip();
    Popup popup;
    popup.content = std::move(content);
    popup.content->SetHost(this);
    popup.options = options;
    popup.previousFocus = focus_;
    popup.appear = std::make_unique<AnimFloat>(options.animate ? 0.0f : 1.0f);
    if (options.animate) popup.appear->To(1.0f, 140, Ease::Out);
    Widget* raw = popup.content.get();
    popups_.push_back(std::move(popup));
    PlacePopup(popups_.back());
    if (options.focusContent && !options.passthrough) {
        std::vector<Widget*> focusables;
        CollectFocusable(raw, focusables);
        SetFocus(focusables.empty() ? nullptr : focusables.front(), false);
    }
    if (pressed_ && !raw->IsAncestorOf(pressed_)) {
        // The click that opened the popup must not "click" something under it on release.
        pressed_ = nullptr;
    }
    UpdateHot(mouse_.x, mouse_.y);
    RequestRender();
    return raw;
}

bool Host::IsPopupOpen(const Widget* content) const {
    for (const Popup& p : popups_)
        if (p.content.get() == content) return true;
    return false;
}

void Host::ClosePopup(Widget* content) {
    for (size_t i = 0; i < popups_.size(); ++i)
        if (popups_[i].content.get() == content) {
            ClosePopupAt(i);
            return;
        }
}

void Host::ClosePopupAt(size_t index) {
    if (index >= popups_.size()) return;
    // Close anything stacked above it first (submenus).
    while (popups_.size() > index + 1) ClosePopupAt(popups_.size() - 1);
    Popup popup = std::move(popups_[index]);
    popups_.erase(popups_.begin() + static_cast<std::ptrdiff_t>(index));
    Widget* restore = popup.previousFocus;
    ForgetSubtree(popup.content.get());
    auto onDismiss = std::move(popup.options.onDismiss);
    popup.content.reset();
    if (restore) SetFocus(restore, focusVisible_);
    if (onDismiss) onDismiss();
    UpdateHot(mouse_.x, mouse_.y);
    RequestRender();
}

void Host::CloseAllPopups() {
    while (!popups_.empty()) ClosePopupAt(popups_.size() - 1);
}

void Host::RepositionPopup(Widget* content) {
    for (Popup& p : popups_)
        if (p.content.get() == content) PlacePopup(p);
    RequestRender();
}

void Host::PlacePopup(Popup& popup) {
    const Size client = ClientSize();
    const PopupOptions& o = popup.options;
    const Rect& a = o.anchor;
    Size s = popup.content->Measure(client.w - 16, client.h - 16);
    s.w = std::min(s.w, client.w - 16);
    s.h = std::min(s.h, client.h - 16);
    float x = a.x, y = a.y;
    popup.above = false;
    switch (o.place) {
        case Place::Below:
        case Place::BelowRight:
            x = o.place == Place::Below ? a.x : a.Right() - s.w;
            y = a.Bottom() + o.offset;
            if (y + s.h > client.h - 8 && a.y - o.offset - s.h >= 8) {
                y = a.y - o.offset - s.h;
                popup.above = true;
            }
            break;
        case Place::Above:
        case Place::AboveRight:
            x = o.place == Place::Above ? a.x : a.Right() - s.w;
            y = a.y - o.offset - s.h;
            popup.above = true;
            if (y < 8 && a.Bottom() + o.offset + s.h <= client.h - 8) {
                y = a.Bottom() + o.offset;
                popup.above = false;
            }
            break;
        case Place::Right:
            x = a.Right() + o.offset;
            y = a.y;
            if (x + s.w > client.w - 8) x = a.x - o.offset - s.w;
            break;
        case Place::Left:
            x = a.x - o.offset - s.w;
            y = a.y;
            if (x < 8) x = a.Right() + o.offset;
            break;
        case Place::Center:
            x = (client.w - s.w) / 2;
            y = (client.h - s.h) / 2;
            break;
        case Place::At:
            break;
    }
    x = std::clamp(x, 8.0f, std::max(8.0f, client.w - 8 - s.w));
    y = std::clamp(y, 8.0f, std::max(8.0f, client.h - 8 - s.h));
    popup.content->Arrange({std::round(x), std::round(y), s.w, s.h});
}

// --- focus / capture -------------------------------------------------------------------

void Host::SetFocus(Widget* w, bool fromKeyboard) {
    focusVisible_ = fromKeyboard;
    if (w == focus_) {
        RequestRender();
        return;
    }
    Widget* old = focus_;
    focus_ = w;
    if (old) old->OnFocusChanged(false);
    if (focus_) focus_->OnFocusChanged(true);
    RequestRender();
}

void Host::SetCapture(Widget* w) {
    capture_ = w;
    ::SetCapture(hwnd_);
}

void Host::ReleaseCapture() {
    capture_ = nullptr;
    if (GetCapture() == hwnd_) ::ReleaseCapture();
}

void Host::SetCursorHidden(bool hidden) {
    if (hidden == cursorHidden_) return;
    cursorHidden_ = hidden;
    if (mouseInside_) SetCursor(hidden ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
}

// --- scheduling -----------------------------------------------------------------------

// Frames are drawn from WM_PAINT, which Windows delivers only when no input or posted
// messages are waiting: clicks, keys and wheel ticks are handled before the next frame even
// while an animation runs (a posted render message would be served ahead of input).
void Host::RequestRender() {
    if (renderPending_ || !hwnd_) return;
    renderPending_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Host::RequestLayout() {
    layoutDirty_ = true;
    RequestRender();
}

void Host::RenderNow() {
    Render();
}

void Host::Post(std::function<void()> fn) {
    posted_.push_back(std::move(fn));
    PostMessageW(hwnd_, WM_APP_POST, 0, 0);
}

UINT_PTR Host::StartTimer(UINT ms, std::function<void()> fn, bool repeat) {
    const UINT_PTR id = nextTimer_++;
    timers_[id] = {std::move(fn), repeat};
    ::SetTimer(hwnd_, id, ms, nullptr);
    return id;
}

void Host::CancelTimer(UINT_PTR id) {
    if (!id) return;
    ::KillTimer(hwnd_, id);
    timers_.erase(id);
}

void Host::DoLayout() {
    layoutDirty_ = false;
    const Size client = ClientSize();
    if (root_) root_->Arrange({0, 0, client.w, client.h});
    for (Popup& p : popups_) PlacePopup(p);
    OnLayoutDone();
}

namespace {

// LUMEN_TRACE frame statistics: once a second, frame count and the worst layout,
// paint and present times (ms) go to the trace file.
struct FrameStats {
    int frames = 0;
    double layout = 0, paint = 0, present = 0, maxLayout = 0, maxPaint = 0, maxPresent = 0, since = 0;
    void Add(double l, double p, double pr) {
        static const bool enabled = GetEnvironmentVariableW(L"LUMEN_TRACE", nullptr, 0) > 0;
        if (!enabled) return;
        ++frames;
        layout += l, paint += p, present += pr;
        maxLayout = std::max(maxLayout, l), maxPaint = std::max(maxPaint, p), maxPresent = std::max(maxPresent, pr);
        const double now = AnimFloat::Now();
        if (since == 0) since = now;
        if (now - since < 1000) return;
        char line[200];
        snprintf(line, sizeof line, "frames %d  layout avg %.2f max %.2f  paint avg %.2f max %.2f  present avg %.2f max %.2f",
                 frames, layout / frames, maxLayout, paint / frames, maxPaint, present / frames, maxPresent);
        Trace(line);
        *this = {};
    }
};
FrameStats g_frameStats;

}  // namespace

void Host::Render() {
    renderPending_ = false;
    if (!hwnd_ || IsIconic(hwnd_)) return;
    // Nothing to draw with until the device is up (the GDI skeleton covers the window): the first
    // layout then runs with the first frame, not inside ShowWindow, where it waited for the fonts.
    if (!gfx::Gpu::Get().Ready()) return;
    const double t0 = AnimFloat::Now();
    const bool animating = AnimFloat::TickAll(t0);
    if (layoutDirty_ || animating) DoLayout();
    const double t1 = AnimFloat::Now();

    ID2D1DeviceContext5* ctx = surface_.Begin();
    if (!ctx) return;
    {
        Painter p(ctx);
        if (textSink_) p.RecordText(textSink_);
        PaintBackground(p);
        if (root_) root_->PaintTree(p);
        const Size client = ClientSize();
        for (Popup& popup : popups_) {
            const float k = popup.appear ? popup.appear->Get() : 1.0f;
            if (popup.options.scrim) p.Fill({0, 0, client.w, client.h}, P().scrim.Fade(k));
            p.PushOpacity(k);
            const float slide = (1 - k) * (popup.options.scrim ? 8.0f : 4.0f) * (popup.above ? 1.0f : -1.0f);
            if (slide != 0) ctx->SetTransform(D2D1::Matrix3x2F::Translation(0, slide));
            popup.content->PaintTree(p);
            if (slide != 0) ctx->SetTransform(D2D1::Matrix3x2F::Identity());
            p.PopOpacity();
        }
        PaintDropOverlay(p);
    }
    OnFramePainted();
    const double t2 = AnimFloat::Now();
    if (surface_.End()) {
        if (!gpuPainted_) Trace("first frame");
        gpuPainted_ = true;
    } else {
        RequestLayout();
    }
    g_frameStats.Add(t1 - t0, t2 - t1, AnimFloat::Now() - t2);
    gfx::Text::Get().EndFrame();
    if (AnimFloat::AnyActive()) RequestRender();
}

// --- hit testing ----------------------------------------------------------------------

Widget* Host::HitTestAll(float x, float y, bool* blockedByModal) const {
    if (blockedByModal) *blockedByModal = false;
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
        if (it->options.passthrough) continue;
        if (Widget* hit = it->content->HitTest(x, y)) return hit;
        if (it->options.modal) {
            if (blockedByModal) *blockedByModal = true;
            return nullptr;
        }
    }
    return root_ ? root_->HitTest(x, y) : nullptr;
}

Widget* Host::InteractiveAt(float x, float y) const {
    Widget* w = HitTestAll(x, y);
    while (w && !w->interactive) w = w->Parent();
    return w && w->IsEnabled() ? w : nullptr;
}

void Host::UpdateHot(float x, float y) {
    Widget* hot = mouseInside_ ? InteractiveAt(x, y) : nullptr;
    if (capture_) hot = capture_->rect.Contains(x, y) ? capture_ : nullptr;
    if (hot == hot_) return;
    Widget* old = hot_;
    hot_ = hot;
    if (old) old->OnMouseLeave();
    if (hot_) hot_->OnMouseEnter();
    UpdateTooltip();
    RequestRender();
}

LRESULT Host::NcHitTest(LPARAM lp) {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (options_.resizable && !fullscreen_ && !IsWindowMaximized()) {
        const int b = static_cast<int>(std::lround(kResizeBorder * Scale()));
        const bool left = pt.x < b, right = pt.x >= rc.right - b, top = pt.y < b, bottom = pt.y >= rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (bottom) return HTBOTTOM;
        if (top) return HTTOP;
    }
    const float x = pt.x / Scale(), y = pt.y / Scale();
    for (const Popup& p : popups_)
        if (p.options.modal || (!p.options.passthrough && p.content->rect.Contains(x, y))) return HTCLIENT;
    Widget* hit = root_ ? root_->HitTest(x, y) : nullptr;
    for (Widget* w = hit; w; w = w->Parent()) {
        if (w->ncRole == NcRole::Maximize) return HTMAXBUTTON;
        if (w->interactive) return HTCLIENT;
        if (w->ncRole == NcRole::Caption) return HTCAPTION;
    }
    return HTCLIENT;
}

Mods Host::CurrentMods() const {
    return {GetKeyState(VK_CONTROL) < 0 || (g_automationMods & 1), GetKeyState(VK_SHIFT) < 0 || (g_automationMods & 2),
            GetKeyState(VK_MENU) < 0 || (g_automationMods & 4)};
}

// --- mouse --------------------------------------------------------------------------

void Host::OnMouseButton(UINT msg, WPARAM wp, LPARAM lp) {
    const float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
    MouseEvent e{x, y, MouseButton::Left, CurrentMods()};
    bool down = false, dbl = false;
    switch (msg) {
        case WM_LBUTTONDBLCLK: dbl = true; [[fallthrough]];
        case WM_LBUTTONDOWN: down = true; break;
        case WM_LBUTTONUP: break;
        case WM_RBUTTONDBLCLK:
        case WM_RBUTTONDOWN: down = true; e.button = MouseButton::Right; break;
        case WM_RBUTTONUP: e.button = MouseButton::Right; break;
        case WM_MBUTTONDBLCLK:
        case WM_MBUTTONDOWN: down = true; e.button = MouseButton::Middle; break;
        case WM_MBUTTONUP: e.button = MouseButton::Middle; break;
        case WM_XBUTTONDBLCLK:
        case WM_XBUTTONDOWN: down = true; [[fallthrough]];
        case WM_XBUTTONUP: e.button = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2; break;
        default: return;
    }

    if (down) {
        HideTooltip();
        // Clicking outside popups closes them (topmost first). A modal popup
        // swallows the click that closes it.
        while (!popups_.empty()) {
            Popup& top = popups_.back();
            if (top.options.passthrough) break;
            if (top.content->rect.Contains(x, y)) break;
            if (!top.options.dismissOnOutside) break;
            const bool wasModal = top.options.modal;
            ClosePopupAt(popups_.size() - 1);
            if (wasModal) return;
        }
        bool blocked = false;
        Widget* hit = HitTestAll(x, y, &blocked);
        if (blocked) return;
        Widget* target = hit;
        while (target && !target->interactive) target = target->Parent();
        if (target && !target->IsEnabled()) target = nullptr;

        Widget* focusTarget = hit;
        while (focusTarget && !focusTarget->focusable) focusTarget = focusTarget->Parent();
        if (focusTarget != focus_) SetFocus(focusTarget && focusTarget->IsEnabled() ? focusTarget : nullptr, false);
        else focusVisible_ = false;

        if (e.button == MouseButton::Left) {
            pressed_ = target;
            capture_ = target;
            ::SetCapture(hwnd_);
            if (target) {
                if (dbl) {
                    if (!target->OnDoubleClick(e) && target->onDoubleClick) target->onDoubleClick();
                } else {
                    target->OnMouseDown(e);
                }
            }
        } else if (e.button == MouseButton::Right) {
            for (Widget* w = hit; w; w = w->Parent()) {
                if (w->onContextMenu && w->IsEnabled()) {
                    w->onContextMenu(e);
                    break;
                }
                if (w->interactive && w->IsEnabled() && w->OnMouseDown(e)) break;
            }
        } else {
            for (Widget* w = target; w; w = w->Parent())
                if (w->interactive && w->OnMouseDown(e)) break;
        }
    } else if (e.button == MouseButton::Left) {
        Widget* t = capture_ ? capture_ : pressed_;
        Widget* wasPressed = pressed_;
        capture_ = nullptr;
        if (GetCapture() == hwnd_) ::ReleaseCapture();
        if (t) {
            t->OnMouseUp(e);
            if (t == wasPressed && InteractiveAt(x, y) == t) t->OnClick();
        }
        pressed_ = nullptr;
        UpdateHot(x, y);
    } else {
        Widget* t = InteractiveAt(x, y);
        for (Widget* w = t; w; w = w->Parent())
            if (w->interactive && w->OnMouseUp(e)) break;
    }
    RequestRender();
}

// --- keyboard -------------------------------------------------------------------------

void Host::CollectFocusable(Widget* w, std::vector<Widget*>& out) const {
    if (!w->visible || !w->enabled) return;
    if (w->focusable) out.push_back(w);
    for (auto& c : w->Children()) CollectFocusable(c.get(), out);
}

void Host::MoveFocus(bool backwards) {
    Widget* scope = root_.get();
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (!it->options.passthrough && it->options.focusContent) {
            scope = it->content.get();
            break;
        }
    if (!scope) return;
    std::vector<Widget*> list;
    CollectFocusable(scope, list);
    if (list.empty()) return;
    auto it = std::find(list.begin(), list.end(), focus_);
    size_t i = 0;
    if (it == list.end())
        i = backwards ? list.size() - 1 : 0;
    else if (backwards)
        i = (static_cast<size_t>(it - list.begin()) + list.size() - 1) % list.size();
    else
        i = (static_cast<size_t>(it - list.begin()) + 1) % list.size();
    SetFocus(list[i], true);
}

void Host::OnKey(UINT vk, bool down, bool repeat) {
    if (!down) return;
    KeyEvent e{vk, CurrentMods(), repeat};
    HideTooltip();

    for (Widget* w = focus_; w; w = w->Parent())
        if (w->IsEnabled() && w->OnKeyDown(e)) {
            RequestRender();
            return;
        }

    if (vk == VK_TAB && !e.mods.ctrl && !e.mods.alt) {
        MoveFocus(e.mods.shift);
        return;
    }
    if (vk == VK_ESCAPE && !popups_.empty()) {
        for (size_t i = popups_.size(); i-- > 0;)
            if (!popups_[i].options.passthrough && popups_[i].options.dismissOnEscape) {
                ClosePopupAt(i);
                return;
            }
    }
    // Space/Enter press the focused control only when it was reached with the
    // keyboard: after a mouse click Space stays the window's shortcut (play/pause).
    if ((vk == VK_RETURN || vk == VK_SPACE) && focusVisible_ && focus_ && focus_->interactive &&
        focus_->IsEnabled() && !focus_->WantsTextInput()) {
        focus_->OnClick();
        RequestRender();
        return;
    }
    // Keyboard shortcuts of the window are suspended while a modal popup is open.
    for (const Popup& p : popups_)
        if (p.options.modal) return;
    if (onKey) onKey(e);
}

// --- tooltip -----------------------------------------------------------------------------

void Host::UpdateTooltip() {
    Widget* target = hot_ && !hot_->tooltip.empty() ? hot_ : nullptr;
    if (target == tooltipFor_) return;
    HideTooltip();
    if (!target || target->tooltip.empty() || pressed_) return;
    tooltipFor_ = target;
    tooltipTimer_ = StartTimer(kTooltipDelayMs, [this] {
        tooltipTimer_ = 0;
        if (!tooltipFor_ || hot_ == nullptr || !mouseInside_) return;
        PopupOptions o;
        o.anchor = {mouse_.x, mouse_.y + 18, 0, 0};
        const Rect& r = tooltipFor_->rect;
        // Prefer centering under small controls (icon buttons), like native tooltips.
        if (r.w < 80) o.anchor = {r.Cx(), r.Bottom() + 6, 0, 0};
        o.place = Place::At;
        if (tooltipFor_->tooltipBeside) {
            o.anchor = {r.x, r.Cy() - 14, r.w, 28};
            o.place = Place::Right;
            o.offset = 8;
        }
        o.passthrough = true;
        o.focusContent = false;
        o.dismissOnOutside = false;
        auto bubble = std::make_unique<TooltipBubble>(tooltipFor_->tooltip);
        Size s = bubble->Measure(400, 200);
        if (r.w < 80 && !tooltipFor_->tooltipBeside) {  // a beside-tooltip is already placed
            o.anchor.x -= s.w / 2;
            // Controls along the bottom edge (the player bar): above them, not over the buttons.
            if (r.Bottom() + 6 + s.h > ClientSize().h - 8) o.anchor.y = r.y - 6 - s.h;
        }
        Widget* target = tooltipFor_;
        tooltipWidget_ = ShowPopup(std::move(bubble), o);  // (ShowPopup hides any tooltip first)
        tooltipFor_ = target;
    });
}

void Host::HideTooltip() {
    if (tooltipTimer_) {
        CancelTimer(tooltipTimer_);
        tooltipTimer_ = 0;
    }
    tooltipFor_ = nullptr;
    if (tooltipWidget_) {
        Widget* w = tooltipWidget_;
        tooltipWidget_ = nullptr;
        ClosePopup(w);
    }
}

// --- tree dump ---------------------------------------------------------------------------

namespace {

// `"..."` in UTF-8 with \" \\ \n \r \t escaped.
void AppendQuoted(std::string& out, std::wstring_view s) {
    std::wstring esc;
    esc.reserve(s.size() + 2);
    for (wchar_t ch : s) {
        switch (ch) {
            case L'"': esc += L"\\\""; break;
            case L'\\': esc += L"\\\\"; break;
            case L'\n': esc += L"\\n"; break;
            case L'\r': esc += L"\\r"; break;
            case L'\t': esc += L"\\t"; break;
            default: esc += ch; break;
        }
    }
    out += '"';
    if (!esc.empty()) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, esc.data(), static_cast<int>(esc.size()), nullptr, 0, nullptr, nullptr);
        const size_t at = out.size();
        out.resize(at + static_cast<size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, esc.data(), static_cast<int>(esc.size()), out.data() + at, n, nullptr, nullptr);
    }
    out += '"';
}

void AppendRect(std::string& out, const char* prefix, const Rect& r) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s[%.0f,%.0f %.0fx%.0f]", prefix, r.x, r.y, r.w, r.h);
    out += buf;
}

// ` text="..."` and what happened to it: ` trunc` (wider than its box: ellipsized or cut),
// ` clip` (taller than its box), ` overflow` (glyphs outside the widget's rect), ` faded`
// (transparent), ` box=[...]` (the box, when it is not the widget's rect).
void AppendText(std::string& out, const TextRecord& t, const Rect& owner) {
    out += " text=";
    AppendQuoted(out, t.text);
    gfx::Text& text = gfx::Text::Get();
    // Natural size, measured here once: one unlimited line, or wrapped to the box.
    float natW = 0, natH = 0;
    if (IDWriteTextLayout* one = text.Layout(t.text, t.style, 100000.0f, 100000.0f, HAlign::Leading, VAlign::Top, 0)) {
        DWRITE_TEXT_METRICS m{};
        one->GetMetrics(&m);
        natW = m.widthIncludingTrailingWhitespace;
        natH = m.height;
    }
    const bool unbounded = t.box.w >= 10000;  // scrolled text (TextBox): judged against the clip
    const bool wrap = (t.flags & gfx::kTextWrap) != 0;
    bool trunc = false, clip = false;
    float shownW = natW;
    if (unbounded) {
        trunc = t.halign == HAlign::Leading && (t.box.x < t.clip.x - 0.5f || t.box.x + natW > t.clip.Right() + 0.5f);
    } else if (wrap) {
        if (IDWriteTextLayout* wrapped = text.Layout(t.text, t.style, std::max(1.0f, t.box.w), 100000.0f,
                                                     HAlign::Leading, VAlign::Top, gfx::kTextWrap)) {
            DWRITE_TEXT_METRICS m{};
            wrapped->GetMetrics(&m);
            shownW = m.widthIncludingTrailingWhitespace;
            natH = m.height;
        }
        trunc = shownW > t.box.w + 0.5f;
        if (natH > t.box.h + 1.0f) (t.flags & gfx::kTextEllipsis ? trunc : clip) = true;
    } else {
        trunc = natW > t.box.w + 0.5f;
        clip = natH > t.box.h + 1.0f;
    }
    if (trunc) out += " trunc";
    if (clip) out += " clip";
    if (!unbounded) {
        // Horizontal extent of the glyphs; an ellipsized line fills its box.
        const float w = (t.flags & gfx::kTextEllipsis) ? std::min(shownW, t.box.w) : shownW;
        float x0 = t.box.x;
        if (t.halign == HAlign::Center) x0 = t.box.Cx() - w / 2;
        if (t.halign == HAlign::Trailing) x0 = t.box.Right() - w;
        if (x0 < owner.x - 1.0f || x0 + w > owner.Right() + 1.0f) out += " overflow";
    }
    if (t.faded) out += " faded";
    if (std::abs(t.box.x - owner.x) > 0.5f || std::abs(t.box.y - owner.y) > 0.5f || std::abs(t.box.w - owner.w) > 0.5f ||
        std::abs(t.box.h - owner.h) > 0.5f)
        AppendRect(out, " box=", unbounded ? t.box.Intersect(t.clip) : t.box);
}

}  // namespace

bool Host::Idle() const {
    return hwnd_ && !layoutDirty_ && !renderPending_ && posted_.empty() && !AnimFloat::AnyActive();
}

void Host::DumpTree(const std::wstring& path) {
    // One real frame with text recording on, so the dump sees exactly what is on screen.
    std::vector<TextRecord> texts;
    if (hwnd_ && gfx::Gpu::Get().Ready() && !IsIconic(hwnd_)) {
        textSink_ = &texts;
        Render();
        textSink_ = nullptr;
    }
    std::unordered_map<const Widget*, std::vector<const TextRecord*>> byOwner;
    for (const TextRecord& t : texts) {
        auto& list = byOwner[t.owner];
        // Text drawn twice in one place (outlines, shadows) is listed once.
        if (!list.empty() && list.back()->text == t.text && list.back()->box.x == t.box.x &&
            list.back()->box.y == t.box.y)
            continue;
        list.push_back(&t);
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    std::string line = "=== window title=";
    wchar_t title[256] = {};
    if (hwnd_) GetWindowTextW(hwnd_, title, static_cast<int>(std::size(title)));
    AppendQuoted(line, title);
    const Size client = hwnd_ ? ClientSize() : Size{};
    char buf[256];
    snprintf(buf, sizeof buf, " client=%.0fx%.0f scale=%g theme=%s fullscreen=%d maximized=%d hwnd=%llu main=%d dpi=%u\n",
             client.w, client.h, Scale(), Theme::Get().Mode() == ThemeMode::Dark ? "dark" : "light", fullscreen_ ? 1 : 0,
             hwnd_ && IsWindowMaximized() ? 1 : 0, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd_)),
             options_.mainWindow ? 1 : 0, dpi_);
    line += buf;
    fwrite(line.data(), 1, line.size(), f);

    std::function<void(const Widget*, int)> dump = [&](const Widget* w, int depth) {
        const char* name = typeid(*w).name();
        if (strncmp(name, "class ", 6) == 0) name += 6;
        line.assign(static_cast<size_t>(depth) * 2, ' ');
        line += name;
        if (!w->id.empty()) line += " #" + w->id;
        AppendRect(line, " ", w->rect);
        if (!w->visible) line += " hidden";
        if (!w->enabled) line += " disabled";
        if (w->interactive) line += " interactive";
        if (w == focus_) line += " focus";
        if (w == hot_) line += " hot";
        if (w == pressed_) line += " pressed";
        w->DebugInfo(line);
        if (auto it = byOwner.find(w); it != byOwner.end())
            for (const TextRecord* t : it->second) AppendText(line, *t, w->rect);
        if (!w->tooltip.empty()) {
            line += " tip=";
            AppendQuoted(line, w->tooltip);
        }
        line += '\n';
        fwrite(line.data(), 1, line.size(), f);
        for (auto& c : w->Children()) dump(c.get(), depth + 1);
    };
    if (root_) dump(root_.get(), 0);
    for (const Popup& p : popups_) {
        fputs("--- popup\n", f);
        dump(p.content.get(), 0);
    }
    fclose(f);
}

std::wstring Host::WriteTreeDump() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring base = std::wstring(tmp) + L"lumen-tree-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring path =
        base + L"-" + std::to_wstring(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd_))) + L".txt";
    DumpTree(path);
    if (options_.mainWindow) CopyFileW(path.c_str(), (base + L".txt").c_str(), FALSE);
    return path;
}

void Host::PaintBackground(Painter& p) {
    const Size client = ClientSize();
    p.Fill({0, 0, client.w, client.h}, P().bgBase);
}

void Host::PaintSkeleton(HDC dc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const uint32_t c = P().bgBase.Rgb();
    HBRUSH b = CreateSolidBrush(RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

// --- window procedure ------------------------------------------------------------------

bool Host::HandleMessage(UINT, WPARAM, LPARAM, LRESULT*) {
    return false;
}

LRESULT CALLBACK Host::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Host* host = nullptr;
    if (msg == WM_NCCREATE) {
        host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        host->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    } else {
        host = reinterpret_cast<Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return host ? host->WndProc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Host::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    HWND self = hwnd_;
    LRESULT result = 0;
    if (HandleMessage(msg, wp, lp, &result)) return result;

    switch (msg) {
        case WM_NCCALCSIZE:
            // Also the wp == FALSE call during CreateWindowExW: the whole window is client area
            // from the start, without a SWP_FRAMECHANGED round trip afterwards.
            if (options_.customFrame) {
                if (wp == TRUE && !fullscreen_ && IsZoomed(hwnd_)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
                    const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi_) + pad;
                    const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi_) + pad;
                    params->rgrc[0].left += fx;
                    params->rgrc[0].right -= fx;
                    params->rgrc[0].top += fy;
                    params->rgrc[0].bottom -= fy;
                }
                return 0;
            }
            break;

        case WM_NCHITTEST:
            if (options_.customFrame) return NcHitTest(lp);
            break;

        case WM_NCMOUSEMOVE:
            if (wp == HTMAXBUTTON) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                Widget* w = root_ ? root_->HitTest(pt.x / Scale(), pt.y / Scale()) : nullptr;
                while (w && w->ncRole != NcRole::Maximize) w = w->Parent();
                if (w != hot_) {
                    if (hot_) hot_->OnMouseLeave();
                    hot_ = w;
                    if (hot_) hot_->OnMouseEnter();
                    RequestRender();
                }
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
                TrackMouseEvent(&tme);
                return 0;
            }
            if (hot_ && hot_->ncRole == NcRole::Maximize) {
                hot_ = nullptr;
                RequestRender();
            }
            break;

        case WM_NCMOUSELEAVE:
            if (hot_ && hot_->ncRole == NcRole::Maximize) {
                hot_ = nullptr;
                pressed_ = nullptr;
                RequestRender();
            }
            break;

        case WM_NCLBUTTONDOWN:
            if (wp == HTMAXBUTTON) {
                pressed_ = hot_;
                RequestRender();
                return 0;
            }
            if (!popups_.empty()) CloseAllPopups();
            break;

        case WM_NCLBUTTONUP:
            if (wp == HTMAXBUTTON) {
                if (pressed_ && pressed_ == hot_) pressed_->OnClick();
                pressed_ = nullptr;
                RequestRender();
                return 0;
            }
            break;

        case WM_NCLBUTTONDBLCLK:
            if (wp == HTMAXBUTTON) return 0;
            break;

        case WM_MOUSEMOVE: {
            const float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
            const bool moved = x != mouse_.x || y != mouse_.y;
            mouse_ = {x, y};
            mouseInside_ = true;
            if (!trackingLeave_ && !g_automation) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                trackingLeave_ = TrackMouseEvent(&tme) != FALSE;
            }
            if (!moved) return 0;
            MouseEvent e{x, y, MouseButton::Left, CurrentMods()};
            if (capture_) {
                capture_->OnMouseMove(e);
                UpdateHot(x, y);
            } else {
                UpdateHot(x, y);
                if (hot_) hot_->OnMouseMove(e);
            }
            if (tooltipWidget_ && tooltipFor_ && !tooltipFor_->rect.Contains(x, y)) HideTooltip();
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_automation) return 0;
            trackingLeave_ = false;
            mouseInside_ = false;
            if (!capture_) UpdateHot(-1, -1);
            HideTooltip();
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            OnMouseButton(msg, wp, lp);
            return msg >= WM_XBUTTONDOWN ? TRUE : 0;

        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd_ && capture_) {
                Widget* t = capture_;
                capture_ = nullptr;
                MouseEvent e{mouse_.x, mouse_.y, MouseButton::Left, CurrentMods()};
                t->OnMouseUp(e);
                pressed_ = nullptr;
                RequestRender();
            }
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            WheelEvent e{pt.x / Scale(), pt.y / Scale(),
                         static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA, msg == WM_MOUSEHWHEEL,
                         CurrentMods()};
            HideTooltip();
            for (Widget* w = HitTestAll(e.x, e.y); w; w = w->Parent())
                if (w->IsEnabled() && w->OnWheel(e)) break;
            RequestRender();
            return 0;
        }

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT || LOWORD(lp) == static_cast<WORD>(HTERROR)) {
                if (cursorHidden_) {
                    SetCursor(nullptr);
                } else {
                    Widget* w = capture_ ? capture_ : hot_;
                    SetCursor(LoadCursorW(nullptr, w && w->cursor ? w->cursor : IDC_ARROW));
                }
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
            OnKey(static_cast<UINT>(wp), true, (lp & (1 << 30)) != 0);
            if (msg == WM_SYSKEYDOWN && wp == VK_MENU) break;
            return 0;

        case WM_SYSCHAR:
            if (wp == VK_RETURN || wp == VK_SPACE) return 0;
            break;

        case WM_CHAR:
            if (focus_ && wp >= 0x20 && wp != 0x7F && focus_->OnChar(static_cast<wchar_t>(wp))) RequestRender();
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (focus_ && focus_->WantsTextInput()) {
                if (HIMC imc = ImmGetContext(hwnd_)) {
                    const Rect c = focus_->CaretRect();
                    COMPOSITIONFORM cf{CFS_POINT, {static_cast<LONG>(c.x * Scale()), static_cast<LONG>(c.y * Scale())}, {}};
                    ImmSetCompositionWindow(imc, &cf);
                    ImmReleaseContext(hwnd_, imc);
                }
            }
            break;

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                surface_.Resize(LOWORD(lp), HIWORD(lp));
                layoutDirty_ = true;
                OnResize();
                Render();
            }
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            surface_.SetDpi(RenderDpi());
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RequestLayout();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(options_.minWidth * Scale());
            mmi->ptMinTrackSize.y = static_cast<LONG>(options_.minHeight * Scale());
            // Maximized = the monitor's work area (plus the frame WM_NCCALCSIZE takes off), so the
            // window never covers the taskbar, whatever the style (tool windows, custom frame).
            MONITORINFO mi{sizeof(mi)};
            if (options_.customFrame && !fullscreen_ &&
                GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) {
                const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
                const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi_) + pad;
                const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi_) + pad;
                mmi->ptMaxPosition = {mi.rcWork.left - mi.rcMonitor.left - fx, mi.rcWork.top - mi.rcMonitor.top - fy};
                mmi->ptMaxSize = {mi.rcWork.right - mi.rcWork.left + 2 * fx, mi.rcWork.bottom - mi.rcWork.top + 2 * fy};
                mmi->ptMaxTrackSize = {std::max(mmi->ptMaxTrackSize.x, mmi->ptMaxSize.x),
                                       std::max(mmi->ptMaxTrackSize.y, mmi->ptMaxSize.y)};
            }
            // --scale: a window may outgrow the real monitor, as on a bigger high-DPI one
            // (1280x780 DIP at 200 % is 2560x1560 px). Maximized and fullscreen stay the monitor.
            if (g_scaleOverride != 1.0f) mmi->ptMaxTrackSize = {8192, 8192};
            return 0;
        }

        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            if (!active_) HideTooltip();
            if (onActivate) onActivate(active_);
            RequestRender();
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            if (!gpuPainted_) PaintSkeleton(dc);
            EndPaint(hwnd_, &ps);
            Render();
            return 0;
        }

        case WM_TIMER: {
            auto it = timers_.find(wp);
            if (it == timers_.end()) return 0;
            std::function<void()> fn = it->second.first;
            if (!it->second.second) {
                ::KillTimer(hwnd_, wp);
                timers_.erase(it);
            }
            fn();
            return 0;
        }

        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wp);
            std::vector<std::wstring> files;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(len, L'\0');
                DragQueryFileW(drop, i, path.data(), len + 1);
                files.push_back(std::move(path));
            }
            DragFinish(drop);
            if (onDropFiles && !files.empty()) onDropFiles(std::move(files));
            return 0;
        }

        case kDeviceReadyMessage: {
            // Sent for the quick WARP device and again for the hardware one (gfx/Gpu.h).
            gfx::Gpu& gpu = gfx::Gpu::Get();
            const bool hadDevice = gpu.Ready();
            if (!gpu.FinishDevice()) {
                if (gpu.DeviceFailed())
                    MessageBoxW(hwnd_, L"Direct3D 11 / DirectComposition is not available.", L"Lumen", MB_ICONERROR);
                return 0;
            }
            for (Host* h : Registry()) {
                if (hadDevice)
                    h->RequestLayout();  // Surface::Begin rebuilds for the new device generation
                else
                    h->OnDeviceReady();
            }
            Trace(hadDevice ? "hardware device" : "first device");
            return 0;
        }

        case kRepaintMessage:
            RequestRender();
            return 0;

        case kAutomationModsMessage:
            if (g_automation) g_automationMods = static_cast<unsigned>(wp);
            return 0;

        case kDumpTreeMessage:
            WriteTreeDump();
            return TRUE;

        case WM_APP_POST: {
            std::vector<std::function<void()>> tasks;
            tasks.swap(posted_);
            for (auto& t : tasks) t();
            return 0;
        }

        case WM_CLOSE:
            if (onCloseRequest && !onCloseRequest()) return 0;
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            RevokeDragDrop(hwnd_);
            if (dropTarget_) {
                dropTarget_->Release();
                dropTarget_ = nullptr;
            }
            if (onDestroyed) onDestroyed();
            return 0;

        case WM_NCDESTROY:
            surface_.Detach();
            hwnd_ = nullptr;
            break;

        default:
            break;
    }
    return DefWindowProcW(self, msg, wp, lp);
}

}  // namespace ui
