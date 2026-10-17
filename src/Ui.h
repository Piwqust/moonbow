#pragma once
#include "Player.h"
#include "Renderer.h"

#include <string>
#include <vector>

enum class Hit {
    None,
    Caption,
    Minimize,
    Maximize,
    Close,
    Video,
    Open,
    Prev,
    Play,
    Next,
    Seek,
    Mute,
    Volume,
    Audio,
    Subs,
    Fullscreen,
    Menu,
    MenuItem,
    ControlBar,
};

struct MenuItem {
    std::wstring label;
    int64_t id = 0;  // 0 = "off"
    bool checked = false;
};

struct TrackMenu {
    bool open = false;
    Hit anchor = Hit::None;
    const char* prop = nullptr;  // "aid" / "sid"
    std::wstring header;
    std::vector<MenuItem> items;
    int hover = -1;
};

// Everything the UI needs to draw one frame.
struct UiModel {
    float width = 0;   // DIPs
    float height = 0;  // DIPs
    bool fullscreen = false;
    bool maximized = false;
    bool active = true;
    float barOpacity = 1.0f;

    const PlayerState* player = nullptr;
    bool engineReady = false;
    std::wstring engineError;
    bool hasPrev = false;
    bool hasNext = false;

    Hit hover = Hit::None;
    Hit pressed = Hit::None;
    float mouseX = 0;
    float mouseY = 0;
    bool seekDragging = false;
    double seekDragPos = 0;

    std::wstring toast;
    float toastOpacity = 0;

    TrackMenu* menu = nullptr;
};

class Ui {
public:
    bool Init(Renderer& renderer);
    void Layout(const UiModel& m);
    void Draw(ID2D1DeviceContext* ctx, const UiModel& m);
    Hit HitTest(float x, float y, const UiModel& m, int* menuIndex) const;

    D2D1_RECT_F VideoRect() const { return video_; }
    D2D1_RECT_F BarRect() const { return bar_; }
    double SeekFraction(float x) const;
    double VolumeFromX(float x) const;

    static float TitleHeight(bool fullscreen);

private:
    void DrawTitleBar(const UiModel& m);
    void DrawEmptyState(const UiModel& m);
    void DrawControlBar(const UiModel& m);
    void DrawSeek(const UiModel& m);
    void DrawVolume(const UiModel& m);
    void DrawMenu(const UiModel& m);
    void DrawToast(const UiModel& m);
    void DrawIconButton(const D2D1_RECT_F& r, const wchar_t* glyph, Hit id, const UiModel& m, bool enabled,
                        bool active = false);
    void DrawText(const std::wstring& text, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, D2D1_COLOR_F color);
    void Fill(const D2D1_RECT_F& r, D2D1_COLOR_F color, float radius = 0);
    D2D1_SIZE_F Measure(const std::wstring& text, IDWriteTextFormat* fmt, float maxWidth) const;
    D2D1_COLOR_F Fade(D2D1_COLOR_F c) const;
    D2D1_RECT_F MenuRect(const UiModel& m) const;

    Renderer* renderer_ = nullptr;
    ID2D1DeviceContext* ctx_ = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush_;
    float fade_ = 1.0f;

    ComPtr<IDWriteTextFormat> fmtTitle_;
    ComPtr<IDWriteTextFormat> fmtLabel_;
    ComPtr<IDWriteTextFormat> fmtCenter_;
    ComPtr<IDWriteTextFormat> fmtTimeLeft_;
    ComPtr<IDWriteTextFormat> fmtTimeRight_;
    ComPtr<IDWriteTextFormat> fmtEmpty_;
    ComPtr<IDWriteTextFormat> fmtIcon_;
    ComPtr<IDWriteTextFormat> fmtIconLarge_;
    ComPtr<IDWriteTextFormat> fmtIconSmall_;

    // Layout, recomputed by Layout().
    D2D1_RECT_F title_{}, minimize_{}, maximize_{}, close_{};
    D2D1_RECT_F video_{}, bar_{};
    D2D1_RECT_F prev_{}, play_{}, next_{}, timeLeft_{}, seek_{}, timeRight_{};
    D2D1_RECT_F mute_{}, volume_{}, audio_{}, subs_{}, fullscreen_{};
    D2D1_RECT_F open_{};
    bool withHours_ = false;
};
