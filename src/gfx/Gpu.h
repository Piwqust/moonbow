#pragma once
#include <windows.h>

#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <thread>

namespace gfx {

using Microsoft::WRL::ComPtr;

// Process-wide graphics objects shared by every window: one D3D11 device,
// one D2D device context (UI thread only), one DirectComposition device.
class Gpu {
public:
    static Gpu& Get();

    // Device-independent factories (D2D, DirectWrite, WIC). Cheap; call first.
    bool CreateFactories();
    // The hardware D3D11 device takes ~150 ms to create (the driver loads), so StartDevice() (at
    // process start) creates it on a worker thread, right after a WARP (software) device that is
    // ready in a few ms: the first frames are drawn with WARP, then everything moves to
    // the hardware device. NotifyWhenReady() posts `doneMessage` to the window for each device
    // that becomes available (at once for one that already is); the window then calls
    // FinishDevice() on the UI thread.
    void StartDevice();
    void NotifyWhenReady(HWND notifyWnd, UINT doneMessage);
    void CreateDeviceAsync(HWND notifyWnd, UINT doneMessage) {
        StartDevice();
        NotifyWhenReady(notifyWnd, doneMessage);
    }
    // On the UI thread after doneMessage: switches to the best device available so far (WARP, then
    // hardware) and builds the D2D device/context and DComp device. True when the device changed;
    // surfaces then rebuild through Generation().
    bool FinishDevice();
    // Both workers are done and no device could be created.
    bool DeviceFailed();
    bool Ready() const { return ctx_ != nullptr; }

    // Recreates all device objects after DXGI device removal. Surfaces notice
    // through Generation() and rebuild themselves.
    bool RecreateDevice();
    uint32_t Generation() const { return generation_; }

    ID3D11Device* D3D() const { return d3d_.Get(); }
    IDXGIFactory2* Dxgi() const { return dxgi_.Get(); }
    IDXGIDevice* DxgiDevice() const { return dxgiDevice_.Get(); }
    ID2D1Factory6* D2DFactory() const { return d2dFactory_.Get(); }
    ID2D1DeviceContext5* Ctx() const { return ctx_.Get(); }
    IDCompositionDevice* DComp() const { return dcomp_.Get(); }
    IDWriteFactory5* DWrite() const { return dwrite_.Get(); }

    ~Gpu();

private:
    bool CreateDeviceObjects();
    // keepComposition: the WARP → hardware switch keeps the DComp device, so windows keep their
    // composition target and swap their content in without a frame of bare background.
    void ReleaseDeviceObjects(bool keepComposition = false);

    void PostReady();  // notifyMutex_ held

    std::thread deviceThread_;
    std::mutex notifyMutex_;  // guards everything below up to usingHardware_
    HWND notifyWnd_ = nullptr;
    UINT notifyMsg_ = 0;
    bool hardwareDone_ = false, warpDone_ = false;
    ComPtr<ID3D11Device> hardwareDevice_, warpDevice_;
    bool usingHardware_ = false;
    uint32_t generation_ = 0;

    ComPtr<ID2D1Factory6> d2dFactory_;
    ComPtr<IDWriteFactory5> dwrite_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIFactory2> dxgi_;
    ComPtr<ID2D1Device5> d2dDevice_;
    ComPtr<ID2D1DeviceContext5> ctx_;
    ComPtr<IDCompositionDevice> dcomp_;
};

// One window's drawing target: a premultiplied composition swapchain shown by
// DirectComposition above the window's child windows (where mpv draws video).
class Surface {
public:
    bool Attach(HWND hwnd, float dpi);
    void Detach();
    // True when a frame drawn ahead for this size (BeginAhead) is now shown: nothing to redraw.
    bool Resize(UINT width, UINT height);
    void SetDpi(float dpi);

    // nullptr when there is nothing to draw (no device yet, zero size).
    ID2D1DeviceContext5* Begin();
    // false when the device was lost; the caller should simply redraw. `now`: shown at the next
    // composition, not after the next vblank (a frame for a new window size, which DWM shows at once).
    bool End(bool now = false);
    // Draws a frame for a size the window is about to get, before it gets it (Host::SwitchSizeAhead):
    // Resize to that size then shows it in the same commit as the new size, never a frame late.
    // End presents it unseen. nullptr when it can't (no device, no visual yet, a device switch).
    ID2D1DeviceContext5* BeginAhead(UINT width, UINT height);
    void DropAhead();  // the size it was drawn for never came
    // A new device's swap chain waits for its second frame before it replaces the old one on
    // screen: the caller draws again soon.
    bool Switching() const { return switchFrames_ > 0; }

    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

private:
    bool Build();
    bool BuildTarget();
    void Drop();

    HWND hwnd_ = nullptr;
    UINT width_ = 1, height_ = 1;
    float dpi_ = 96.0f;
    uint32_t generation_ = 0;
    bool drawing_ = false;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDCompositionDevice> dcomp_;     // the device visual_ belongs to
    ComPtr<IDXGISwapChain1> oldSwap_;       // still on screen while switchFrames_ > 0
    int switchFrames_ = 0;                  // frames to present before swap_ replaces oldSwap_
    ComPtr<IDXGISwapChain1> ahead_;         // BeginAhead's frame, waiting for its size
    ComPtr<ID2D1Bitmap1> aheadTarget_;
    UINT aheadW_ = 0, aheadH_ = 0;
    bool drawingAhead_ = false;
};

}  // namespace gfx
