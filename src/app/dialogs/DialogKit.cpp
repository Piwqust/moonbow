#include "app/dialogs/DialogKit.h"
#include "ui/Host.h"

#include "app/library/LibraryCommon.h"
#include "gfx/Text.h"

#include <shobjidl.h>

#include <algorithm>
#include <cmath>

namespace app::dlg {

using namespace ui;

Color SoftText() {
    return Color::Mix(P().textPrimary, P().textSecondary, 0.35f);
}

// --- painting helpers ------------------------------------------------------------------

void DashedLine(Painter& p, float x1, float x2, float y, Color c, float dash, float gapLen) {
    for (float x = x1; x < x2; x += dash + gapLen) p.Fill({x, y, std::min(dash, x2 - x), 1}, c);
}

void DashedRect(Painter& p, const Rect& r, Color c, float dash, float gapLen) {
    const float step = dash + gapLen;
    for (float x = r.x; x < r.Right(); x += step) {
        const float w = std::min(dash, r.Right() - x);
        p.Fill({x, r.y, w, 1}, c);
        p.Fill({x, r.Bottom() - 1, w, 1}, c);
    }
    for (float y = r.y; y < r.Bottom(); y += step) {
        const float h = std::min(dash, r.Bottom() - y);
        p.Fill({r.x, y, 1, h}, c);
        p.Fill({r.Right() - 1, y, 1, h}, c);
    }
}

std::vector<std::wstring> SplitKeys(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t at = text.find(L" + ", start);
        out.push_back(text.substr(start, at == std::wstring::npos ? std::wstring::npos : at - start));
        if (at == std::wstring::npos) break;
        start = at + 3;
    }
    if (out.size() == 1 && out[0].empty()) out.clear();
    return out;
}

// --- DButton -----------------------------------------------------------------------------

DButton::DButton(std::wstring text, BtnStyle s, TextStyle ts) : style(s), textStyle(ts), text_(std::move(text)) {
    interactive = true;
    focusable = true;
    height = Length::Px(32);
}

void DButton::SetText(std::wstring text) {
    text_ = std::move(text);
    InvalidateLayout();
}

Size DButton::MeasureContent(float, float) {
    float w = gfx::Text::Get().Measure(text_, textStyle).width + 32;
    if (!icon.empty()) w += 28;
    return {std::max(minWidth, std::ceil(w)), 32};
}

void DButton::Paint(Painter& p) {
    const Palette& c = P();
    const bool on = IsEnabled();
    const bool hot = on && IsHot(), down = on && IsPressed();
    Color bg, fg;
    switch (style) {
        case BtnStyle::Neutral:
            bg = down ? c.bgSelected : hot ? Color::Mix(c.bgChrome, c.bgSelected, 0.6f) : c.bgChrome;
            fg = hot ? c.textPrimary : c.textSecondary;
            break;
        case BtnStyle::Accent:  // the primary action: same filled look as ui::Button Primary
        case BtnStyle::Solid:
            bg = down ? Color::Mix(c.accent, c.bgBase, 0.22f) : hot ? Color::Mix(c.accent, c.bgBase, 0.1f) : c.accent;
            fg = c.onAccent;
            break;
    }
    if (!on) p.PushOpacity(0.5f);
    p.Fill(rect, bg, 6);
    const float tw = gfx::Text::Get().Measure(text_, textStyle).width;
    const float total = tw + (icon.empty() ? 0.0f : 28.0f);
    float x = std::round(rect.Cx() - total / 2);
    if (!icon.empty()) {
        p.Icon(icon, {x, rect.Cy() - 10, 20, 20}, fg);
        x += 28;
    }
    p.Text(text_, {x, rect.y, tw + 2, rect.h}, textStyle, fg, HAlign::Leading, VAlign::Center, 0);
    if (!on) p.PopOpacity();
    PaintFocusRing(p, rect, 6);
}

// --- FieldDropdown -----------------------------------------------------------------------

