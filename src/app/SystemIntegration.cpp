#include "app/SystemIntegration.h"

#include "Trace.h"
#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/NowPlaying.h"
#include "core/ShellText.h"
#include "gfx/Gpu.h"
#include "gfx/Icons.h"
#include "library/History.h"
#include "library/Thumbnails.h"
#include "ui/Anim.h"
#include "ui/Host.h"

#include <shellapi.h>
#include <shcore.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <systemmediatransportcontrolsinterop.h>

#pragma warning(push)
#pragma warning(disable : 4265 4946 5205 4459 4100)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma warning(pop)

#include <chrono>
#include <functional>
#include <vector>

namespace app {

namespace {

constexpr UINT WM_APP_SMTC = WM_APP + 90;
constexpr UINT WM_APP_TRAY = WM_APP + 91;
constexpr UINT WM_APP_SMTC_SEEK = WM_APP + 92;  // wParam: milliseconds
constexpr UINT kTrayId = 1;
enum TrayCommand : UINT { kTrayShow = 100, kTrayPlay, kTrayPrev, kTrayNext, kTrayQuit };
constexpr UINT kBtnPrev = 1, kBtnPlay = 2, kBtnNext = 3;

namespace wm = winrt::Windows::Media;
namespace wss = winrt::Windows::Storage::Streams;

// The cover or video frame of `path` the thumbnail cache holds already (the library and the
// music view ask for them); null when there is none yet.
std::shared_ptr<const library::ThumbnailBitmap> CachedCover(const std::wstring& path) {
    for (int size : {320, 480, 720, 192, 1024, 96})
        if (auto bmp = TheApp().thumbnails().Cached(path, size); bmp && bmp->width && bmp->height) return bmp;
    return nullptr;
}

// CachedCover as a PNG for the media flyout.
wss::IRandomAccessStream CachedCoverPng(const std::wstring& path) {
    const auto bmp = CachedCover(path);
    if (!bmp) return nullptr;
    gfx::ComPtr<IWICImagingFactory> wic;
    gfx::ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    gfx::ComPtr<IWICBitmapEncoder> encoder;
    gfx::ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (!stream || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(bmp->width, bmp->height)) || FAILED(frame->SetPixelFormat(&format)))
        return nullptr;
    // PNG takes straight BGRA: the same bytes for an opaque cover.
    if (format != GUID_WICPixelFormat32bppPBGRA && format != GUID_WICPixelFormat32bppBGRA) return nullptr;
    if (FAILED(frame->WritePixels(bmp->height, bmp->width * 4, static_cast<UINT>(bmp->pixels.size()),
                                  const_cast<BYTE*>(bmp->pixels.data()))) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return nullptr;
    stream->Seek({}, STREAM_SEEK_SET, nullptr);
    wss::IRandomAccessStream out{nullptr};
    if (FAILED(CreateRandomAccessStreamOverStream(stream.Get(), BSOS_DEFAULT, winrt::guid_of<wss::IRandomAccessStream>(),
                                                  winrt::put_abi(out))))
        return nullptr;
    return out;
}

// What media Play starts while nothing is loaded: the queue's current item, or else the newest
// unfinished history item, as WMP and Spotify continue the last one.
std::wstring ResumePath() {
    App& app = TheApp();
    if (const QueueItem* item = app.queue().CurrentItem()) return item->path;
    for (const library::HistoryEntry& e : app.history().Entries())
        if (e.path.find(L"://") == std::wstring::npos && e.Progress() < 0.95) return e.path;
    return {};
}

winrt::Windows::Foundation::TimeSpan Ticks(double seconds) {
    return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
        std::chrono::duration<double>(std::max(0.0, seconds)));
}

// The taskbar's own mode (not the apps' one): the thumbnail buttons sit on its preview flyout.
bool SystemUsesLightTheme() {
    DWORD light = 0, size = sizeof(light);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS &&
           light != 0;
}

// Draws a px x px HICON with Direct2D.
HICON RenderIcon(int px, const std::function<void(ID2D1DeviceContext5*)>& draw) {
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
    draw(ctx);
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

// A design icon for the thumbnail buttons.
HICON MakeIcon(const char* name, int px, ui::Color glyph) {
    return RenderIcon(px, [&](ID2D1DeviceContext5* ctx) {
        gfx::Icons::Draw(ctx, name, {0, 0, static_cast<float>(px), static_cast<float>(px)}, glyph, glyph);
    });
}

// A cover cut to a square, for the track notification.
HICON CoverIcon(const library::ThumbnailBitmap& cover, int px) {
    return RenderIcon(px, [&](ID2D1DeviceContext5* ctx) {
        gfx::ComPtr<ID2D1Bitmap> bmp;
        const D2D1_BITMAP_PROPERTIES props =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(ctx->CreateBitmap(D2D1::SizeU(cover.width, cover.height), cover.pixels.data(), cover.width * 4, props, &bmp)))
            return;
        const float w = static_cast<float>(cover.width), h = static_cast<float>(cover.height), side = std::min(w, h);
        const D2D1_RECT_F src{(w - side) / 2, (h - side) / 2, (w + side) / 2, (h + side) / 2};
        const D2D1_RECT_F dst{0, 0, static_cast<float>(px), static_cast<float>(px)};
        ctx->DrawBitmap(bmp.Get(), &dst, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &src, nullptr);
    });
}

}  // namespace

