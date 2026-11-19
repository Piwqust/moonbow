#pragma once
// Clicking a word in subtitles (MARKET.md P2-7, as in PotPlayer). mpv draws subtitles in its own
// window, so Moonbow has no word geometry: on pause it draws the current subtitle itself, in about
// the same place and style, and hides mpv's copy (sub-visibility) while it shows. Words under the
// pointer underline; a click, a shift-click or a drag opens a menu: copy the word or the phrase,
// look it up in a dictionary or a translator (the browser opens a link built from a template in
// Settings > Subtitles; Moonbow itself sends nothing). W does the same from the keyboard.
//
// While playing it costs nothing: mpv properties are observed only while paused.
#include "app/Signal.h"
#include "app/player/SubStyle.h"
#include "app/player/SubText.h"
#include "core/Config.h"
#include "gfx/Gpu.h"
#include "ui/Controls.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app {

class MainWindow;

class SubtitleWords : public ui::Widget {
public:
    explicit SubtitleWords(MainWindow* window);
    ~SubtitleWords() override;

    // PlayerView::OnLayout: the rect mpv's window covers. Lays the words out again only when
    // something they depend on changed. Then Arrange(Bounds()).
    void Place(const ui::Rect& video);
    ui::Rect Bounds() const { return bounds_; }
    // mpv's subtitles are hidden because this overlay shows them.
    bool HidesMpvSubs() const { return hid_; }
    // A click on the subtitle line of a playing video, when a click doesn't pause by itself
    // (Settings > Controls > Mouse): pauses so the words come up. `band` is where subtitles sit.
    bool ClaimVideoClick(float x, float y, const ui::Rect& band);
    // Called on every paint of the player: the player's settings panel holds a "Show subtitles"
    // switch, so the overlay steps aside (and mpv shows its own subtitles) while it is open.
    void Frame();
    // The "Pick a word in subtitles" action: pauses, shows the words and moves keyboard focus
    // to them (arrows move, Shift+arrows select, Enter opens the menu, Esc closes).
    void StartKeyboard();
    // "Hide subtitles" while the overlay shows: mpv's subtitles are already hidden, keep them so.
    void ReleaseKeepHidden();
    // A frame step pauses again right after one frame: don't flash mpv's subtitles in between.
    void ExpectFrameStep();

    // PlayerView: what a double click or a right click away from the words does on the video.
    std::function<bool(const ui::MouseEvent&)> forwardDoubleClick;
    std::function<void(const ui::MouseEvent&)> fallbackMenu;

    Widget* HitTest(float x, float y) override;
    void Paint(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnDoubleClick(const ui::MouseEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    void OnMouseLeave() override;
    void OnClick() override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    void OnFocusChanged(bool focused) override;
    void DebugInfo(std::string& out) const override;

private:
    // How one block is drawn, in window DIPs: from the Moonbow style or the .ass script's style.
    struct Look {
        std::wstring font;
        float line = 20;  // line height: libass sizes fonts by it
        bool bold = false, italic = false;
        int weight = 0;  // 0: from `bold`
        ui::Color text, outlineColor, back;
        float outline = 0, shadow = 0;
        enum Background : uint8_t { None, Lines, Box } background = None;
    };
    struct FontMetrics {
        std::wstring name;
        bool bold = false;
        float em = 0.89f, ascent = 0.8f;  // em size and ascent per line height
    };
    struct Block {
        std::wstring text;
        int align = 2;
        bool rtl = false;
        bool secondary = false;
        Look look;
        gfx::ComPtr<IDWriteTextLayout> layout;
        float ox = 0, oy = 0;  // layout origin, window DIPs
        ui::Rect box;
        std::vector<ui::Rect> lines;  // per-line boxes ("Box behind each line")
        gfx::ComPtr<ID2D1CommandList> scene;
    };
    struct Word {
        int block = 0;
        uint32_t start = 0, length = 0;
        ui::Rect r;
    };

    void OnPlayer(unsigned changes);
    void Reevaluate(bool inLayout = false);
    bool Eligible() const;
    void StartObserving();
    void StopObserving();
    void Schedule();  // Reevaluate after the current event batch
    void SetMpvHidden(bool hide);
    void ReadStyle();
    void ReadAssHeader();
    void FontRatios(const std::wstring& name, bool bold, float& em, float& ascent);
    void Rebuild();
    bool Record(ID2D1DeviceContext5* ctx, Block& b);
    void DropPick();  // hover, selection, focus and the word menu
    void ResetInteraction();
    void EnterKeyboard();
    int WordAt(float x, float y, int block = -1) const;
    int FirstWord() const;
    int Step(int from, int step) const;
    int LineStep(int from, int down) const;
    std::wstring SelectedText() const;
    std::wstring PhraseText() const;
    void Select(int a, int b);
    void OpenMenu();
    void Copy(const std::wstring& text);
    void Lookup(const char* key, const std::wstring& text);
    ui::Rect SelectionRect() const;

    MainWindow* window_;
    ui::Rect video_, bounds_;
    std::wstring key_;  // inputs of the last Rebuild

    // Observed while paused.
    std::vector<uint64_t> observers_;
    std::wstring assText_, secondaryText_;
    bool textKnown_ = false, visibilityKnown_ = false;
    bool scheduled_ = false, styleDirty_ = false;
    std::wstring textKey_;  // the lines the selection belongs to
    bool mpvVisible_ = true;  // sub-visibility as mpv reports it
    double subPos_ = 100, subScale_ = 1, secondaryPos_ = 0;
    int64_t marginY_ = 22, secondarySid_ = 0;
    int64_t observedSid_ = 0;  // the track the observers were set up for

    bool hid_ = false, hidConfirmed_ = false, hidSecondary_ = false;
    bool dismissed_ = false;  // Esc, or the user changed visibility: until playback resumes
    bool panelOpen_ = false;
    bool enabled_ = true;     // subtitles.wordPicker
    bool keyboardWanted_ = false, keyboard_ = false;
    double stepUntil_ = 0;
    UINT_PTR stepTimer_ = 0;

    SubtitleStyle style_;
    subtext::AssHeader header_;
    uint64_t headerKey_ = 0;  // the file and track header_ was read for
    bool assMode_ = false;    // a native .ass track drawn with its own styles
    std::vector<FontMetrics> fontCache_;
    float builtScale_ = 0;
    uint32_t generation_ = 0;
    gfx::ComPtr<ID2D1DeviceContext> recorder_;  // records the blocks' command lists
    std::vector<Block> blocks_;
    std::vector<Word> words_;  // top to bottom, each block in logical order

    int hover_ = -1, anchor_ = -1, selA_ = -1, selB_ = -1, focus_ = -1, pressed_ = -1;
    bool dragging_ = false, clickPending_ = false;
    Widget* menu_ = nullptr;
    bool openingMenu_ = false;
    // Automation runs never touch the clipboard or the browser: what would have been copied or
    // opened shows in the tree dump instead.
    std::wstring lastCopy_, lastUrl_;
    int builds_ = 0, buildUs_ = 0;  // layouts made and the last one's time (tree dump)

    ScopedConnection playerConn_, screenConn_;
    Config::Token styleToken_ = 0, enabledToken_ = 0;
};

}  // namespace app
