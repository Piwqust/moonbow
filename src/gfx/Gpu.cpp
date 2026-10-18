#include "gfx/Gpu.h"

#include <algorithm>

namespace gfx {

namespace {

ComPtr<ID3D11Device> CreateDevice(D3D_DRIVER_TYPE type) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, flags, levels, static_cast<UINT>(std::size(levels)),
                                   D3D11_SDK_VERSION, &device, nullptr, nullptr);
    return SUCCEEDED(hr) ? device : nullptr;
}

// Hardware, or WARP when there is no usable GPU.
ComPtr<ID3D11Device> CreateD3DDevice() {
    ComPtr<ID3D11Device> device = CreateDevice(D3D_DRIVER_TYPE_HARDWARE);
    return device ? device : CreateDevice(D3D_DRIVER_TYPE_WARP);
}

}  // namespace

Gpu& Gpu::Get() {
    static Gpu gpu;
    return gpu;
}

Gpu::~Gpu() {
    if (deviceThread_.joinable()) deviceThread_.join();
}

bool Gpu::CreateFactories() {
    if (d2dFactory_) return true;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf()))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)));
}

void Gpu::StartDevice() {
    if (deviceThread_.joinable() || d3d_) return;
    // One thread, WARP first: two threads raced for the loader lock, and while the GPU driver's
    // DLLs loaded under it WARP could not start either.
    deviceThread_ = std::thread([this] {
        ComPtr<ID3D11Device> warp = CreateDevice(D3D_DRIVER_TYPE_WARP);
        {
            std::lock_guard lock(notifyMutex_);
            warpDevice_ = std::move(warp);
            warpDone_ = true;
            PostReady();
        }
        ComPtr<ID3D11Device> device = CreateD3DDevice();
        std::lock_guard lock(notifyMutex_);
        hardwareDevice_ = std::move(device);
        hardwareDone_ = true;
        PostReady();
    });
}

void Gpu::PostReady() {
    if (notifyWnd_) PostMessageW(notifyWnd_, notifyMsg_, 0, 0);
}

void Gpu::NotifyWhenReady(HWND notifyWnd, UINT doneMessage) {
    std::lock_guard lock(notifyMutex_);
    notifyWnd_ = notifyWnd;
    notifyMsg_ = doneMessage;
    if (hardwareDone_ || warpDone_) PostReady();
}

bool Gpu::FinishDevice() {
    ComPtr<ID3D11Device> next;
    {
        std::lock_guard lock(notifyMutex_);
        if (usingHardware_) return false;
        if (hardwareDone_ && hardwareDevice_) {
            next = std::move(hardwareDevice_);
            usingHardware_ = true;
            warpDevice_.Reset();
        } else if (!Ready() && warpDevice_) {
            next = std::move(warpDevice_);
        } else {
            return false;
        }
    }
    if (Ready()) ReleaseDeviceObjects();
    d3d_ = std::move(next);
    if (CreateDeviceObjects()) return true;
    ReleaseDeviceObjects();
    d3d_ = CreateDevice(D3D_DRIVER_TYPE_WARP);  // the hardware device was unusable after all
    return CreateDeviceObjects();
}

bool Gpu::DeviceFailed() {
    std::lock_guard lock(notifyMutex_);
    return !Ready() && hardwareDone_ && warpDone_ && !hardwareDevice_ && !warpDevice_;
}

bool Gpu::CreateDeviceObjects() {
    if (!d3d_) d3d_ = CreateD3DDevice();
    if (!d3d_) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(d3d_.As(&dxgiDevice_)) || FAILED(dxgiDevice_->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgi_))))
        return false;
    // One queued frame: Present waits for vsync instead of letting frames pile up,
    // so what is on screen is at most one frame behind the latest input.
    ComPtr<IDXGIDevice1> dxgi1;
    if (SUCCEEDED(dxgiDevice_.As(&dxgi1))) dxgi1->SetMaximumFrameLatency(1);
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_)) ||
        FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_)))
        return false;
    ctx_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    ++generation_;
    return true;
}

