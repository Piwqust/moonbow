#pragma once
#include "gfx/Gpu.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace gfx {

// Text styles from design/DESIGN.md, section 2.
enum class TextStyle : uint8_t {
    Display,     // Light 32
    Title,       // Light 24
    Section,     // Regular 20
    Heading,     // Medium 16
    Large,       // Regular 16
    Body,        // Regular 14
    BodyMedium,  // Medium 14
    BodyStrong,  // SemiBold 14
    Small,       // Regular 12
    SmallMedium, // Medium 12
    Micro,       // Medium 10
    Mono,        // Regular 14, tabular figures
    MonoSmall,   // Regular 12, tabular figures
    Count,
};

enum class HAlign : uint8_t { Leading, Center, Trailing };
enum class VAlign : uint8_t { Top, Center, Bottom };

enum TextFlags : uint8_t {
    kTextEllipsis = 1 << 0,  // single line, trimmed with "…"
    kTextWrap = 1 << 1,      // wraps to the width (otherwise single line)
};

struct TextMetrics {
    float width = 0;   // widest line
    float height = 0;  // all lines
    uint32_t lines = 0;
};

// Loads the bundled Inter fonts and caches text formats and layouts.
class Text {
public:
    static Text& Get();
    ~Text() { WaitReady(); }

    // Loads the fonts on a worker thread (~10 ms) while the window is created; the first
    // layout waits for it. DirectWrite objects are free-threaded.
    void InitAsync(HINSTANCE instance);
    float LineHeight(TextStyle s) const;

    // Cached layout for one string. Valid until the next Trim() pass drops it.
    IDWriteTextLayout* Layout(std::wstring_view text, TextStyle style, float maxWidth, float maxHeight, HAlign h,
                              VAlign v, uint8_t flags);
    TextMetrics Measure(std::wstring_view text, TextStyle style, float maxWidth = 100000.0f, uint8_t flags = 0);

    // Drops layouts not used for a few frames. Call once per frame.
    void EndFrame();

private:
    IDWriteTextFormat* Format(TextStyle style);
    void WaitReady();
    bool LoadFonts(HINSTANCE instance);

    ComPtr<IDWriteFontCollection1> fonts_;
    ComPtr<IDWriteInMemoryFontFileLoader> loader_;
    ComPtr<IDWriteTextFormat> formats_[static_cast<int>(TextStyle::Count)];
    ComPtr<IDWriteTypography> tabular_;
    ComPtr<IDWriteInlineObject> ellipsis_[static_cast<int>(TextStyle::Count)];
    const wchar_t* family_ = L"Inter";
    struct Impl;
    Impl* impl_ = nullptr;
    std::thread fontThread_;
};

}  // namespace gfx
