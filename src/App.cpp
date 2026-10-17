#include "App.h"

#include "Theme.h"
#include "Trace.h"
#include "Util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace {

constexpr UINT WM_APP_RENDER = WM_APP + 10;
constexpr UINT WM_APP_CLOSED = WM_APP + 11;
constexpr UINT WM_APP_DEVICE_READY = WM_APP + 12;

constexpr UINT_PTR kTimerHide = 1;
constexpr UINT_PTR kTimerToast = 2;
constexpr UINT_PTR kTimerAnim = 3;

constexpr wchar_t kVideoHostClass[] = L"LumenVideoHost";

constexpr float kMinWidthDip = 560.0f;
constexpr float kMinHeightDip = 320.0f;
// Narrow (vertical) videos get pillarboxed so the seek bar stays usable.
constexpr float kFitMinWidthDip = 760.0f;

}  // namespace

int App::Run(HINSTANCE instance, const std::wstring& initialFile) {
    pendingFile_ = initialFile;
    if (!CreateWindows(instance)) return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool App::CreateWindows(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kMainWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW vc{sizeof(vc)};
    vc.lpfnWndProc = DefWindowProcW;
    vc.hInstance = instance;
    vc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    vc.lpszClassName = kVideoHostClass;
    if (!RegisterClassExW(&vc)) return false;

    dpi_ = GetDpiForSystem();
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = static_cast<int>(960 * Scale());
    const int h = static_cast<int>((540 + theme::kTitleH + theme::kBarH) * Scale());
    const int x = work.left + (work.right - work.left - w) / 2;
    const int y = work.top + (work.bottom - work.top - h) / 2;

    Trace("create window");
    hwnd_ = CreateWindowExW(0, kMainWindowClass, L"Lumen", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);

    // Disabled, so mouse input over the video goes to the main window.
    video_ = CreateWindowExW(WS_EX_NOPARENTNOTIFY, kVideoHostClass, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED, 0, 0, 1, 1,
                             hwnd_, nullptr, instance, nullptr);
    if (!video_) return false;

    ApplyWindowAttributes();
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    DragAcceptFiles(hwnd_, TRUE);

    Render();  // layout only until the device is ready
    LayoutChildren();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    Trace("window shown");

    // The GPU device (driver load) and libmpv start only now: their DLL loads
    // hold the loader lock and would otherwise delay the first paint.
    renderer_.CreateDeviceAsync(hwnd_, WM_APP_DEVICE_READY);
    player_.StartAsync(hwnd_, video_);

    // Fonts are only needed for the D2D frame, which waits for the device anyway.
    return renderer_.CreateFactories() && ui_.Init(renderer_);
}

void App::PaintSkeleton(HDC dc) {
    auto fill = [&](const D2D1_RECT_F& r, D2D1_COLOR_F c) {
        const float s = Scale();
        RECT px{static_cast<LONG>(std::lround(r.left * s)), static_cast<LONG>(std::lround(r.top * s)),
                static_cast<LONG>(std::lround(r.right * s)), static_cast<LONG>(std::lround(r.bottom * s))};
        HBRUSH brush = CreateSolidBrush(RGB(c.r * 255, c.g * 255, c.b * 255));
        FillRect(dc, &px, brush);
        DeleteObject(brush);
    };
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F all = {0, 0, rc.right / Scale(), rc.bottom / Scale()};
    fill(all, theme::kBg);
    if (!fullscreen_) {
        fill({0, 0, all.right, Ui::TitleHeight(false)}, theme::kChrome);
        fill(ui_.BarRect(), theme::kChrome);
    }
}

void App::ApplyWindowAttributes() {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    COLORREF border = RGB(0x26, 0x26, 0x26);
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = nullptr;
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return app ? app->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

D2D1_POINT_2F App::ToDip(LPARAM lp) const {
    return {GET_X_LPARAM(lp) / Scale(), GET_Y_LPARAM(lp) / Scale()};
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp == TRUE) {
                if (!fullscreen_ && IsZoomed(hwnd_)) {
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
            return OnNcHitTest(lp);

        case WM_NCMOUSEMOVE:
            if (wp == HTMAXBUTTON) {
                SetHover(Hit::Maximize);
                if (!trackingNcMouse_) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
                    trackingNcMouse_ = TrackMouseEvent(&tme) != FALSE;
                }
                return 0;
            }
            if (model_.hover == Hit::Maximize) SetHover(Hit::None);
            break;

        case WM_NCMOUSELEAVE:
            trackingNcMouse_ = false;
            if (model_.hover == Hit::Maximize) SetHover(Hit::None);
            break;

        case WM_NCLBUTTONDOWN:
            if (menu_.open) CloseMenu();
            if (wp == HTMAXBUTTON) {
                model_.pressed = Hit::Maximize;
                RequestRender();
                return 0;
            }
            break;

        case WM_NCLBUTTONUP:
            if (wp == HTMAXBUTTON) {
                if (model_.pressed == Hit::Maximize) ToggleMaximize();
                model_.pressed = Hit::None;
                RequestRender();
                return 0;
            }
            break;

        case WM_NCLBUTTONDBLCLK:
            if (wp == HTMAXBUTTON) return 0;
            break;

        case WM_MOUSEMOVE: {
            POINT raw{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const bool moved = raw.x != lastMouse_.x || raw.y != lastMouse_.y;
            lastMouse_ = raw;
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                trackingMouse_ = TrackMouseEvent(&tme) != FALSE;
            }
            if (moved && fullscreen_) ShowControls();
            auto p = ToDip(lp);
            OnMouseMove(p.x, p.y);
            return 0;
        }

        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            lastMouse_ = {-1, -1};
            if (GetCapture() != hwnd_) SetHover(Hit::None);
            return 0;

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd_);
            auto p = ToDip(lp);
            OnLButtonDown(p.x, p.y);
            return 0;
        }

        case WM_LBUTTONUP: {
            auto p = ToDip(lp);
            OnLButtonUp(p.x, p.y);
            if (GetCapture() == hwnd_) ReleaseCapture();
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            SetCapture(hwnd_);
            auto p = ToDip(lp);
            OnDoubleClick(p.x, p.y);
            return 0;
        }

        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd_ && (model_.seekDragging || volumeDragging_)) {
                if (model_.seekDragging) player_.SeekAbsolute(model_.seekDragPos, true);
                model_.seekDragging = false;
                volumeDragging_ = false;
                model_.pressed = Hit::None;
                RequestRender();
            }
            return 0;

        case WM_RBUTTONUP:
            if (menu_.open) CloseMenu();
            return 0;

        case WM_MOUSEWHEEL:
            ChangeVolume(5.0 * GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            return 0;

        case WM_KEYDOWN:
            OnKeyDown(wp);
            return 0;

        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN) {
                ToggleFullscreen();
                return 0;
            }
            break;

        case WM_SYSCHAR:
            if (wp == VK_RETURN) return 0;
            break;

        case WM_SETCURSOR: {
            const UINT hit = LOWORD(lp);
            if (hit == HTCLIENT || hit == static_cast<UINT>(HTERROR)) {
                SetCursor(cursorHidden_ ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        }

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) return 0;
            renderer_.Resize(LOWORD(lp), HIWORD(lp));
            Render();
            LayoutChildren();
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            renderer_.SetDpi(static_cast<float>(dpi_));
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(kMinWidthDip * Scale());
            mmi->ptMinTrackSize.y = static_cast<LONG>(kMinHeightDip * Scale());
            return 0;
        }

        case WM_ACTIVATE:
            model_.active = LOWORD(wp) != WA_INACTIVE;
            RequestRender();
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            PaintSkeleton(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_APP_DEVICE_READY:
            if (!renderer_.FinishInit(hwnd_, static_cast<float>(dpi_))) {
                MessageBoxW(hwnd_, L"Не удалось инициализировать Direct3D 11 / DirectComposition.", L"Lumen",
                            MB_ICONERROR);
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            }
            Trace("ui on screen");
            Render();
            return 0;

        case WM_TIMER:
            if (wp == kTimerHide) {
                HideControlsIfIdle();
            } else if (wp == kTimerToast) {
                KillTimer(hwnd_, kTimerToast);
                toastTarget_ = 0;
                StartAnimation();
            } else if (wp == kTimerAnim) {
                StepAnimation();
            }
            return 0;

        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(wp);
            UINT len = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(drop, 0, path.data(), len + 1);
            DragFinish(drop);
            if (!path.empty()) {
                SetForegroundWindow(hwnd_);
                OpenFile(path);
            }
            return 0;
        }

        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kCopyDataOpen) return FALSE;
            std::wstring path(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!path.empty() && path.back() == L'\0') path.pop_back();
            if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
            SetForegroundWindow(hwnd_);
            if (!path.empty()) OpenFile(path);
            return TRUE;
        }

        case WM_APP_MPV_READY:
            if (!closing_) OnPlayerReady(wp != 0);
            return 0;

        case WM_APP_MPV_WAKEUP:
            if (!closing_) OnPlayerEvents();
            return 0;

        case WM_APP_RENDER:
            if (renderPending_) Render();
            return 0;

        case WM_CLOSE:
            if (closing_) return 0;
            closing_ = true;
            ShowWindow(hwnd_, SW_HIDE);
            // mpv's video window is a child of ours, owned by an mpv thread, so
            // tear mpv down off the UI thread while this thread keeps pumping.
            std::thread([this] {
                player_.Shutdown();
                PostMessageW(hwnd_, WM_APP_CLOSED, 0, 0);
            }).detach();
            return 0;

        case WM_APP_CLOSED:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void App::SyncModel() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    model_.width = (rc.right - rc.left) / Scale();
    model_.height = (rc.bottom - rc.top) / Scale();
    model_.fullscreen = fullscreen_;
    model_.maximized = IsZoomed(hwnd_) != FALSE;
    model_.player = &player_.State();
    model_.hasPrev = siblingIndex_ > 0;
    model_.hasNext = siblingIndex_ >= 0 && siblingIndex_ + 1 < static_cast<int>(siblings_.size());
    model_.menu = &menu_;
    if (!fullscreen_) model_.barOpacity = 1.0f;
}

