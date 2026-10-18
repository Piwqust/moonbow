#include "app/SystemIntegration.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "core/ShellText.h"
#include "gfx/Gpu.h"
#include "gfx/Icons.h"
#include "ui/Host.h"

#include <shellapi.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <systemmediatransportcontrolsinterop.h>

#pragma warning(push)
#pragma warning(disable : 4265 4946 5205 4459 4100)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#pragma warning(pop)

#include <vector>

namespace app {

namespace {

constexpr UINT WM_APP_SMTC = WM_APP + 90;
constexpr UINT WM_APP_TRAY = WM_APP + 91;
constexpr UINT kTrayId = 1;
enum TrayCommand : UINT { kTrayShow = 100, kTrayPlay, kTrayPrev, kTrayNext, kTrayQuit };
constexpr UINT kBtnPrev = 1, kBtnPlay = 2, kBtnNext = 3;

namespace wm = winrt::Windows::Media;

// Renders a design icon into an HICON (white, for the dark taskbar flyout).
HICON MakeIcon(const char* name, int px) {
    gfx::Gpu& gpu = gfx::Gpu::Get();
    if (!gpu.Ready()) return nullptr;
    ID2D1DeviceContext5* ctx = gpu.Ctx();
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(px), static_cast<UINT32>(px));
    const D2D1_PIXEL_FORMAT fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    gfx::ComPtr<ID2D1Bitmap1> target, readback;
    D2D1_BITMAP_PROPERTIES1 tp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt, 96, 96);
    D2D1_BITMAP_PROPERTIES1 rp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt, 96, 96);
    if (FAILED(ctx->CreateBitmap(size, nullptr, 0, tp, &target)) || FAILED(ctx->CreateBitmap(size, nullptr, 0, rp, &readback)))
        return nullptr;
    gfx::ComPtr<ID2D1Image> old;
    ctx->GetTarget(&old);
    float dx = 96, dy = 96;
    ctx->GetDpi(&dx, &dy);
    ctx->SetTarget(target.Get());
    ctx->SetDpi(96, 96);
    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
    const ui::Color white = ui::Color::Hex(0xFFFFFF);
    gfx::Icons::Draw(ctx, name, {0, 0, static_cast<float>(px), static_cast<float>(px)}, white, white);
    const HRESULT hr = ctx->EndDraw();
    ctx->SetTarget(old.Get());
    ctx->SetDpi(dx, dy);
    if (FAILED(hr) || FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return nullptr;

    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return nullptr;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = px;
    bi.bV5Height = -px;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (color && bits)
        for (int y = 0; y < px; ++y)
            memcpy(static_cast<BYTE*>(bits) + static_cast<size_t>(y) * px * 4, mapped.bits + static_cast<size_t>(y) * mapped.pitch,
                   static_cast<size_t>(px) * 4);
    readback->Unmap();
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = color ? CreateIconIndirect(&ii) : nullptr;
    if (mask) DeleteObject(mask);
    if (color) DeleteObject(color);
    return icon;
}

}  // namespace

struct SystemIntegration::Impl {
    HWND hwnd = nullptr;
    UINT taskbarCreated = 0;
    gfx::ComPtr<ITaskbarList3> taskbar;
    bool buttonsAdded = false;
    HICON prev = nullptr, play = nullptr, pause = nullptr, next = nullptr;
    bool shownPlaying = false;
    wm::SystemMediaTransportControls smtc{nullptr};
    winrt::event_token buttonToken{};
    double lastProgress = -1;
    UINT trayCreated = 0;  // "TaskbarCreated": Explorer restarted, add the icon again
    bool trayAdded = false;
    bool trayV4 = false;  // NOTIFYICON_VERSION_4: callbacks carry the icon's anchor in wParam
    std::wstring trayTip, notifiedPath;

    ~Impl() {
        RemoveTray();
        for (HICON i : {prev, play, pause, next})
            if (i) DestroyIcon(i);
        if (smtc) {
            try {
                smtc.ButtonPressed(buttonToken);
                smtc.IsEnabled(false);
            } catch (...) {
            }
        }
    }