struct SystemIntegration::Impl {
    HWND hwnd = nullptr;
    UINT taskbarCreated = 0;
    gfx::ComPtr<ITaskbarList3> taskbar;
    bool buttonsAdded = false;
    HICON prev = nullptr, play = nullptr, pause = nullptr, next = nullptr;
    UINT iconDpi = 0;
    bool lightIcons = false;
    bool shownPlaying = false, shownPrev = true, shownNext = true;
    wm::SystemMediaTransportControls smtc{nullptr};
    winrt::event_token buttonToken{}, seekToken{};
    bool traceSmtc = false;  // --automation with LUMEN_TRACE: no media controls, trace what they would get
    std::wstring smtcPath;   // the item the media flyout shows (empty: disabled)
    bool smtcCover = false;  // ...with its picture
    int smtcFlags = -1;      // previous | next << 1 | shuffle << 2 | repeat << 3, as last sent
    double timelinePos = 0, timelineAt = 0;
    double lastProgress = -1;
    UINT trayCreated = 0;  // "TaskbarCreated": Explorer restarted, add the icon again
    bool trayAdded = false;
    bool trayV4 = false;  // NOTIFYICON_VERSION_4: callbacks carry the icon's anchor in wParam
    std::wstring trayTip, notifiedPath;
    std::wstring notifyPath, notifyText;  // a track notification waiting for its cover
    unsigned notifySerial = 0;

    ~Impl() {
        RemoveTray();
        for (HICON i : {prev, play, pause, next})
            if (i) DestroyIcon(i);
        if (smtc) {
            try {
                smtc.ButtonPressed(buttonToken);
                smtc.PlaybackPositionChangeRequested(seekToken);
                smtc.IsEnabled(false);
            } catch (...) {
            }
        }
    }

    // Glyphs for the taskbar's mode at the window's DPI, made again when either changes.
    bool EnsureIcons() {
        const UINT dpi = GetDpiForWindow(hwnd);
        const bool light = SystemUsesLightTheme();
        if (play && dpi == iconDpi && light == lightIcons) return true;
        const HICON old[] = {prev, play, pause, next};
        iconDpi = dpi;
        lightIcons = light;
        const int px = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const ui::Color color = ui::Color::Hex(light ? 0x000000 : 0xFFFFFF);
        prev = MakeIcon("previous", px, color);
        play = MakeIcon("play", px, color);
        pause = MakeIcon("pause", px, color);
        next = MakeIcon("next", px, color);
        UpdateButtons();  // the taskbar shows the old handles until then
        for (HICON i : old)
            if (i) DestroyIcon(i);
        return play != nullptr;
    }