void FieldDropdown::Paint(Painter& p) {
    const Palette& c = P();
    const bool hot = IsEnabled() && (IsHot() || IsPressed());
    p.Fill(rect, hot ? Color::Mix(c.bgRaised, c.bgSelected, 0.5f) : c.bgRaised, 6);
    PaintFocusRing(p, rect, 6);
    const int sel = Selected();
    const bool has = sel >= 0 && sel < static_cast<int>(Choices().size());
    const std::wstring& label = has ? Choices()[static_cast<size_t>(sel)].label : placeholder;
    const Color fg = !IsEnabled() ? c.textDisabled : hot ? c.textPrimary : c.textSecondary;
    p.Text(label, {rect.x + 12, rect.y, rect.w - 48, rect.h}, TextStyle::Body, fg);
    p.Icon("chevron_down", {rect.Right() - 32, rect.Cy() - 12, 24, 24}, fg);
}

// --- KeyCap ----------------------------------------------------------------------------------

KeyCap::KeyCap(std::wstring text, bool large) : text_(std::move(text)), large_(large) {}

Size KeyCap::MeasureContent(float, float) {
    const TextStyle s = large_ ? TextStyle::Large : TextStyle::Small;
    const float w = gfx::Text::Get().Measure(text_, s).width + (large_ ? 40.0f : 16.0f);
    return large_ ? Size{std::max(56.0f, std::ceil(w)), 46} : Size{std::max(26.0f, std::ceil(w)), 24};
}

void KeyCap::Paint(Painter& p) {
    const Palette& c = P();
    if (large_) {
        const Rect cap{rect.x, rect.y, rect.w, 42};
        p.Fill(cap.Offset(0, 4), c.bgDeep, 12);  // key depth
        p.Fill(cap, c.bgChrome, 12);
        p.Stroke(cap, Color::Mix(c.line, c.lineStrong, 0.6f), 12, 2);
        p.Text(text_, cap, TextStyle::Large, SoftText(), HAlign::Center);
    } else {
        p.Fill(rect, c.bgDeep, 4);
        p.Stroke(rect, c.line, 4);
        p.Text(text_, rect, TextStyle::Small, c.textSecondary, HAlign::Center);
    }
}

// --- TextArea ---------------------------------------------------------------------------------

TextArea::TextArea(std::wstring ph) : placeholder(std::move(ph)) {
    interactive = true;
    focusable = true;
    cursor = IDC_IBEAM;
}

void TextArea::SetText(std::wstring text, bool notify) {
    text_ = std::move(text);
    caret_ = anchor_ = text_.size();
    EnsureVisible();
    Invalidate();
    if (notify && onChange) onChange(text_);
}

std::vector<std::pair<size_t, size_t>> TextArea::Lines() const {
    std::vector<std::pair<size_t, size_t>> out;
    size_t start = 0;
    for (size_t i = 0; i <= text_.size(); ++i)
        if (i == text_.size() || text_[i] == L'\n') {
            out.push_back({start, i});
            start = i + 1;
        }
    return out;
}

TextArea::Pos TextArea::PosOf(size_t index) const {
    const auto lines = Lines();
    for (size_t l = 0; l < lines.size(); ++l)
        if (index <= lines[l].second) return {l, index - lines[l].first};
    return {lines.size() - 1, lines.back().second - lines.back().first};
}

size_t TextArea::IndexOf(size_t line, size_t col) const {
    const auto lines = Lines();
    line = std::min(line, lines.size() - 1);
    return std::min(lines[line].first + col, lines[line].second);
}

float TextArea::LineH() const {
    return std::round(gfx::Text::Get().LineHeight(TextStyle::Body) + 6);
}

Rect TextArea::TextRect() const {
    return rect.Inset(Insets{14, 10, 14, 10});
}

