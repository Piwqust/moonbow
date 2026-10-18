#pragma once
// Text for fixed-size shell buffers (the tray tooltip's szTip[128], the notification's
// szInfo[256]...), where wcsncpy_s(_TRUNCATE) would cut a long title mid-pair and drop the suffix.

#include <cstddef>
#include <string>
#include <string_view>

// `text` + `suffix` in at most `capacity - 1` UTF-16 units (capacity counts the NUL). A text that
// doesn't fit is cut at a character boundary, without trailing spaces, and ends with "…" before
// the suffix, so " — Lumen" stays visible.
inline std::wstring FitShellText(std::wstring_view text, std::wstring_view suffix, size_t capacity) {
    if (capacity == 0) return {};
    const size_t max = capacity - 1;
    if (text.size() + suffix.size() <= max) return std::wstring(text) + std::wstring(suffix);
    if (suffix.size() + 1 >= max) {  // no room for any text: the text alone, cut
        suffix = {};
        if (text.size() <= max) return std::wstring(text);
    }
    size_t keep = max - suffix.size() - 1;  // room for the text before "…"
    if (keep > 0 && keep < text.size() && text[keep] >= 0xDC00 && text[keep] <= 0xDFFF &&
        text[keep - 1] >= 0xD800 && text[keep - 1] <= 0xDBFF)
        --keep;  // never split a surrogate pair
    while (keep > 0 && (text[keep - 1] == L' ' || text[keep - 1] == L'\t')) --keep;
    return std::wstring(text.substr(0, keep)) + L"…" + std::wstring(suffix);
}