    // The three thumbnail buttons, with tooltips in the current language.
    void FillButtons(THUMBBUTTON (&b)[3]) const {
        const struct {
            UINT id;
            HICON icon;
            const wchar_t* tip;
            bool enabled;
        } defs[] = {{kBtnPrev, prev, Tr(L"Previous"), shownPrev},
                    {kBtnPlay, shownPlaying ? pause : play, shownPlaying ? Tr(L"Pause") : Tr(L"Play"), true},
                    {kBtnNext, next, Tr(L"Next"), shownNext}};
        for (int i = 0; i < 3; ++i) {
            b[i] = {};
            b[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
            b[i].iId = defs[i].id;
            b[i].hIcon = defs[i].icon;
            wcsncpy_s(b[i].szTip, defs[i].tip, _TRUNCATE);
            b[i].dwFlags = defs[i].enabled ? THBF_ENABLED : THBF_DISABLED;
        }
    }

    void AddButtons() {
        if (!taskbar || buttonsAdded || !EnsureIcons()) return;
        THUMBBUTTON b[3];
        FillButtons(b);
        buttonsAdded = SUCCEEDED(taskbar->ThumbBarAddButtons(hwnd, 3, b));
    }

    void UpdateButtons() {
        if (!buttonsAdded) return;
        THUMBBUTTON b[3];
        FillButtons(b);
        taskbar->ThumbBarUpdateButtons(hwnd, 3, b);
    }

    // Previous / Next as the player's own buttons: disabled with nothing before / after.
    void SyncButtons(bool playing) {
        const bool prevOn = Enabled(Action::Previous), nextOn = Enabled(Action::Next);
        if (playing == shownPlaying && prevOn == shownPrev && nextOn == shownNext) return;
        shownPlaying = playing;
        shownPrev = prevOn;
        shownNext = nextOn;
        UpdateButtons();
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

    // The notification shows the file's cover: it is asked for now and waited for a moment.
    void NotifyTrack(const std::wstring& path, const std::wstring& text) {
        if (!trayAdded || !TheApp().config().GetBool("interface.notifyTrackChange")) return;
        // Only worth a notification when Lumen isn't what the user is looking at.
        if (GetForegroundWindow() == hwnd && !IsIconic(hwnd)) return;
        notifyPath = path;
        notifyText = text;
        const unsigned serial = ++notifySerial;
        TheApp().images().Get(path, 320);
        if (MainWindow* w = TheApp().main())
            w->StartTimer(1000, [this, serial] {
                if (serial == notifySerial) SendNotification();
            });
        if (CachedCover(path)) SendNotification();
    }

    void SendNotification() {
        if (notifyText.empty() || !trayAdded) return;
        NOTIFYICONDATAW nid = TrayData();
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_RESPECT_QUIET_TIME | NIIF_NOSOUND;
        wcscpy_s(nid.szInfoTitle, FitShellText(Tr(L"Now playing"), {}, std::size(nid.szInfoTitle)).c_str());
        wcscpy_s(nid.szInfo, FitShellText(notifyText, {}, std::size(nid.szInfo)).c_str());
        // Without a cover NIIF_USER shows the tray icon.
        const auto cover = CachedCover(notifyPath);
        nid.hBalloonIcon = cover ? CoverIcon(*cover, GetSystemMetricsForDpi(SM_CXICON, GetDpiForWindow(hwnd))) : nullptr;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        if (nid.hBalloonIcon) DestroyIcon(nid.hBalloonIcon);
        notifyText.clear();
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
            // A drag of the flyout's position bar (offered by the timeline's seek range).
            seekToken = smtc.PlaybackPositionChangeRequested(
                [target](auto&&, const wm::PlaybackPositionChangeRequestedEventArgs& args) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(args.RequestedPlaybackPosition());
                    PostMessageW(target, WM_APP_SMTC_SEEK, static_cast<WPARAM>(std::max<long long>(0, ms.count())), 0);
                });
        } catch (...) {
            smtc = nullptr;
        }
    }

    // Previous / Next as the player's own buttons, shuffle and repeat as the queue has them.
    void SyncSmtcFlags() {
        if (smtcPath.empty()) return;
        const PlayQueue& q = TheApp().queue();
        const int flags = (Enabled(Action::Previous) ? 1 : 0) | (Enabled(Action::Next) ? 2 : 0) | (q.Shuffle() ? 4 : 0) |
                          static_cast<int>(q.RepeatMode()) << 3;
        if (flags == smtcFlags) return;
        smtcFlags = flags;
        Trace(("smtc: previous " + std::to_string(flags & 1) + " next " + std::to_string((flags >> 1) & 1) +
               " shuffle " + std::to_string((flags >> 2) & 1) + " repeat " + std::to_string(flags >> 3))
                  .c_str());
        if (!smtc) return;
        try {
            smtc.IsPreviousEnabled((flags & 1) != 0);
            smtc.IsNextEnabled((flags & 2) != 0);
            smtc.ShuffleEnabled((flags & 4) != 0);
            smtc.AutoRepeatMode(q.RepeatMode() == Repeat::One   ? wm::MediaPlaybackAutoRepeatMode::Track
                                : q.RepeatMode() == Repeat::All ? wm::MediaPlaybackAutoRepeatMode::List
                                                                : wm::MediaPlaybackAutoRepeatMode::None);
        } catch (...) {
        }
    }