void App::LayoutChildren() {
    const D2D1_RECT_F v = ui_.VideoRect();
    const float s = Scale();
    const int l = static_cast<int>(std::lround(v.left * s));
    const int t = static_cast<int>(std::lround(v.top * s));
    const int r = static_cast<int>(std::lround(v.right * s));
    const int b = static_cast<int>(std::lround(v.bottom * s));
    SetWindowPos(video_, nullptr, l, t, std::max(1, r - l), std::max(1, b - t),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void App::RequestRender() {
    if (renderPending_ || !hwnd_) return;
    renderPending_ = true;
    PostMessageW(hwnd_, WM_APP_RENDER, 0, 0);
}

void App::Render() {
    renderPending_ = false;
    SyncModel();
    ui_.Layout(model_);
    if (ID2D1DeviceContext* ctx = renderer_.Begin()) {
        ui_.Draw(ctx, model_);
        if (!renderer_.End()) RequestRender();
    }
}

LRESULT App::OnNcHitTest(LPARAM lp) {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd_, &pt);
    RECT rc;
    GetClientRect(hwnd_, &rc);

    if (!fullscreen_ && !IsZoomed(hwnd_)) {
        const int b = static_cast<int>(std::lround(theme::kResizeBorder * Scale()));
        const bool left = pt.x < b, right = pt.x >= rc.right - b;
        const bool top = pt.y < b, bottom = pt.y >= rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (bottom) return HTBOTTOM;
        if (top) return HTTOP;
    }

    const Hit hit = ui_.HitTest(pt.x / Scale(), pt.y / Scale(), model_, nullptr);
    if (hit == Hit::Maximize) return HTMAXBUTTON;
    if (hit == Hit::Caption) return HTCAPTION;
    return HTCLIENT;
}

void App::SetHover(Hit hit, int menuIndex) {
    const int menuHover = hit == Hit::MenuItem ? menuIndex : -1;
    if (hit == model_.hover && menuHover == menu_.hover) return;
    model_.hover = hit;
    menu_.hover = menuHover;
    RequestRender();
}

void App::OnMouseMove(float x, float y) {
    model_.mouseX = x;
    model_.mouseY = y;
    const PlayerState& s = player_.State();

    if (model_.seekDragging) {
        model_.seekDragPos = ui_.SeekFraction(x) * s.duration;
        player_.SeekAbsolute(model_.seekDragPos, false);
        RequestRender();
        return;
    }
    if (volumeDragging_) {
        player_.SetVolume(ui_.VolumeFromX(x));
        return;
    }

    int menuIndex = -1;
    const Hit hit = ui_.HitTest(x, y, model_, &menuIndex);
    const bool tooltip = hit == Hit::Seek || model_.hover == Hit::Seek;
    SetHover(hit, menuIndex);
    if (tooltip) RequestRender();
}

void App::OnLButtonDown(float x, float y) {
    int menuIndex = -1;
    const Hit hit = ui_.HitTest(x, y, model_, &menuIndex);
    const PlayerState& s = player_.State();

    if (menu_.open && hit != Hit::Menu && hit != Hit::MenuItem && hit != menu_.anchor) {
        CloseMenu();
        if (hit == Hit::Video || hit == Hit::ControlBar) {
            model_.pressed = Hit::None;
            return;
        }
    }

    model_.pressed = hit;
    if (hit == Hit::Seek) {
        model_.seekDragging = true;
        model_.seekDragPos = ui_.SeekFraction(x) * s.duration;
        player_.SeekAbsolute(model_.seekDragPos, false);
    } else if (hit == Hit::Volume) {
        volumeDragging_ = true;
        player_.SetVolume(ui_.VolumeFromX(x));
    } else if (hit == Hit::Video && s.fileLoaded) {
        player_.TogglePause();
    }
    RequestRender();
}

void App::OnLButtonUp(float x, float y) {
    int menuIndex = -1;
    const Hit hit = ui_.HitTest(x, y, model_, &menuIndex);

    if (model_.seekDragging) {
        model_.seekDragging = false;
        player_.SeekAbsolute(model_.seekDragPos, true);
    } else if (volumeDragging_) {
        volumeDragging_ = false;
    } else if (hit == model_.pressed && hit != Hit::Video) {
        Activate(hit, menuIndex);
    }
    model_.pressed = Hit::None;
    RequestRender();
}

void App::OnDoubleClick(float x, float y) {
    const Hit hit = ui_.HitTest(x, y, model_, nullptr);
    if (hit == Hit::Video) {
        model_.pressed = Hit::None;
        // The first click of the pair already toggled pause; undo it, like MPC-HC.
        if (player_.State().fileLoaded) player_.TogglePause();
        ToggleFullscreen();
        return;
    }
    OnLButtonDown(x, y);
}

void App::Activate(Hit hit, int menuIndex) {
    const PlayerState& s = player_.State();
    switch (hit) {
        case Hit::Minimize: ShowWindow(hwnd_, SW_MINIMIZE); break;
        case Hit::Maximize: ToggleMaximize(); break;
        case Hit::Close: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case Hit::Open: ShowOpenDialog(); break;
        case Hit::Prev: PlaySibling(-1); break;
        case Hit::Next: PlaySibling(1); break;
        case Hit::Play:
            if (s.fileLoaded)
                player_.TogglePause();
            else
                ShowOpenDialog();
            break;
        case Hit::Mute:
            player_.ToggleMute();
            ShowToast(s.mute ? L"Звук включён" : L"Звук выключен");
            break;
        case Hit::Audio:
        case Hit::Subs: ToggleMenu(hit); break;
        case Hit::Fullscreen: ToggleFullscreen(); break;
        case Hit::MenuItem:
            if (menuIndex >= 0 && menuIndex < static_cast<int>(menu_.items.size())) {
                player_.SetTrack(menu_.prop, menu_.items[static_cast<size_t>(menuIndex)].id);
                pendingTrackToast_ = menu_.anchor == Hit::Audio ? L"audio" : L"sub";
            }
            CloseMenu();
            break;
        default: break;
    }
}

void App::OnKeyDown(WPARAM key) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const PlayerState& s = player_.State();
    switch (key) {
        case VK_SPACE: player_.TogglePause(); break;
        case VK_LEFT:
        case VK_RIGHT: {
            const double step = ctrl ? 30.0 : shift ? 1.0 : 5.0;
            player_.SeekRelative(key == VK_LEFT ? -step : step);
            if (fullscreen_) ShowControls();
            break;
        }
        case VK_UP: ChangeVolume(5); break;
        case VK_DOWN: ChangeVolume(-5); break;
        case 'M':
            player_.ToggleMute();
            ShowToast(s.mute ? L"Звук включён" : L"Звук выключен");
            break;
        case 'F':
        case VK_RETURN: ToggleFullscreen(); break;
        case VK_ESCAPE:
            if (menu_.open)
                CloseMenu();
            else if (fullscreen_)
                ToggleFullscreen();
            break;
        case 'O':
            if (ctrl) ShowOpenDialog();
            break;
        case 'W':
            if (ctrl) player_.Stop();
            break;
        case VK_PRIOR: PlaySibling(-1); break;
        case VK_NEXT: PlaySibling(1); break;
        case 'A':
            player_.CycleTrack("aid");
            pendingTrackToast_ = L"audio";
            break;
        case 'S':
            if (ctrl) {
                if (s.fileLoaded) {
                    player_.Screenshot();
                    ShowToast(L"Скриншот сохранён в «Изображения\\Lumen»");
                }
            } else {
                player_.CycleTrack("sid");
                pendingTrackToast_ = L"sub";
            }
            break;
        case VK_OEM_PERIOD: player_.FrameStep(false); break;
        case VK_OEM_COMMA: player_.FrameStep(true); break;
        case VK_OEM_4: ChangeSpeed(-0.1); break;
        case VK_OEM_6: ChangeSpeed(0.1); break;
        case VK_BACK: ChangeSpeed(0, true); break;
        default: break;
    }
}

