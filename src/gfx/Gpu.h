#pragma once
#include <windows.h>

#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
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
    // Creates the D3D11 device on a worker thread (driver load is slow), then
    // posts doneMessage to notifyWnd.
    void CreateDeviceAsync(HWND notifyWnd, UINT doneMessage);
    // On the UI thread after doneMessage: D2D device/context and DComp device.
    bool FinishDevice();
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
    IWICImagingFactory* Wic() const { return wic_.Get(); }

    ~Gpu();

private:
    bool CreateDeviceObjects();
    void ReleaseDeviceObjects();

    std::thread deviceThread_;
    ComPtr<ID3D11Device> pendingDevice_;
    uint32_t generation_ = 0;

    ComPtr<ID2D1Factory6> d2dFactory_;
    ComPtr<IDWriteFactory5> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
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
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);

    // nullptr when there is nothing to draw (no device yet, zero size).
    ID2D1DeviceContext5* Begin();
    // false when the device was lost; the caller should simply redraw.
    bool End();

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
};

}  // namespace gfx