    bool EnsureIcons() {
        if (play) return true;
        const int px = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(hwnd));
        prev = MakeIcon("previous", px);
        play = MakeIcon("play", px);
        pause = MakeIcon("pause", px);
        next = MakeIcon("next", px);
        return play != nullptr;
    }

    void AddButtons() {
        if (!taskbar || buttonsAdded || !EnsureIcons()) return;
        THUMBBUTTON b[3]{};
        const struct {
            UINT id;
            HICON icon;
            const wchar_t* tip;
        } defs[] = {{kBtnPrev, prev, L"Previous"}, {kBtnPlay, play, L"Play"}, {kBtnNext, next, L"Next"}};
        for (int i = 0; i < 3; ++i) {
            b[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
            b[i].iId = defs[i].id;
            b[i].hIcon = defs[i].icon;
            wcscpy_s(b[i].szTip, defs[i].tip);
            b[i].dwFlags = THBF_ENABLED;
        }
        buttonsAdded = SUCCEEDED(taskbar->ThumbBarAddButtons(hwnd, 3, b));
    }

    void UpdatePlayButton(bool playing) {
        if (!buttonsAdded || playing == shownPlaying) return;
        shownPlaying = playing;
        THUMBBUTTON b{};
        b.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        b.iId = kBtnPlay;
        b.hIcon = playing ? pause : play;
        wcscpy_s(b.szTip, playing ? L"Pause" : L"Play");
        b.dwFlags = THBF_ENABLED;
        taskbar->ThumbBarUpdateButtons(hwnd, 1, &b);
    }

    // The notification needs the icon too, so either setting shows it.
    static bool WantsTray() {
        const Config& c = TheApp().config();
        return c.GetBool("interface.trayIcon") || c.GetBool("interface.notifyTrackChange");
    }

    NOTIFYICONDATAW TrayData() const {
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd = hwnd;
        nid.uID = kTrayId;
        return nid;
    }

    void SyncTray() {
        if (!WantsTray() || ui::Host::Automation()) {
            RemoveTray();
            return;
        }
        if (trayAdded) return;
        NOTIFYICONDATAW nid = TrayData();
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_APP_TRAY;
        const int px = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(hwnd));
        nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, 0));
        wcscpy_s(nid.szTip, trayTip.empty() ? L"Lumen" : trayTip.c_str());
        trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (trayAdded) {
            nid.uVersion = NOTIFYICON_VERSION_4;
            trayV4 = Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE;
        }
        if (nid.hIcon) DestroyIcon(nid.hIcon);
    }

    void RemoveTray() {
        if (!trayAdded) return;
        NOTIFYICONDATAW nid = TrayData();
        Shell_NotifyIconW(NIM_DELETE, &nid);
        trayAdded = false;
    }

    // `tip` fits szTip (FitShellText).
    void SetTrayTip(const std::wstring& tip) {
        if (tip == trayTip) return;
        trayTip = tip;
        if (!trayAdded) return;
        NOTIFYICONDATAW nid = TrayData();
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcscpy_s(nid.szTip, tip.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void NotifyTrack(const std::wstring& text) {
        if (!trayAdded || !TheApp().config().GetBool("interface.notifyTrackChange")) return;
        // Only worth a notification when Lumen isn't what the user is looking at.
        if (GetForegroundWindow() == hwnd && !IsIconic(hwnd)) return;
        NOTIFYICONDATAW nid = TrayData();
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_RESPECT_QUIET_TIME | NIIF_NOSOUND;
        wcscpy_s(nid.szInfoTitle, FitShellText(Tr(L"Now playing"), {}, std::size(nid.szInfoTitle)).c_str());
        wcscpy_s(nid.szInfo, FitShellText(text, {}, std::size(nid.szInfo)).c_str());
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void ShowWindowFromTray() const {
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }

    // `anchor`: where the shell asks for the menu (the icon, also for Shift+F10 / the menu key).
    void TrayMenu(POINT anchor) {
        const PlayerState& s = TheApp().player().State();
        HMENU menu = CreatePopupMenu();
        const bool playing = s.fileLoaded && !s.paused;
        const UINT media = MF_STRING | (s.fileLoaded ? 0u : static_cast<UINT>(MF_GRAYED));
        AppendMenuW(menu, MF_STRING, kTrayShow, Tr(L"Show Lumen"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, media, kTrayPlay, playing ? Tr(L"Pause") : Tr(L"Play"));
        AppendMenuW(menu, media, kTrayPrev, Tr(L"Previous"));
        AppendMenuW(menu, media, kTrayNext, Tr(L"Next"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayQuit, Tr(L"Quit"));
        SetMenuDefaultItem(menu, kTrayShow, FALSE);
        SetForegroundWindow(hwnd);  // so the menu closes when clicking elsewhere
        const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
            menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, anchor.x, anchor.y, hwnd, nullptr));
        PostMessageW(hwnd, WM_NULL, 0, 0);  // the menu opens again on the next click (KB135788)
        DestroyMenu(menu);
        switch (cmd) {
            case kTrayShow: ShowWindowFromTray(); break;
            case kTrayPlay: Run(Action::PlayPause); break;
            case kTrayPrev: Run(Action::Previous); break;
            case kTrayNext: Run(Action::Next); break;
            case kTrayQuit: TheApp().Quit(); break;
            default: break;
        }
    }

    void InitSmtc() {
        try {
            auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(interop->GetForWindow(hwnd, winrt::guid_of<wm::SystemMediaTransportControls>(),
                                                       winrt::put_abi(smtc)));
            smtc.IsPlayEnabled(true);
            smtc.IsPauseEnabled(true);
            smtc.IsStopEnabled(true);
            smtc.IsNextEnabled(true);
            smtc.IsPreviousEnabled(true);
            smtc.IsEnabled(false);
            HWND target = hwnd;
            buttonToken = smtc.ButtonPressed([target](auto&&, const wm::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                // Raised on a worker thread: hand it to the UI thread.
                PostMessageW(target, WM_APP_SMTC, static_cast<WPARAM>(args.Button()), 0);
            });
        } catch (...) {
            smtc = nullptr;
        }
    }
};

SystemIntegration::SystemIntegration() : impl_(std::make_unique<Impl>()) {}
SystemIntegration::~SystemIntegration() = default;

void SystemIntegration::Attach(HWND window) {
    impl_->hwnd = window;
    if (ui::Host::Automation()) return;
    impl_->taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    impl_->trayCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // Let the (non-elevated or elevated) shell deliver it either way.
    ChangeWindowMessageFilterEx(window, impl_->taskbarCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, impl_->trayCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, WM_COMMAND, MSGFLT_ALLOW, nullptr);
    impl_->InitSmtc();
    impl_->SyncTray();
}

void SystemIntegration::SettingsChanged() {
    Impl& d = *impl_;
    d.SyncTray();
    d.lastProgress = -2;  // force a taskbar progress refresh
    if (d.taskbar && !TheApp().config().GetBool("interface.taskbarProgress"))
        d.taskbar->SetProgressState(d.hwnd, TBPF_NOPROGRESS);
    Update(kChangeState | kChangeMeta);
}

bool SystemIntegration::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    Impl& d = *impl_;
    // Closing: the window is hidden and mpv is being destroyed on another thread. The tray icon
    // goes at once ("Show Lumen" would bring back a dying window), and no tray, thumbnail-button
    // or media-key command reaches the player.
    if (TheApp().Quitting()) {
        d.RemoveTray();
        if (msg == WM_APP_TRAY || msg == WM_APP_SMTC || (msg == WM_COMMAND && HIWORD(wp) == THBN_CLICKED)) {
            *result = 0;
            return true;
        }
        return false;
    }
    if (msg == d.trayCreated && d.trayCreated) {
        d.trayAdded = false;
        d.SyncTray();
        return false;
    }
    if (msg == WM_APP_TRAY) {
        switch (LOWORD(lp)) {
            case NIN_SELECT:
            case NIN_KEYSELECT:
                if (IsWindowVisible(d.hwnd) && !IsIconic(d.hwnd) && GetForegroundWindow() == d.hwnd)
                    ShowWindow(d.hwnd, SW_MINIMIZE);
                else
                    d.ShowWindowFromTray();
                break;
            case WM_CONTEXTMENU: {
                POINT at{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
                if (!d.trayV4) GetCursorPos(&at);
                d.TrayMenu(at);
                break;
            }
            default: break;
        }
        *result = 0;
        return true;
    }
    // Minimize to tray: hide the taskbar button while minimized.
    if (msg == WM_SIZE && wp == SIZE_MINIMIZED && d.trayAdded && TheApp().config().GetBool("interface.minimizeToTray")) {
        ShowWindow(d.hwnd, SW_HIDE);
        return false;
    }
    if (msg == d.taskbarCreated && d.taskbarCreated) {
        if (!d.taskbar && FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d.taskbar))))
            d.taskbar = nullptr;
        if (d.taskbar) d.taskbar->HrInit();
        d.buttonsAdded = false;
        d.AddButtons();
        *result = 0;
        return true;
    }
    if (msg == WM_COMMAND && HIWORD(wp) == THBN_CLICKED) {
        switch (LOWORD(wp)) {
            case kBtnPrev: Run(Action::Previous); break;
            case kBtnPlay: Run(Action::PlayPause); break;
            case kBtnNext: Run(Action::Next); break;
            default: return false;
        }
        *result = 0;
        return true;
    }
    if (msg == WM_APP_SMTC) {
        using B = wm::SystemMediaTransportControlsButton;
        const PlayerState& s = TheApp().player().State();
        switch (static_cast<B>(wp)) {
            case B::Play: if (s.paused || !s.fileLoaded) Run(Action::PlayPause); break;
            case B::Pause: if (!s.paused) Run(Action::PlayPause); break;
            case B::Stop: Run(Action::Stop); break;
            case B::Next: Run(Action::Next); break;
            case B::Previous: Run(Action::Previous); break;
            default: break;
        }
        *result = 0;
        return true;
    }
    return false;
}