    // The flyout moves its position bar on by itself while playing: sent on a file, a pause or
    // play, and when the position jumps (a seek) or stalls.
    void SyncTimeline(const PlayerState& s, bool force) {
        const double now = ui::AnimFloat::Now();
        const double expected = timelinePos + (s.paused ? 0.0 : (now - timelineAt) / 1000.0 * s.speed);
        if (!force && std::abs(s.position - expected) < 1.5) return;
        timelinePos = s.fileLoaded ? s.position : 0;
        timelineAt = now;
        const double end = s.fileLoaded ? std::max(0.0, s.duration) : 0;
        char line[80];
        snprintf(line, sizeof(line), "smtc: timeline %.1f / %.1f", timelinePos, end);
        Trace(line);
        if (!smtc) return;
        try {
            wm::SystemMediaTransportControlsTimelineProperties t;
            t.StartTime(Ticks(0));
            t.MinSeekTime(Ticks(0));
            t.EndTime(Ticks(end));
            t.MaxSeekTime(Ticks(s.seekable ? end : 0));
            t.Position(Ticks(timelinePos));
            smtc.UpdateTimelineProperties(t);
        } catch (...) {
        }
    }

    // The flyout's picture of smtcPath, once the thumbnail cache has one.
    void ShowCover() {
        if (smtcCover || smtcPath.empty()) return;
        try {
            const wss::IRandomAccessStream png = CachedCoverPng(smtcPath);
            if (!png) return;
            smtcCover = true;
            Trace(("smtc: cover, png of " + std::to_string(png.Size()) + " bytes").c_str());
            if (!smtc) return;
            auto updater = smtc.DisplayUpdater();
            updater.Thumbnail(wss::RandomAccessStreamReference::CreateFromStream(png));
            updater.Update();
        } catch (...) {
        }
    }
};

SystemIntegration::SystemIntegration() : impl_(std::make_unique<Impl>()) {}
SystemIntegration::~SystemIntegration() = default;