float TextArea::XOf(size_t index) const {
    const auto lines = Lines();
    const Pos pos = PosOf(index);
    const size_t start = lines[pos.line].first;
    return gfx::Text::Get().Measure(std::wstring_view(text_).substr(start, pos.col), TextStyle::Body).width;
}

size_t TextArea::IndexAt(float x, float y) const {
    const Rect tr = TextRect();
    const auto lines = Lines();
    const float fl = std::floor((y - tr.y + scroll_) / LineH());
    const size_t line = static_cast<size_t>(std::clamp(fl, 0.0f, static_cast<float>(lines.size() - 1)));
    const auto [s, e] = lines[line];
    const float lx = x - tr.x;
    size_t best = s;
    float prev = 0;
    for (size_t i = s; i < e; ++i) {
        const float w = gfx::Text::Get().Measure(std::wstring_view(text_).substr(s, i + 1 - s), TextStyle::Body).width;
        if (lx < (prev + w) / 2) return i;
        prev = w;
        best = i + 1;
    }
    return lx <= 0 ? s : best;
}

Rect TextArea::CaretRect() const {
    const Rect tr = TextRect();
    const Pos pos = PosOf(caret_);
    const float lh = LineH();
    return {tr.x + XOf(caret_), tr.y + pos.line * lh - scroll_ + 3, 1, lh - 6};
}

void TextArea::EnsureVisible() {
    const Rect tr = TextRect();
    const float lh = LineH();
    const float top = PosOf(caret_).line * lh;
    if (top < scroll_) scroll_ = top;
    if (top + lh > scroll_ + tr.h) scroll_ = top + lh - tr.h;
    scroll_ = std::max(0.0f, scroll_);
}

void TextArea::Paint(Painter& p) {
    const Palette& c = P();
    p.Fill(rect, c.bgDeep, 6);
    p.Stroke(rect, IsFocused() ? c.lineStrong : IsHot() ? c.line : c.bgPanel, 6, 2);
    const Rect tr = TextRect();
    p.PushClip(rect.Inset(2));
    const float lh = LineH();
    if (text_.empty()) {
        // A few DIPs in, so the caret at the start doesn't sit on the first letter.
        p.Text(placeholder, {tr.x + 4, tr.y, tr.w - 4, lh}, TextStyle::Body, c.textDisabled);
    } else {
        const auto lines = Lines();
        const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        for (size_t l = 0; l < lines.size(); ++l) {
            const float y = tr.y + l * lh - scroll_;
            if (y + lh < rect.y || y > rect.Bottom()) continue;
            const auto [s, e] = lines[l];
            if (IsFocused() && b > a && b > s && a <= e) {
                const size_t ss = std::max(a, s), se = std::min(b, e);
                const float x1 = XOf(ss), x2 = se > ss ? XOf(se) : x1;
                const float extra = b > e ? 6.0f : 0.0f;  // the line break is selected too
                p.Fill({tr.x + x1, y + 2, x2 - x1 + extra, lh - 4}, c.accentDim, 2);
            }
            p.Text(std::wstring_view(text_).substr(s, e - s), {tr.x, y, 100000.0f, lh}, TextStyle::Body, c.textPrimary,
                   HAlign::Leading, VAlign::Center, 0);
        }
    }
    if (IsFocused()) p.Fill(CaretRect(), c.textPrimary);
    p.PopClip();
    PaintFocusRing(p, rect, 6);
}

bool TextArea::OnMouseDown(const MouseEvent& e) {
    selecting_ = true;
    MoveCaret(IndexAt(e.x, e.y), e.mods.shift);
    return true;
}

bool TextArea::OnMouseMove(const MouseEvent& e) {
    if (selecting_) MoveCaret(IndexAt(e.x, e.y), true);
    return true;
}

bool TextArea::OnMouseUp(const MouseEvent&) {
    selecting_ = false;
    return true;
}

