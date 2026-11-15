// MoonbowThumbs.dll: Explorer thumbnails for audio files. Built apart from Moonbow.exe (build.bat).
//
// Order: the handler that was registered for the type before Moonbow (Icaros from K-Lite, say) gets
// the first try, then the cover in the tags (library/TagReader), then Moonbow's stand-in record, so
// a song without art no longer shows the bare file-type icon.
#include "library/TagReader.h"
#include "thumbs/ThumbsClsid.h"

#include <windows.h>
#include <d2d1.h>
#include <propsys.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")

using Microsoft::WRL::ComPtr;

namespace {

std::atomic<long> g_objects{0}, g_locks{0};

CLSID OurClsid() {
    CLSID id{};
    CLSIDFromString(MOONBOW_THUMBS_CLSID_STR, &id);
    return id;
}

class StreamSource : public library::ByteSource {
public:
    explicit StreamSource(IStream* s) : s_(s) {
        STATSTG st{};
        if (SUCCEEDED(s_->Stat(&st, STATFLAG_NONAME))) size_ = st.cbSize.QuadPart;
    }
    uint64_t Size() const override { return size_; }
    size_t Read(uint64_t offset, void* buf, size_t n) override {
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(offset);
        ULONG got = 0;
        if (n > 0xFFFFFFFFu || FAILED(s_->Seek(at, STREAM_SEEK_SET, nullptr))) return 0;
        if (FAILED(s_->Read(buf, static_cast<ULONG>(n), &got))) return 0;
        return got;
    }

private:
    IStream* s_;
    uint64_t size_ = 0;
};

void Rewind(IStream* s) {
    LARGE_INTEGER zero{};
    s->Seek(zero, STREAM_SEEK_SET, nullptr);
}

// The thumbnail handler the type had without Moonbow: machine-wide registrations only, since
// Moonbow's own sits in HKCU and shadows them.
bool PreviousHandler(const std::wstring& ext, CLSID* out) {
    if (ext.empty()) return false;
    const CLSID ours = OurClsid();
    for (const std::wstring& key : {L"Software\\Classes\\" + ext + L"\\ShellEx\\" SHELL_THUMBNAIL_HANDLER_STR,
                                    L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\ShellEx\\" SHELL_THUMBNAIL_HANDLER_STR}) {
        wchar_t value[64];
        DWORD n = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &n) != ERROR_SUCCESS) continue;
        CLSID id{};
        if (SUCCEEDED(CLSIDFromString(value, &id)) && id != ours) return *out = id, true;
    }
    return false;
}

HRESULT FromPrevious(IStream* stream, const std::wstring& ext, UINT cx, HBITMAP* bmp, WTS_ALPHATYPE* alpha) {
    CLSID id;
    if (!PreviousHandler(ext, &id)) return E_FAIL;
    ComPtr<IThumbnailProvider> provider;
    if (FAILED(CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&provider)))) return E_FAIL;
    ComPtr<IInitializeWithStream> init;
    if (FAILED(provider.As(&init))) return E_FAIL;
    Rewind(stream);
    if (FAILED(init->Initialize(stream, STGM_READ))) return E_FAIL;
    const HRESULT hr = provider->GetThumbnail(cx, bmp, alpha);
    return hr == S_OK && *bmp ? S_OK : E_FAIL;
}

// A top-down 32-bit DIB with the pixels of `source`.
HRESULT ToHBitmap(IWICBitmapSource* source, HBITMAP* out) {
    UINT w = 0, h = 0;
    source->GetSize(&w, &h);
    if (!w || !h) return E_FAIL;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = static_cast<LONG>(w);
    bi.bmiHeader.biHeight = -static_cast<LONG>(h);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return E_OUTOFMEMORY;
    if (FAILED(source->CopyPixels(nullptr, w * 4, w * h * 4, static_cast<BYTE*>(bits)))) {
        DeleteObject(bmp);
        return E_FAIL;
    }
    *out = bmp;
    return S_OK;
}

HRESULT FromCover(IWICImagingFactory* wic, const std::vector<uint8_t>& cover, UINT cx, HBITMAP* bmp) {
    ComPtr<IWICStream> mem;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateStream(&mem)) ||
        FAILED(mem->InitializeFromMemory(const_cast<BYTE*>(cover.data()), static_cast<DWORD>(cover.size()))) ||
        FAILED(wic->CreateDecoderFromStream(mem.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)))
        return E_FAIL;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) return E_FAIL;
    // Explorer wants the longer side at most cx.
    const double k = std::min(1.0, double(cx) / std::max(w, h));
    const UINT tw = std::max(1u, UINT(std::lround(w * k))), th = std::max(1u, UINT(std::lround(h * k)));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeFant)) ||
        FAILED(wic->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return E_FAIL;
    return ToHBitmap(converter.Get(), bmp);
}

uint32_t ArtSeed(const std::wstring& s) {  // app/StandInArt.cpp
    uint32_t h = 2166136261u;
    for (wchar_t c : s) h = (h ^ static_cast<uint32_t>(c)) * 16777619u;
    return h;
}

D2D1_COLOR_F Hex(uint32_t rgb, float a = 1) {
    return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}