void SystemIntegration::Attach(HWND window) {
    impl_->hwnd = window;
    TheApp().images().ready.Connect([this] {  // both live as long as the app
        impl_->ShowCover();
        if (!impl_->notifyText.empty() && CachedCover(impl_->notifyPath)) impl_->SendNotification();
    });
    if (ui::Host::Automation()) {
        impl_->traceSmtc = GetEnvironmentVariableW(L"LUMEN_TRACE", nullptr, 0) > 0;
        return;
    }
    impl_->taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    impl_->trayCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // Let the (non-elevated or elevated) shell deliver it either way.
    ChangeWindowMessageFilterEx(window, impl_->taskbarCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, impl_->trayCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, WM_APP_TRAY, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(window, WM_COMMAND, MSGFLT_ALLOW, nullptr);
    impl_->InitSmtc();
    impl_->SyncTray();
    // Items added to or removed from the queue enable Previous / Next. Both live as long as the app.
    TheApp().queue().changed.Connect([this] { Update(0); });
}

void SystemIntegration::LanguageChanged() {
    impl_->UpdateButtons();
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
        if (msg == WM_APP_TRAY || msg == WM_APP_SMTC || msg == WM_APP_SMTC_SEEK ||
            (msg == WM_COMMAND && HIWORD(wp) == THBN_CLICKED)) {
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
    if ((msg == WM_SETTINGCHANGE && lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) ||
        msg == WM_DPICHANGED) {
        if (d.buttonsAdded) d.EnsureIcons();
        return false;
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
        App& app = TheApp();
        const PlayerState& s = app.player().State();
        switch (static_cast<B>(wp)) {
            case B::Play:
                if (!s.fileLoaded && !app.queue().CurrentItem()) {
                    if (const std::wstring path = ResumePath(); !path.empty()) app.Open({path});
                } else if (s.paused || !s.fileLoaded) {
                    Run(Action::PlayPause);
                }
                break;
            case B::Pause: if (!s.paused) Run(Action::PlayPause); break;
            case B::Stop: Run(Action::Stop); break;
            case B::Next: Run(Action::Next); break;
            case B::Previous: Run(Action::Previous); break;
            default: break;
        }
        *result = 0;
        return true;
    }
    if (msg == WM_APP_SMTC_SEEK) {
        const PlayerState& s = TheApp().player().State();
        if (s.fileLoaded && s.seekable) TheApp().player().SeekAbsolute(static_cast<double>(wp) / 1000.0, true);
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
    d.SyncButtons(s.fileLoaded && !s.paused);

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

    if (!d.smtcPath.empty() && s.fileLoaded && (changes & kChangeTime)) d.SyncTimeline(s, false);
    d.SyncSmtcFlags();

    if (!(changes & (kChangeState | kChangeFile | kChangeMeta))) return;
    // The tags as the player screens show them (the library's reader first, then mpv's).
    const PlayingTags& tags = CurrentTags();
    const QueueItem* item = app.queue().CurrentItem();
    const std::wstring& title = tags.title.empty() && item ? FileNameOf(item->path) : tags.title;

    // Tray tooltip and track notification (once per file).
    if (changes & (kChangeFile | kChangeMeta)) {
        d.SetTrayTip(s.fileLoaded && !title.empty() ? FitShellText(title, L" вЂ” Lumen", std::size(NOTIFYICONDATAW{}.szTip))
                                                     : L"Lumen");
        if ((changes & kChangeFile) && s.fileLoaded && item && item->path != d.notifiedPath) {
            d.notifiedPath = item->path;
            d.NotifyTrack(item->path, tags.artist.empty() ? title : title + L"\n" + tags.artist);
        }
    }

    // Media transport controls. With nothing loaded they offer the item media Play resumes, so
    // the media keys work from the start.
    if (!d.smtc && !d.traceSmtc) return;
    try {
        const std::wstring path = s.fileLoaded ? (item ? item->path : s.path) : ResumePath();
        const bool enabled = !path.empty() && app.config().GetBool("interface.mediaControls");
        if (d.smtc) d.smtc.IsEnabled(enabled);
        if (!enabled) {
            if (!d.smtcPath.empty()) Trace("smtc: disabled");
            d.smtcPath.clear();
            d.smtcFlags = -1;
            return;
        }
        if (d.smtc)
            d.smtc.PlaybackStatus(s.fileLoaded && !s.paused ? wm::MediaPlaybackStatus::Playing
                                                            : wm::MediaPlaybackStatus::Paused);
        if ((changes & (kChangeFile | kChangeMeta)) || path != d.smtcPath) {
            std::wstring shown = title, artist = tags.artist, album = tags.album;
            bool video = s.hasVideo;
            if (!s.fileLoaded) {
                const library::HistoryEntry* h = app.history().Find(path);
                shown = h && !h->title.empty()                              ? h->title
                        : item && item->path == path && !item->title.empty() ? item->title
                                                                             : FileNameOf(path);
                artist.clear();
                album.clear();
                video = library::KindOf(path) != library::MediaKind::Audio;
            }
            const bool newItem = path != d.smtcPath;
            d.smtcPath = path;
            if (newItem) d.smtcCover = false;
            Trace(("smtc: " + std::string(video ? "video " : "music ") + WideToUtf8(shown) +
                   (artist.empty() ? "" : " / " + WideToUtf8(artist)) + (s.fileLoaded ? "" : " (to resume)"))
                      .c_str());
            if (d.smtc) {
                auto updater = d.smtc.DisplayUpdater();
                if (video) {
                    updater.Type(wm::MediaPlaybackType::Video);
                    updater.VideoProperties().Title(shown);
                } else {
                    updater.Type(wm::MediaPlaybackType::Music);
                    updater.MusicProperties().Title(shown);
                    updater.MusicProperties().Artist(artist);
                    updater.MusicProperties().AlbumTitle(album);
                }
                if (newItem) updater.Thumbnail(nullptr);  // not the previous item's picture
                updater.Update();
            }
            d.ShowCover();
            d.SyncSmtcFlags();
        }
        d.SyncTimeline(s, true);
    } catch (...) {
    }
}

}  // namespace app