bool TextArea::OnWheel(const WheelEvent& e) {
    const float maxScroll = std::max(0.0f, Lines().size() * LineH() - TextRect().h);
    if (maxScroll <= 0) return false;
    scroll_ = std::clamp(scroll_ - e.delta * LineH() * 3, 0.0f, maxScroll);
    Invalidate();
    return true;
}

void TextArea::Insert(std::wstring_view s) {
    DeleteSelection();
    std::wstring clean;
    for (wchar_t ch : s) {
        if (ch == L'\r') continue;
        if (ch == L'\n' || ch == L'\t' || (ch >= 0x20 && ch != 0x7F)) clean.push_back(ch == L'\t' ? L' ' : ch);
    }
    text_.insert(caret_, clean);
    MoveCaret(caret_ + clean.size(), false);
    Changed();
}

void TextArea::DeleteSelection() {
    const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
    if (b > a) text_.erase(a, b - a);
    caret_ = anchor_ = a;
}

void TextArea::MoveCaret(size_t pos, bool extend) {
    caret_ = std::min(pos, text_.size());
    if (!extend) anchor_ = caret_;
    EnsureVisible();
    Invalidate();
}

void TextArea::Changed() {
    Invalidate();
    if (onChange) onChange(text_);
}

bool TextArea::OnKeyDown(const KeyEvent& e) {
    const bool shift = e.mods.shift, ctrl = e.mods.ctrl;
    HWND hwnd = GetHost() ? GetHost()->Hwnd() : nullptr;
    const Pos pos = PosOf(caret_);
    switch (e.key) {
        case VK_LEFT:
            if (!shift && caret_ != anchor_) MoveCaret(std::min(caret_, anchor_), false);
            else MoveCaret(caret_ > 0 ? caret_ - 1 : 0, shift);
            return true;
        case VK_RIGHT:
            if (!shift && caret_ != anchor_) MoveCaret(std::max(caret_, anchor_), false);
            else MoveCaret(caret_ + 1, shift);
            return true;
        case VK_UP:
            if (pos.line == 0) return false;
            MoveCaret(IndexAt(TextRect().x + XOf(caret_), CaretRect().y - LineH() / 2), shift);
            return true;
        case VK_DOWN:
            if (pos.line + 1 >= Lines().size()) return false;
            MoveCaret(IndexAt(TextRect().x + XOf(caret_), CaretRect().Bottom() + LineH() / 2), shift);
            return true;
        case VK_HOME: MoveCaret(ctrl ? 0 : IndexOf(pos.line, 0), shift); return true;
        case VK_END: MoveCaret(ctrl ? text_.size() : IndexOf(pos.line, std::wstring::npos / 2), shift); return true;
        case VK_BACK:
            if (caret_ == anchor_ && caret_ > 0) anchor_ = caret_ - 1;
            DeleteSelection();
            MoveCaret(caret_, false);
            Changed();
            return true;
        case VK_DELETE:
            if (caret_ == anchor_ && caret_ < text_.size()) anchor_ = caret_ + 1;
            DeleteSelection();
            MoveCaret(caret_, false);
            Changed();
            return true;
        case VK_RETURN:
            if (onSubmit && !shift && !ctrl) {
                onSubmit();
                return true;
            }
            Insert(L"\n");
            return true;
        case VK_ESCAPE: return false;
        default: break;
    }
    if (ctrl) {
        const size_t a = std::min(caret_, anchor_), b = std::max(caret_, anchor_);
        switch (e.key) {
            case 'A':
                anchor_ = 0;
                MoveCaret(text_.size(), true);
                return true;
            case 'C':
                if (b > a) SetClipboardText(hwnd, text_.substr(a, b - a));
                return true;
            case 'X':
                if (b > a) {
                    SetClipboardText(hwnd, text_.substr(a, b - a));
                    DeleteSelection();
                    MoveCaret(caret_, false);
                    Changed();
                }
                return true;
            case 'V': Insert(ClipboardText(hwnd)); return true;
            default: break;
        }
    }
    return !ctrl && !e.mods.alt && e.key >= 0x20 && e.key < VK_F1 && e.key != VK_TAB && e.key != VK_PRIOR &&
           e.key != VK_NEXT;
}

