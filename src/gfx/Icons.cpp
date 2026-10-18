#include "gfx/Icons.h"

#include <shlwapi.h>

#include <cstdio>
#include <string>
#include <unordered_map>

namespace gfx {

namespace {

struct IconSource {
    const char* name;
    const char* svg;
};

const IconSource kIcons[] = {
#include "gfx/IconData.inc"
};

const IconSource* Find(std::string_view name) {
    // The table is sorted by name (tools/gen-icons.ps1 sorts the files).
    size_t lo = 0, hi = std::size(kIcons);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = name.compare(kIcons[mid].name);
        if (c == 0) return &kIcons[mid];
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}

struct DocKey {
    const IconSource* icon;
    uint32_t primary;
    uint32_t secondary;
    uint32_t generation;
    bool operator==(const DocKey& o) const {
        return icon == o.icon && primary == o.primary && secondary == o.secondary && generation == o.generation;
    }
};

struct DocKeyHash {
    size_t operator()(const DocKey& k) const {
        return std::hash<const void*>()(k.icon) ^ (static_cast<size_t>(k.primary) << 7) ^
               (static_cast<size_t>(k.secondary) << 31) ^ k.generation;
    }
};

std::unordered_map<DocKey, ComPtr<ID2D1SvgDocument>, DocKeyHash>& Cache() {
    static std::unordered_map<DocKey, ComPtr<ID2D1SvgDocument>, DocKeyHash> cache;
    return cache;
}

std::string HexColor(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", rgb);
    return buf;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

ComPtr<ID2D1SvgDocument> Build(ID2D1DeviceContext5* ctx, const IconSource& icon, uint32_t primary,
                               uint32_t secondary) {
    std::string svg = icon.svg;
    ReplaceAll(svg, "#8F8F8F", HexColor(primary));
    ReplaceAll(svg, "#AB6314", HexColor(secondary));
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(svg.data()), static_cast<UINT>(svg.size())));
    if (!stream) return nullptr;
    ComPtr<ID2D1SvgDocument> doc;
    if (FAILED(ctx->CreateSvgDocument(stream.Get(), D2D1::SizeF(24, 24), &doc))) return nullptr;
    return doc;
}

}  // namespace

bool Icons::Exists(std::string_view name) {
    return Find(name) != nullptr;
}

void Icons::Draw(ID2D1DeviceContext5* ctx, std::string_view name, const ui::Rect& box, ui::Color primary,
                 ui::Color secondary) {
    const IconSource* icon = Find(name);
    if (!icon || box.Empty() || primary.a <= 0.001f) return;

    DocKey key{icon, primary.Rgb(), secondary.Rgb(), Gpu::Get().Generation()};
    auto& cache = Cache();
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 1500) cache.clear();
        it = cache.emplace(key, Build(ctx, *icon, key.primary, key.secondary)).first;
    }
    if (!it->second) return;

    const float scale = std::min(box.w, box.h) / 24.0f;
    const float x = box.x + (box.w - 24 * scale) / 2, y = box.y + (box.h - 24 * scale) / 2;
    D2D1_MATRIX_3X2_F saved;
    ctx->GetTransform(&saved);
    ctx->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * saved);

    // Alpha is applied with a layer only when needed; most icons are opaque.
    const bool translucent = primary.a < 0.999f;
    if (translucent) {
        D2D1_LAYER_PARAMETERS1 params = D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr,
                                                               D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                               D2D1::IdentityMatrix(), primary.a);
        ctx->PushLayer(params, nullptr);
    }
    ctx->DrawSvgDocument(it->second.Get());
    if (translucent) ctx->PopLayer();
    ctx->SetTransform(saved);
}

}  // namespace gfx