void SystemIntegration::Update(unsigned changes) {
    Impl& d = *impl_;
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    if (d.taskbar && !d.buttonsAdded) d.AddButtons();
    d.UpdatePlayButton(s.fileLoaded && !s.paused);

    // Taskbar progress.
    if (d.taskbar && app.config().GetBool("interface.taskbarProgress")) {
        if (!s.fileLoaded || s.duration <= 0) {
            if (d.lastProgress != -1) d.taskbar->SetProgressState(d.hwnd, TBPF_NOPROGRESS);
            d.lastProgress = -1;
        } else {
            const double k = std::clamp(s.position / s.duration, 0.0, 1.0);
            if (std::abs(k - d.lastProgress) > 0.002 || (changes & kChangeState)) {
                d.taskbar->SetProgressState(d.hwnd, s.paused ? TBPF_PAUSED : TBPF_NORMAL);
                d.taskbar->SetProgressValue(d.hwnd, static_cast<ULONGLONG>(k * 10000), 10000);
                d.lastProgress = k;
            }
        }
    }

    // Tray tooltip and track notification (once per file).
    if (changes & (kChangeFile | kChangeMeta)) {
        const QueueItem* item = app.queue().CurrentItem();
        std::wstring title = s.title.empty() && item ? FileNameOf(item->path) : s.title;
        d.SetTrayTip(s.fileLoaded && !title.empty() ? FitShellText(title, L" — Lumen", std::size(NOTIFYICONDATAW{}.szTip))
                                                     : L"Lumen");
        if ((changes & kChangeFile) && s.fileLoaded && item && item->path != d.notifiedPath) {
            d.notifiedPath = item->path;
            d.NotifyTrack(s.artist.empty() ? title : title + L"\n" + s.artist);
        }
    }

    // Media transport controls.
    if (!d.smtc || !(changes & (kChangeState | kChangeFile | kChangeMeta))) return;
    try {
        const bool enabled = s.fileLoaded && app.config().GetBool("interface.mediaControls");
        d.smtc.IsEnabled(enabled);
        if (!enabled) return;
        d.smtc.PlaybackStatus(s.paused ? wm::MediaPlaybackStatus::Paused : wm::MediaPlaybackStatus::Playing);
        if (changes & (kChangeFile | kChangeMeta)) {
            auto updater = d.smtc.DisplayUpdater();
            const QueueItem* item = app.queue().CurrentItem();
            std::wstring title = s.title.empty() && item ? FileNameOf(item->path) : s.title;
            if (s.hasVideo) {
                updater.Type(wm::MediaPlaybackType::Video);
                updater.VideoProperties().Title(title);
            } else {
                updater.Type(wm::MediaPlaybackType::Music);
                updater.MusicProperties().Title(title);
                updater.MusicProperties().Artist(s.artist);
                updater.MusicProperties().AlbumTitle(s.album);
            }
            updater.Update();
        }
    } catch (...) {
    }
}

}  // namespace app