bool TextArea::OnChar(wchar_t ch) {
    if (ch < 0x20) return true;  // Enter / Tab arrive as keys
    Insert(std::wstring_view(&ch, 1));
    return true;
}

// --- Frame --------------------------------------------------------------------------------------

namespace {

constexpr float kRadius = 14;

}  // namespace

Frame::Frame(std::wstring title, float w, float h) {
    width = Length::Px(w);
    if (h > 0) height = Length::Px(h);
    dir = Dir::Column;
    interactive = true;  // swallow clicks on the panel background

    header_ = Add<Widget>();
    header_->dir = Dir::Column;
    header_->width = Length::Grow();
    header_->pad = Insets::All(9);
    header_->gap = 12;
    auto* row = header_->Add<Widget>();
    row->dir = Dir::Row;
    row->align = Align::Center;
    row->width = Length::Grow();
    row->height = Length::Px(32);
    row->pad = {10, 0, 0, 0};
    auto* label = row->Add<Label>(std::move(title), TextStyle::Body, Tone::Secondary);
    label->width = Length::Grow();
    auto* close = row->Add<IconButton>("close", L"", 32, 24);
    close->focusable = false;  // Esc closes; keep Tab on the content
    close->onClick = [this] { Close(); };

    body_ = Add<Widget>();
    body_->dir = Dir::Column;
    body_->width = Length::Grow();
    if (h > 0) body_->height = Length::Grow();
    body_->pad = {20, 22, 20, 22};
    body_->gap = 20;

    footer_ = Add<Widget>();
    footer_->dir = Dir::Row;
    footer_->align = Align::Center;
    footer_->width = Length::Grow();
    footer_->height = Length::Px(72);
    footer_->pad = Insets::All(20);
    footer_->gap = 12;
}

Frame::~Frame() = default;

Widget* Frame::Toolbar() {
    if (!toolbar_) {
        toolbar_ = header_->Add<Widget>();
        toolbar_->dir = Dir::Row;
        toolbar_->align = Align::Center;
        toolbar_->width = Length::Grow();
        toolbar_->height = Length::Px(34);
        toolbar_->gap = 10;
    }
    return toolbar_;
}

void Frame::Close() {
    // The frame may sit inside a larger popup (Customize Player): close that.
    Widget* top = this;
    while (top->Parent()) top = top->Parent();
    if (Host* host = GetHost()) host->ClosePopup(top);
}

void Frame::Paint(Painter& p) {
    const Palette& c = P();
    p.Shadow(rect, kRadius, 15, 8);
    p.Fill(rect, c.bgPanel, kRadius);
    p.PushRoundClip(rect, kRadius);
    p.Fill(header_->rect, c.bgDeep);
    p.PopClip();
    p.Stroke(rect, c.lineSubtle, kRadius);
}

bool Frame::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_ESCAPE) {
        Close();
        return true;
    }
    if (e.key == VK_RETURN && onSubmit && !e.mods.alt) {
        Host* host = GetHost();
        Widget* f = host ? host->Focus() : nullptr;
        // A focused button handles Enter itself (the host clicks it).
        if (f && f != this && f->interactive && !f->WantsTextInput()) return false;
        onSubmit();
        return true;
    }
    return false;
}

Frame* ShowFrame(Host* host, std::unique_ptr<Frame> frame, Widget* focus) {
    if (!host) return nullptr;
    Frame* raw = frame.get();
    PopupOptions o;
    o.place = Place::Center;
    o.modal = true;
    o.scrim = true;
    o.dismissOnOutside = false;
    o.dismissOnEscape = true;
    o.onDismiss = [cb = raw->Closed()] {
        if (*cb) (*cb)();
    };
    host->ShowPopup(std::move(frame), o);
    if (focus) host->SetFocus(focus, false);
    return raw;
}

