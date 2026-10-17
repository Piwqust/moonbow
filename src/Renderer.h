#pragma once
#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <thread>

using Microsoft::WRL::ComPtr;

// Owns the UI layer: a premultiplied-alpha composition swapchain that
// DirectComposition shows on top of the window's child windows (where mpv
// renders video). Direct2D draws into it in DIPs.
class Renderer {
public:
    ~Renderer();

    // Cheap, device-independent factories (D2D, DirectWrite). Call first.
    bool CreateFactories();
    // Creates the D3D11 device on a worker thread, then posts doneMessage.
    void CreateDeviceAsync(HWND notifyWnd, UINT doneMessage);
    // Call on the UI thread after doneMessage: swapchain, D2D and DComp.
    bool FinishInit(HWND hwnd, float dpi);
    bool Ready() const { return ctx_ != nullptr; }

    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);

    // Returns nullptr when the window has no area to draw into.
    ID2D1DeviceContext* Begin();
    // Returns false when the device was lost and resources were recreated.
    bool End();

    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }
    bool HasFontFamily(const wchar_t* family) const;
    ComPtr<IDWriteTextFormat> CreateFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                                           DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT valign,
                                           bool ellipsis) const;

private:
    bool CreateDeviceResources();
    void ReleaseDeviceResources();
    bool CreateTarget();

    HWND hwnd_ = nullptr;
    UINT width_ = 1;
    UINT height_ = 1;
    float dpi_ = 96.0f;
    bool drawing_ = false;
    std::thread deviceThread_;
    ComPtr<ID3D11Device> pendingDevice_;

    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> ctx_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDCompositionDevice> dcomp_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> visual_;
};