// The library's stand-in record in the dark palette (bgCard, textPrimary, textTertiary): keep it in
// step with PaintStandInArt in app/StandInArt.cpp and the "music" icon in gfx/IconData.inc.
HRESULT StandInRecord(IWICImagingFactory* wic, uint32_t seed, UINT cx, HBITMAP* bmp) {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IWICBitmap> target;
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(wic->CreateBitmap(cx, cx, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &target)))
        return E_FAIL;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(d2d->CreateWicBitmapRenderTarget(target.Get(), props, &rt))) return E_FAIL;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(Hex(0x1C1E24), &brush))) return E_FAIL;

    const float w = float(cx), c = w / 2, maxR = w * 0.42f;
    const D2D1_POINT_2F center{c, c};
    auto ink = [&](float a) { brush->SetColor(Hex(0xEEF1F6, a)); };
    rt->BeginDraw();
    rt->Clear(Hex(0x1C1E24));
    // Explorer thumbnails are a few times larger than DIPs at 1x, so lines grow with the size.
    const float line = std::max(1.f, w / 256.f);
    int k = 0;
    for (float rad = maxR * 0.38f; rad < maxR; rad += w * 0.012f, ++k) {
        ink(0.025f + 0.035f * static_cast<float>((seed >> (k % 24)) & 1));
        rt->DrawEllipse(D2D1::Ellipse(center, rad, rad), brush.Get(), line);
    }
    ink(0.14f);
    rt->DrawEllipse(D2D1::Ellipse(center, maxR, maxR), brush.Get(), 1.5f * line);
    ink(0.03f);
    rt->FillEllipse(D2D1::Ellipse(center, maxR * 0.30f, maxR * 0.30f), brush.Get());

    // The note: the "music" icon's path on a 24-unit grid.
    ComPtr<ID2D1PathGeometry> note;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(d2d->CreatePathGeometry(&note)) && SUCCEEDED(note->Open(&sink))) {
        // Larger than the library's 0.22: Explorer shows thumbnails small, where that note is a few pixels.
        const float icon = maxR * 0.40f, s = icon / 24, x0 = c - icon / 2, y0 = c - icon / 2;
        auto p = [&](float x, float y) { return D2D1::Point2F(x0 + x * s, y0 + y * s); };
        sink->BeginFigure(p(18, 6), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(p(13.6676f, 6));
        sink->AddLine(p(13.6676f, 13.5f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(p(13.6676f, 13.5f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(p(13.6676f, 13.5f), p(13.668f, 18), p(9.16772f, 18)));
        sink->AddBezier(D2D1::BezierSegment(p(6.75246f, 18), p(5.41767f, 15.75f), p(6.2462f, 14.0932f)));
        sink->AddBezier(D2D1::BezierSegment(p(7.88438f, 10.8173f), p(13.6676f, 13.5f), p(13.6676f, 13.5f)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        ComPtr<ID2D1StrokeStyle> round;
        d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &round);
        brush->SetColor(Hex(0x8E939C));
        rt->DrawGeometry(note.Get(), brush.Get(), 1.5f * s, round.Get());
    }
    if (FAILED(rt->EndDraw())) return E_FAIL;
    return ToHBitmap(target.Get(), bmp);
}

class ThumbProvider : public IThumbnailProvider, public IInitializeWithStream {
public:
    ThumbProvider() { ++g_objects; }
    ~ThumbProvider() { --g_objects; }

    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IThumbnailProvider) *out = static_cast<IThumbnailProvider*>(this);
        else if (riid == IID_IInitializeWithStream) *out = static_cast<IInitializeWithStream*>(this);
        else return *out = nullptr, E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        const long n = --refs_;
        if (!n) delete this;
        return n;
    }

    STDMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_INVALIDARG;
        if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream_ = stream;
        STATSTG st{};
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_DEFAULT)) && st.pwcsName) {
            name_ = st.pwcsName;
            CoTaskMemFree(st.pwcsName);
        }
        return S_OK;
    }

    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* bmp, WTS_ALPHATYPE* alpha) override {
        if (!bmp || !alpha) return E_POINTER;
        if (!stream_ || !cx) return E_UNEXPECTED;
        *bmp = nullptr;
        *alpha = WTSAT_RGB;
        const wchar_t* dot = PathFindExtensionW(name_.c_str());
        if (SUCCEEDED(FromPrevious(stream_.Get(), dot, cx, bmp, alpha))) return S_OK;
        *alpha = WTSAT_RGB;

        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return E_FAIL;
        StreamSource src(stream_.Get());
        library::TagInfo tags;
        library::TagOptions opt;
        opt.tags = false;
        opt.cover = true;
        if (library::ReadTags(src, &tags, opt) && tags.hasCover && !tags.cover.empty() && SUCCEEDED(FromCover(wic.Get(), tags.cover, cx, bmp)))
            return S_OK;
        return StandInRecord(wic.Get(), ArtSeed(name_), cx, bmp);
    }

private:
    std::atomic<long> refs_{1};
    ComPtr<IStream> stream_;
    std::wstring name_;
};

class Factory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid != IID_IUnknown && riid != IID_IClassFactory) return *out = nullptr, E_NOINTERFACE;
        *out = this;
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }  // a static object
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* p = new (std::nothrow) ThumbProvider;
        if (!p) return E_OUTOFMEMORY;
        const HRESULT hr = p->QueryInterface(riid, out);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        lock ? ++g_locks : --g_locks;
        return S_OK;
    }
};

Factory g_factory;

}  // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != OurClsid()) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, out);
}

STDAPI DllCanUnloadNow() {
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}