Widget* AddFieldHeader(Widget* parent, const std::wstring& title, const std::wstring& desc, const std::wstring& suffix) {
    auto* box = parent->Add<Widget>();
    box->dir = Dir::Column;
    box->width = Length::Grow();
    box->gap = 5;
    auto* row = box->Add<Widget>();
    row->dir = Dir::Row;
    row->align = Align::Center;
    row->gap = 6;
    auto* t = row->Add<Label>(title, TextStyle::Large);
    t->color = SoftText;
    if (!suffix.empty()) row->Add<Label>(suffix, TextStyle::Large, Tone::Tertiary);
    if (!desc.empty()) {
        auto* d = box->Add<Label>(desc, TextStyle::Body, Tone::Tertiary);
        d->wrap = true;
        d->width = Length::Grow();
    }
    return box;
}

namespace {

class Separator : public Widget {
public:
    Separator() {
        width = Length::Grow();
        height = Length::Px(1);
    }
    void Paint(Painter& p) override { p.Fill(rect, Color::Mix(P().line, P().lineSubtle, 0.3f)); }
};

}  // namespace

void AddSeparator(Widget* parent) {
    parent->Add<Separator>();
}

// --- pickers ---------------------------------------------------------------------------------

namespace {

void SetInitialFolder(IFileDialog* dlg, const std::wstring& folder) {
    if (folder.empty()) return;
    gfx::ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
        dlg->SetFolder(item.Get());
}

std::wstring ItemPath(IShellItem* item) {
    std::wstring out;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        out = path;
        CoTaskMemFree(path);
    }
    return out;
}

}  // namespace

std::vector<std::wstring> PickFiles(HWND owner, bool multi, const std::vector<FileFilter>& filters,
                                    const std::wstring& initialFolder) {
    std::vector<std::wstring> out;
    gfx::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(opts);
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const FileFilter& f : filters) specs.push_back({f.name.c_str(), f.spec.c_str()});
    if (!specs.empty()) dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    SetInitialFolder(dlg.Get(), initialFolder);
    if (FAILED(dlg->Show(owner))) return out;
    gfx::ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return out;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        gfx::ComPtr<IShellItem> item;
        if (SUCCEEDED(items->GetItemAt(i, &item))) {
            std::wstring p = ItemPath(item.Get());
            if (!p.empty()) out.push_back(std::move(p));
        }
    }
    return out;
}

std::wstring PickFolder(HWND owner, const std::wstring& initialFolder) {
    gfx::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    SetInitialFolder(dlg.Get(), initialFolder);
    if (FAILED(dlg->Show(owner))) return {};
    gfx::ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return {};
    return ItemPath(item.Get());
}

std::wstring ClipboardText(HWND owner) {
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::wstring Breadcrumb(const std::wstring& path, size_t maxParts) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i)
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (i > start) parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    if (!parts.empty() && parts.front().size() == 2 && parts.front()[1] == L':' && parts.size() > 1)
        parts.erase(parts.begin());
    if (parts.size() > maxParts) parts.erase(parts.begin(), parts.end() - static_cast<std::ptrdiff_t>(maxParts));
    std::wstring out;
    for (const auto& p : parts) out += (out.empty() ? L"" : L" / ") + p;
    return out.empty() ? path : out;
}

std::wstring FormatBytes(double bytes) {
    // Same units and decimal comma as the library lists ("105 МБ", "1,2 ГБ").
    return lib::FormatSize(static_cast<uint64_t>(std::max(0.0, bytes)));
}

float FitWidth(Host* host, float designWidth) {
    if (!host) return designWidth;
    return std::min(designWidth, std::floor(host->ClientSize().w - 48));
}

float FitHeight(Host* host, float designHeight) {
    if (!host) return designHeight;
    return std::min(designHeight, std::floor(host->ClientSize().h - 136));  // title bar 40 + control bar 56 + margins
}

}  // namespace app::dlg
