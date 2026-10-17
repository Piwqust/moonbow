#pragma once
// Shared building blocks of the dialogs module: the modal frame used by every
// dialog in design/ref (dark header band, body, footer with 100x32 buttons),
// design buttons, a multi-line text area, key caps, dashed outlines and the
// Windows file pickers.
#include "ui/Controls.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app::dlg {

// Text color between primary and secondary (#C6C6C6 in the design: field titles, row labels).
ui::Color SoftText();

// --- painting helpers ------------------------------------------------------------------

void DashedLine(ui::Painter& p, float x1, float x2, float y, ui::Color c, float dash = 8, float gapLen = 8);
void DashedRect(ui::Painter& p, const ui::Rect& r, ui::Color c, float dash = 4, float gapLen = 4);
// "Shift + S" -> {"Shift", "S"}.
std::vector<std::wstring> SplitKeys(const std::wstring& text);

// --- buttons ---------------------------------------------------------------------------

enum class BtnStyle : uint8_t {
    Neutral,  // bgChrome, secondary text (Cancel, Back)
    Accent,   // accentDim with accent text (dialog Save)
    Solid,    // accent fill, onAccent text (onboarding Next / Finish)
};

// The 100x32 radius-6 buttons of the design. Grows for longer labels.
class DButton : public ui::Widget {
public:
    DButton(std::wstring text, BtnStyle style, ui::TextStyle textStyle = ui::TextStyle::BodyStrong);
    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }
    BtnStyle style;
    ui::TextStyle textStyle;
    std::string icon;
    float minWidth = 100;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;

private:
    std::wstring text_;
};

// Dropdown with the toolbar look of the dialogs (bgRaised field, radius 6).
class FieldDropdown : public ui::Dropdown {
public:
    using Dropdown::Dropdown;
    void Paint(ui::Painter& p) override;
};

// A key cap ("Space", "F", "Shift") as in the onboarding shortcuts step.
class KeyCap : public ui::Widget {
public:
    explicit KeyCap(std::wstring text, bool large = true);
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;

private:
    std::wstring text_;
    bool large_;
};

// --- multi-line text input -------------------------------------------------------------

// Plain multi-line editor (no wrapping). Enter calls onSubmit when set, Shift+Enter
// (or Enter without onSubmit) inserts a line break. Clipboard, selection, IME caret.
class TextArea : public ui::Widget {
public:
    explicit TextArea(std::wstring placeholder = {});
    const std::wstring& Text() const { return text_; }
    void SetText(std::wstring text, bool notify = false);
    std::wstring placeholder;
    std::function<void(const std::wstring&)> onChange;
    std::function<void()> onSubmit;

    ui::Size MeasureContent(float availW, float) override { return {availW, 120}; }
    void Paint(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    bool OnChar(wchar_t ch) override;
    bool WantsTextInput() const override { return true; }
    ui::Rect CaretRect() const override;

private:
    struct Pos {
        size_t line = 0, col = 0;
    };
    std::vector<std::pair<size_t, size_t>> Lines() const;  // [start, end) per line
    Pos PosOf(size_t index) const;
    size_t IndexOf(size_t line, size_t col) const;
    size_t IndexAt(float x, float y) const;
    float XOf(size_t index) const;
    float LineH() const;
    ui::Rect TextRect() const;
    void Insert(std::wstring_view s);
    void DeleteSelection();
    void MoveCaret(size_t pos, bool extend);
    void Changed();
    void EnsureVisible();

    std::wstring text_;
    size_t caret_ = 0, anchor_ = 0;
    float scroll_ = 0;
    bool selecting_ = false;
};

// --- modal frame ---------------------------------------------------------------------------

// Dialog frame from design/ref (Keyboard_Shortcuts, Open_network_stream, new_playlist,
// Add_Subtitles_*): 14 px radius, a bgDeep header band with the title and a close button
// (plus an optional toolbar row), a body and a 72 px footer.
class Frame : public ui::Widget {
public:
    // height 0 = hug the content.
    Frame(std::wstring title, float width, float height = 0);
    ~Frame() override;
    Widget* Toolbar();  // created on first use, sits in the header band
    Widget* Body() const { return body_; }
    Widget* Footer() const { return footer_; }
    void Close();

    std::function<void()> onSubmit;  // Enter outside buttons / multi-line fields
    // Runs after the popup closed for any reason (the frame is already destroyed then).
    void OnClosed(std::function<void()> fn) { *closed_ = std::move(fn); }
    std::shared_ptr<std::function<void()>> Closed() const { return closed_; }

    void Paint(ui::Painter& p) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;

private:
    std::shared_ptr<std::function<void()>> closed_ = std::make_shared<std::function<void()>>();
    Widget* header_ = nullptr;
    Widget* toolbar_ = nullptr;
    Widget* body_ = nullptr;
    Widget* footer_ = nullptr;
};

// Shows the frame centered with a scrim (modal). Focuses `focus` (or the first focusable).
Frame* ShowFrame(ui::Host* host, std::unique_ptr<Frame> frame, ui::Widget* focus = nullptr);

// Title + description block used by the dialog bodies ("Network URL" / "You can enter...").
ui::Widget* AddFieldHeader(ui::Widget* parent, const std::wstring& title, const std::wstring& desc,
                           const std::wstring& suffix = {});
// 1 px separator line (line.subtle) across the parent.
void AddSeparator(ui::Widget* parent);

// --- Windows pickers -------------------------------------------------------------------

struct FileFilter {
    std::wstring name, spec;  // L"Subtitles", L"*.srt;*.ass"
};
std::vector<std::wstring> PickFiles(HWND owner, bool multi, const std::vector<FileFilter>& filters,
                                    const std::wstring& initialFolder = {});
std::wstring PickFolder(HWND owner, const std::wstring& initialFolder = {});

std::wstring ClipboardText(HWND owner);
void SetClipboardText(HWND owner, const std::wstring& text);

// "D:\Video\Shows\S01" -> "Video / Shows / S01" (drive dropped, at most `maxParts` tail parts).
std::wstring Breadcrumb(const std::wstring& path, size_t maxParts = 4);

// "1.4 GB" style sizes.
std::wstring FormatBytes(double bytes);

// Dialog width that fits the main window (design width, minus a margin on small windows).
float FitWidth(ui::Host* host, float designWidth);
float FitHeight(ui::Host* host, float designHeight);

// --- module internals ------------------------------------------------------------------

// AddSubtitles.cpp: re-attaches remembered subtitle pairs whenever a file starts.
void InstallSubtitleMemory();

}  // namespace app::dlg