void Gpu::ReleaseDeviceObjects() {
    dcomp_.Reset();
    ctx_.Reset();
    d2dDevice_.Reset();
    dxgi_.Reset();
    dxgiDevice_.Reset();
    d3d_.Reset();
}

bool Gpu::RecreateDevice() {
    ReleaseDeviceObjects();
    return CreateDeviceObjects();
}

// ---------------------------------------------------------------------------

bool Surface::Attach(HWND hwnd, float dpi) {
    hwnd_ = hwnd;
    dpi_ = dpi;
    RECT rc;
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    height_ = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    return Build();
}

void Surface::Detach() {
    Drop();
    hwnd_ = nullptr;
}

void Surface::Drop() {
    if (Gpu::Get().Ctx() && target_) Gpu::Get().Ctx()->SetTarget(nullptr);
    target_.Reset();
    visual_.Reset();
    dcompTarget_.Reset();
    swap_.Reset();
}

bool Surface::Build() {
    Gpu& gpu = Gpu::Get();
    if (!gpu.Ready() || !hwnd_) return false;
    generation_ = gpu.Generation();

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
    if (FAILED(gpu.Dxgi()->CreateSwapChainForComposition(gpu.D3D(), &desc, nullptr, &swap_))) return false;

    // topmost = TRUE: composed above child windows, i.e. above mpv's video window.
    IDCompositionDevice* dcomp = gpu.DComp();
    if (FAILED(dcomp->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_)) || FAILED(dcomp->CreateVisual(&visual_)) ||
        FAILED(visual_->SetContent(swap_.Get())) || FAILED(dcompTarget_->SetRoot(visual_.Get())) ||
        FAILED(dcomp->Commit()))
        return false;
    return BuildTarget();
}

bool Surface::BuildTarget() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
    return SUCCEEDED(Gpu::Get().Ctx()->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_));
}

void Surface::Resize(UINT width, UINT height) {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    if (!swap_) return;
    if (Gpu::Get().Ctx()) Gpu::Get().Ctx()->SetTarget(nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0))) {
        Drop();
        Build();
        return;
    }
    BuildTarget();
}

void Surface::SetDpi(float dpi) {
    dpi_ = dpi;
    if (!swap_) return;
    if (Gpu::Get().Ctx()) Gpu::Get().Ctx()->SetTarget(nullptr);
    target_.Reset();
    BuildTarget();
}

ID2D1DeviceContext5* Surface::Begin() {
    Gpu& gpu = Gpu::Get();
    if (!gpu.Ready() || !hwnd_) return nullptr;
    if (!swap_ || generation_ != gpu.Generation()) {
        Drop();
        if (!Build()) Drop();
    } else if (!target_) {
        BuildTarget();
    }
    if (!target_) {
        // A resize or DPI change can be the first to meet a removed device (driver update, TDR);
        // End() never runs then, so recover here or the window stays blank.
        if (FAILED(gpu.D3D()->GetDeviceRemovedReason())) {
            Drop();
            if (gpu.RecreateDevice() && !Build()) Drop();
        }
        if (!target_) return nullptr;
    }
    ID2D1DeviceContext5* ctx = gpu.Ctx();
    ctx->SetTarget(target_.Get());
    ctx->SetDpi(dpi_, dpi_);
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
    drawing_ = true;
    return ctx;
}

bool Surface::End() {
    if (!drawing_) return true;
    drawing_ = false;
    Gpu& gpu = Gpu::Get();
    HRESULT hr = gpu.Ctx()->EndDraw();
    gpu.Ctx()->SetTarget(nullptr);
    if (SUCCEEDED(hr)) hr = swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Drop();
        gpu.RecreateDevice();
        return false;
    }
    return true;
}

}  // namespace gfx
