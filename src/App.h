#pragma once
#include "Player.h"
#include "Renderer.h"
#include "Ui.h"

#include <string>
#include <vector>

inline constexpr wchar_t kMainWindowClass[] = L"LumenPlayerWindow";
inline constexpr ULONG_PTR kCopyDataOpen = 0x4C554D31;  // 'LUM1'

class App {
public:
    int Run(HINSTANCE instance, const std::wstring& initialFile);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    bool CreateWindows(HINSTANCE instance);
    void ApplyWindowAttributes();
    void LayoutChildren();
    void PaintSkeleton(HDC dc);
    void Render();
    void RequestRender();
    void SyncModel();
    float Scale() const { return static_cast<float>(dpi_) / 96.0f; }
    D2D1_POINT_2F ToDip(LPARAM lp) const;

    LRESULT OnNcHitTest(LPARAM lp);
    void OnMouseMove(float x, float y);
    void OnLButtonDown(float x, float y);
    void OnLButtonUp(float x, float y);
    void OnDoubleClick(float x, float y);
    void OnKeyDown(WPARAM key);
    void SetHover(Hit hit, int menuIndex = -1);
    void Activate(Hit hit, int menuIndex);

    void OpenFile(const std::wstring& path);
    void ShowOpenDialog();
    void PlaySibling(int delta);
    void ChangeVolume(double delta);
    void ChangeSpeed(double delta, bool reset = false);
    void ToggleFullscreen();
    void ToggleMaximize();
    void ShowControls();
    void HideControlsIfIdle();
    void ShowToast(const std::wstring& text);
    void StartAnimation();
    void StepAnimation();
    void ToggleMenu(Hit anchor);
    void CloseMenu();
    void ToastSelectedTrack(const wchar_t* type);

    void OnPlayerReady(bool ok);
    void OnPlayerEvents();
    void FitWindowToVideo();

    HWND hwnd_ = nullptr;
    HWND video_ = nullptr;
    UINT dpi_ = 96;

    Renderer renderer_;
    Ui ui_;
    Player player_;
    UiModel model_;
    TrackMenu menu_;

    bool renderPending_ = false;
    bool closing_ = false;
    bool fullscreen_ = false;
    WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};
    bool trackingMouse_ = false;
    bool trackingNcMouse_ = false;
    bool volumeDragging_ = false;
    bool cursorHidden_ = false;
    POINT lastMouse_{-1, -1};
    float barTarget_ = 1.0f;
    float toastTarget_ = 0.0f;
    bool animating_ = false;

    std::wstring pendingFile_;
    std::vector<std::wstring> siblings_;
    int siblingIndex_ = -1;
    bool fitPending_ = false;
    const wchar_t* pendingTrackToast_ = nullptr;
    long long lastSecond_ = -1;
    int lastThumbPx_ = -1;
};
