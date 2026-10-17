#include "Renderer.h"

#include <algorithm>

namespace {

// Loading the GPU driver dominates this call (~150 ms on a cold start), so it
// runs on a worker thread while the window is already on screen.
ComPtr<ID3D11Device> CreateD3DDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, nullptr);
    return SUCCEEDED(hr) ? device : nullptr;
}

}  // namespace

Renderer::~Renderer() {
    if (deviceThread_.joinable()) deviceThread_.join();
}

bool Renderer::CreateFactories() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf()))))
        return false;
    return SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())));
}

void Renderer::CreateDeviceAsync(HWND notifyWnd, UINT doneMessage) {
    deviceThread_ = std::thread([this, notifyWnd, doneMessage] {
        pendingDevice_ = CreateD3DDevice();
        PostMessageW(notifyWnd, doneMessage, 0, 0);
    });
}

bool Renderer::FinishInit(HWND hwnd, float dpi) {
    if (deviceThread_.joinable()) deviceThread_.join();
    hwnd_ = hwnd;
    dpi_ = dpi;
    RECT rc;
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    height_ = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    d3d_ = std::move(pendingDevice_);
    return CreateDeviceResources();
}

bool Renderer::CreateDeviceResources() {
    if (!d3d_) d3d_ = CreateD3DDevice();
    if (!d3d_) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(d3d_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(d3d_.Get(), &desc, nullptr, &swap_))) return false;

    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)) ||
        FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_)))
        return false;
    ctx_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // topmost = TRUE: the visual tree is composed above child windows,
    // i.e. above the video that mpv renders into its own child HWND.
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp_))) ||
        FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_)) || FAILED(dcomp_->CreateVisual(&visual_)) ||
        FAILED(visual_->SetContent(swap_.Get())) || FAILED(dcompTarget_->SetRoot(visual_.Get())) ||
        FAILED(dcomp_->Commit()))
        return false;

    return CreateTarget();
}

void Renderer::ReleaseDeviceResources() {
    if (ctx_) ctx_->SetTarget(nullptr);
    target_.Reset();
    visual_.Reset();
    dcompTarget_.Reset();
    dcomp_.Reset();
    ctx_.Reset();
    d2dDevice_.Reset();
    swap_.Reset();
    d3d_.Reset();
}

bool Renderer::CreateTarget() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
    if (FAILED(ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_))) return false;
    ctx_->SetTarget(target_.Get());
    ctx_->SetDpi(dpi_, dpi_);
    return true;
}

void Renderer::Resize(UINT width, UINT height) {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    if (!swap_) return;
    ctx_->SetTarget(nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0))) {
        ReleaseDeviceResources();
        CreateDeviceResources();
        return;
    }
    CreateTarget();
}

void Renderer::SetDpi(float dpi) {
    dpi_ = dpi;
    if (!swap_) return;
    ctx_->SetTarget(nullptr);
    target_.Reset();
    CreateTarget();
}

ID2D1DeviceContext* Renderer::Begin() {
    if (!ctx_ || !target_) return nullptr;
    ctx_->BeginDraw();
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx_->Clear(D2D1::ColorF(0, 0, 0, 0));
    drawing_ = true;
    return ctx_.Get();
}

bool Renderer::End() {
    if (!drawing_) return true;
    drawing_ = false;
    HRESULT hr = ctx_->EndDraw();
    if (SUCCEEDED(hr)) hr = swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        ReleaseDeviceResources();
        CreateDeviceResources();
        return false;
    }
    return true;
}

bool Renderer::HasFontFamily(const wchar_t* family) const {
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(dwrite_->GetSystemFontCollection(&fonts))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

ComPtr<IDWriteTextFormat> Renderer::CreateFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                                                 DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT valign,
                                                 bool ellipsis) const {
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, L"ru-ru", &format)))
        return nullptr;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(valign);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (ellipsis) {
        ComPtr<IDWriteInlineObject> sign;
        dwrite_->CreateEllipsisTrimmingSign(format.Get(), &sign);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        format->SetTrimming(&trimming, sign.Get());
    }
    return format;
}