void App::OpenFile(const std::wstring& path) {
    CloseMenu();
    if (!model_.engineError.empty()) {
        ShowToast(model_.engineError);
        return;
    }
    if (!model_.engineReady) {
        pendingFile_ = path;
        return;
    }
    fitPending_ = true;
    player_.Open(path);
}

void App::ShowOpenDialog() {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    const COMDLG_FILTERSPEC filters[] = {
        {L"Видео и аудио",
         L"*.mkv;*.mp4;*.m4v;*.avi;*.mov;*.webm;*.wmv;*.flv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.vob;*.ogv;*.3gp;"
         L"*.rmvb;*.mp3;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wav;*.wma"},
        {L"Все файлы", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(L"Открыть файл");
    if (FAILED(dialog->Show(hwnd_))) return;
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        OpenFile(path);
        CoTaskMemFree(path);
    }
}

void App::PlaySibling(int delta) {
    if (siblingIndex_ < 0) return;
    const int i = siblingIndex_ + delta;
    if (i < 0 || i >= static_cast<int>(siblings_.size())) return;
    OpenFile(siblings_[static_cast<size_t>(i)]);
}

void App::ChangeVolume(double delta) {
    if (!model_.engineReady) return;
    const double v = std::clamp(std::round(player_.State().volume + delta), 0.0, 100.0);
    player_.SetVolume(v);
    ShowToast(L"Громкость " + std::to_wstring(static_cast<int>(v)) + L"%");
}

void App::ChangeSpeed(double delta, bool reset) {
    if (!model_.engineReady) return;
    double speed = reset ? 1.0 : std::round((player_.State().speed + delta) * 10.0) / 10.0;
    speed = std::clamp(speed, 0.1, 4.0);
    player_.SetSpeed(speed);
    wchar_t buf[32];
    swprintf_s(buf, L"Скорость %.1f×", speed);
    ShowToast(buf);
}

void App::ToggleMaximize() {
    ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void App::ToggleFullscreen() {
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    if (!fullscreen_) {
        MONITORINFO mi{sizeof(mi)};
        if (!GetWindowPlacement(hwnd_, &windowedPlacement_) ||
            !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi))
            return;
        fullscreen_ = true;
        SetWindowLongW(hwnd_, GWL_STYLE, static_cast<LONG>(style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ShowControls();
    } else {
        fullscreen_ = false;
        KillTimer(hwnd_, kTimerHide);
        barTarget_ = 1.0f;
        model_.barOpacity = 1.0f;
        cursorHidden_ = false;
        SetWindowLongW(hwnd_, GWL_STYLE, static_cast<LONG>(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    Render();
    LayoutChildren();
}

void App::ShowControls() {
    if (cursorHidden_) {
        cursorHidden_ = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    barTarget_ = 1.0f;
    if (!fullscreen_) return;
    StartAnimation();
    SetTimer(hwnd_, kTimerHide, 2000, nullptr);
}

void App::HideControlsIfIdle() {
    KillTimer(hwnd_, kTimerHide);
    if (!fullscreen_) return;
    const bool overControls = model_.hover != Hit::None && model_.hover != Hit::Video;
    if (menu_.open || model_.seekDragging || volumeDragging_ || overControls) {
        SetTimer(hwnd_, kTimerHide, 2000, nullptr);
        return;
    }
    barTarget_ = 0.0f;
    cursorHidden_ = true;
    SetCursor(nullptr);
    StartAnimation();
}

void App::ShowToast(const std::wstring& text) {
    model_.toast = text;
    model_.toastOpacity = 1.0f;
    toastTarget_ = 1.0f;
    SetTimer(hwnd_, kTimerToast, 1200, nullptr);
    RequestRender();
}

void App::StartAnimation() {
    if (animating_) return;
    animating_ = true;
    SetTimer(hwnd_, kTimerAnim, 16, nullptr);
}

void App::StepAnimation() {
    constexpr float kStep = 16.0f / 150.0f;
    auto approach = [](float& v, float target) {
        v = v < target ? std::min(target, v + kStep) : std::max(target, v - kStep);
    };
    approach(model_.barOpacity, barTarget_);
    approach(model_.toastOpacity, toastTarget_);
    if (model_.barOpacity == barTarget_ && model_.toastOpacity == toastTarget_) {
        KillTimer(hwnd_, kTimerAnim);
        animating_ = false;
        if (model_.toastOpacity == 0.0f) model_.toast.clear();
    }
    RequestRender();
}

void App::ToggleMenu(Hit anchor) {
    if (menu_.open && menu_.anchor == anchor) {
        CloseMenu();
        return;
    }
    const bool audio = anchor == Hit::Audio;
    const wchar_t* type = audio ? L"audio" : L"sub";
    menu_.items.clear();
    bool anySelected = false;
    for (const Track& t : player_.State().tracks) {
        if (t.type != type) continue;
        menu_.items.push_back({t.Label(), t.id, t.selected});
        anySelected |= t.selected;
    }
    if (menu_.items.empty()) return;
    menu_.items.insert(menu_.items.begin(), {L"Выкл.", 0, !anySelected});
    menu_.open = true;
    menu_.anchor = anchor;
    menu_.prop = audio ? "aid" : "sid";
    menu_.header = audio ? L"Аудиодорожка" : L"Субтитры";
    menu_.hover = -1;
    RequestRender();
}

void App::CloseMenu() {
    if (!menu_.open) return;
    menu_.open = false;
    menu_.hover = -1;
    RequestRender();
}

void App::ToastSelectedTrack(const wchar_t* type) {
    const bool audio = std::wstring_view(type) == L"audio";
    std::wstring text = audio ? L"Аудио: " : L"Субтитры: ";
    std::wstring label = L"выкл.";
    for (const Track& t : player_.State().tracks)
        if (t.type == type && t.selected) label = t.Label();
    ShowToast(text + label);
}

void App::OnPlayerReady(bool ok) {
    if (!player_.FinishStart(ok)) {
        model_.engineError = player_.Error();
        RequestRender();
        return;
    }
    Trace("mpv ready");
    model_.engineReady = true;
    if (!pendingFile_.empty()) {
        std::wstring file = std::move(pendingFile_);
        pendingFile_.clear();
        OpenFile(file);
    }
    RequestRender();
}

void App::OnPlayerEvents() {
    const unsigned changes = player_.ProcessEvents();
    const PlayerState& s = player_.State();

    if ((changes & kChangeFile) && !s.path.empty()) {
        siblings_ = ListSiblingMedia(s.path);
        siblingIndex_ = -1;
        for (size_t i = 0; i < siblings_.size(); ++i)
            if (_wcsicmp(siblings_[i].c_str(), s.path.c_str()) == 0) siblingIndex_ = static_cast<int>(i);
    }
    if (changes & (kChangeFile | kChangeState)) {
        std::wstring caption = s.fileLoaded && !s.title.empty() ? s.title + L" — Lumen" : L"Lumen";
        SetWindowTextW(hwnd_, caption.c_str());
    }
    if ((changes & kChangeVideoSize) && fitPending_ && s.videoW > 0 && s.videoH > 0) {
        Trace("video size known");
        fitPending_ = false;
        FitWindowToVideo();
    }
    if (changes & kChangeError) ShowToast(s.lastError);
    if (changes & kChangeTracks) {
        if (pendingTrackToast_) {
            ToastSelectedTrack(pendingTrackToast_);
            pendingTrackToast_ = nullptr;
        }
        CloseMenu();
    }

    bool redraw = (changes & ~static_cast<unsigned>(kChangeTime)) != 0;
    if (changes & kChangeTime) {
        const long long second = static_cast<long long>(s.position);
        const D2D1_RECT_F bar = ui_.BarRect();
        const double frac = s.duration > 0 ? s.position / s.duration : 0.0;
        const int px = static_cast<int>((bar.right - bar.left) * Scale() * frac);
        if (second != lastSecond_ || px != lastThumbPx_) {
            lastSecond_ = second;
            lastThumbPx_ = px;
            redraw = true;
        }
    }
    if (redraw) RequestRender();
}

void App::FitWindowToVideo() {
    if (fullscreen_ || IsZoomed(hwnd_)) return;
    const PlayerState& s = player_.State();
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& work = mi.rcWork;
    const double workW = work.right - work.left, workH = work.bottom - work.top;
    const double chrome = (theme::kTitleH + theme::kBarH) * Scale();

    double f = std::min({1.0, workW * 0.8 / static_cast<double>(s.videoW),
                         (workH * 0.8 - chrome) / static_cast<double>(s.videoH)});
    int w = static_cast<int>(std::lround(static_cast<double>(s.videoW) * f));
    int h = static_cast<int>(std::lround(static_cast<double>(s.videoH) * f + chrome));
    w = std::max(w, static_cast<int>(kFitMinWidthDip * Scale()));
    h = std::max(h, static_cast<int>(kMinHeightDip * Scale()));

    RECT cur;
    GetWindowRect(hwnd_, &cur);
    int x = (cur.left + cur.right) / 2 - w / 2;
    int y = (cur.top + cur.bottom) / 2 - h / 2;
    x = std::clamp(x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - w));
    y = std::clamp(y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - h));
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}
