#include "gfx/Text.h"

#include <cmath>
#include <string>
#include <unordered_map>

namespace gfx {

namespace {

struct StyleSpec {
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool tabular;
};

constexpr StyleSpec kStyles[] = {
    {32, DWRITE_FONT_WEIGHT_LIGHT, false},     // Display
    {24, DWRITE_FONT_WEIGHT_LIGHT, false},     // Title
    {20, DWRITE_FONT_WEIGHT_REGULAR, false},   // Section
    {16, DWRITE_FONT_WEIGHT_MEDIUM, false},    // Heading
    {16, DWRITE_FONT_WEIGHT_REGULAR, false},   // Large
    {14, DWRITE_FONT_WEIGHT_REGULAR, false},   // Body
    {14, DWRITE_FONT_WEIGHT_MEDIUM, false},    // BodyMedium
    {14, DWRITE_FONT_WEIGHT_SEMI_BOLD, false}, // BodyStrong
    {12, DWRITE_FONT_WEIGHT_REGULAR, false},   // Small
    {12, DWRITE_FONT_WEIGHT_MEDIUM, false},    // SmallMedium
    {10, DWRITE_FONT_WEIGHT_MEDIUM, false},    // Micro
    {14, DWRITE_FONT_WEIGHT_REGULAR, true},    // Mono
    {12, DWRITE_FONT_WEIGHT_REGULAR, true},    // MonoSmall
};
static_assert(std::size(kStyles) == static_cast<size_t>(TextStyle::Count));

// Resource ids of the bundled fonts, see res/app.rc.
constexpr int kFontResources[] = {201, 202, 203, 204};

struct LayoutKey {
    std::wstring text;
    uint32_t params;
    int32_t w, h;
    bool operator==(const LayoutKey& o) const { return params == o.params && w == o.w && h == o.h && text == o.text; }
};

struct LayoutKeyHash {
    size_t operator()(const LayoutKey& k) const {
        size_t h = std::hash<std::wstring>()(k.text);
        h ^= (static_cast<size_t>(k.params) << 1) ^ (static_cast<size_t>(k.w) << 20) ^ (static_cast<size_t>(k.h) << 40);
        return h;
    }
};

struct LayoutEntry {
    ComPtr<IDWriteTextLayout> layout;
    uint32_t lastFrame = 0;
};

int32_t Quantize(float v) {
    return v >= 100000.0f ? INT32_MAX : static_cast<int32_t>(std::ceil(v * 4));
}

}  // namespace

struct Text::Impl {
    std::unordered_map<LayoutKey, LayoutEntry, LayoutKeyHash> layouts;
    uint32_t frame = 1;
};

Text& Text::Get() {
    static Text text;
    return text;
}

bool Text::Init(HINSTANCE instance) {
    impl_ = new Impl();
    IDWriteFactory5* dw = Gpu::Get().DWrite();
    ComPtr<IDWriteFontSetBuilder1> builder;
    bool bundled = SUCCEEDED(dw->CreateInMemoryFontFileLoader(&loader_)) &&
                   SUCCEEDED(dw->RegisterFontFileLoader(loader_.Get())) && SUCCEEDED(dw->CreateFontSetBuilder(&builder));
    if (bundled) {
        for (int id : kFontResources) {
            HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
            HGLOBAL mem = res ? LoadResource(instance, res) : nullptr;
            const void* data = mem ? LockResource(mem) : nullptr;
            if (!data) continue;
            ComPtr<IDWriteFontFile> file;
            // The resource stays mapped for the process lifetime, so no copy is needed:
            // passing the loader itself as the owner object keeps DirectWrite from copying.
            if (SUCCEEDED(loader_->CreateInMemoryFontFileReference(dw, data, SizeofResource(instance, res),
                                                                    loader_.Get(), &file)))
                builder->AddFontFile(file.Get());
        }
        ComPtr<IDWriteFontSet> set;
        bundled = SUCCEEDED(builder->CreateFontSet(&set)) &&
                  SUCCEEDED(dw->CreateFontCollectionFromFontSet(set.Get(), &fonts_));
    }
    if (!bundled) {
        fonts_.Reset();
        family_ = L"Segoe UI Variable Text";
    }

    if (SUCCEEDED(dw->CreateTypography(&tabular_)))
        tabular_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});

    for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i) {
        const StyleSpec& s = kStyles[i];
        if (FAILED(dw->CreateTextFormat(family_, fonts_.Get(), s.weight, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, s.size, L"ru-ru", &formats_[i])))
            return false;
        formats_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const float lh = std::round(s.size * 1.43f);
        formats_[i]->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh, std::round(lh * 0.78f));
        dw->CreateEllipsisTrimmingSign(formats_[i].Get(), &ellipsis_[i]);
    }
    return true;
}

float Text::Size(TextStyle s) const {
    return kStyles[static_cast<int>(s)].size;
}

float Text::LineHeight(TextStyle s) const {
    return std::round(Size(s) * 1.43f);
}

IDWriteTextFormat* Text::Format(TextStyle style) {
    return formats_[static_cast<int>(style)].Get();
}

IDWriteTextLayout* Text::Layout(std::wstring_view text, TextStyle style, float maxWidth, float maxHeight, HAlign h,
                                VAlign v, uint8_t flags) {
    const uint32_t params = static_cast<uint32_t>(style) | (static_cast<uint32_t>(h) << 8) |
                            (static_cast<uint32_t>(v) << 12) | (static_cast<uint32_t>(flags) << 16);
    LayoutKey key{std::wstring(text), params, Quantize(maxWidth), Quantize(maxHeight)};
    auto it = impl_->layouts.find(key);
    if (it != impl_->layouts.end()) {
        it->second.lastFrame = impl_->frame;
        return it->second.layout.Get();
    }

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Gpu::Get().DWrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), Format(style),
                                                     std::max(0.0f, maxWidth), std::max(0.0f, maxHeight), &layout)))
        return nullptr;
    layout->SetTextAlignment(h == HAlign::Leading    ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : h == HAlign::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                     : DWRITE_TEXT_ALIGNMENT_TRAILING);
    layout->SetParagraphAlignment(v == VAlign::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : v == VAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                        : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    if (flags & kTextWrap) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (flags & kTextEllipsis) {
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trimming, ellipsis_[static_cast<int>(style)].Get());
    }
    if (kStyles[static_cast<int>(style)].tabular && tabular_)
        layout->SetTypography(tabular_.Get(), {0, static_cast<UINT32>(text.size())});

    IDWriteTextLayout* raw = layout.Get();
    impl_->layouts.emplace(std::move(key), LayoutEntry{std::move(layout), impl_->frame});
    return raw;
}

TextMetrics Text::Measure(std::wstring_view text, TextStyle style, float maxWidth, uint8_t flags) {
    TextMetrics m;
    if (text.empty()) {
        m.height = LineHeight(style);
        return m;
    }
    IDWriteTextLayout* layout = Layout(text, style, maxWidth, 100000.0f, HAlign::Leading, VAlign::Top, flags);
    if (!layout) return m;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    m.width = std::ceil(tm.widthIncludingTrailingWhitespace);
    m.height = std::ceil(tm.height);
    m.lines = tm.lineCount;
    return m;
}

void Text::EndFrame() {
    ++impl_->frame;
    // Every ~2 s of continuous drawing, drop layouts unused for 120 frames.
    if (impl_->frame % 120 != 0) return;
    for (auto it = impl_->layouts.begin(); it != impl_->layouts.end();) {
        if (impl_->frame - it->second.lastFrame > 120)
            it = impl_->layouts.erase(it);
        else
            ++it;
    }
}

}  // namespace gfx
