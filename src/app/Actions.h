#pragma once
#include "ui/Widget.h"

#include <string>
#include <vector>

// Every user command with its default keyboard shortcut. The same table drives
// key handling, menus, the "Keyboard Shortcuts" dialog and Settings > Controls.
namespace app {

enum class Action : int {
    None,
    // Playback
    PlayPause, Stop, Previous, Next, SeekForward, SeekBackward, SeekForwardLong, SeekBackwardLong,
    SeekForwardShort, SeekBackwardShort, FrameStep, FrameBackStep, SpeedUp, SpeedDown, SpeedReset,
    NextChapter, PreviousChapter, Replay, ToggleRepeat, ToggleShuffle, ABLoop,
    // Audio
    VolumeUp, VolumeDown, Mute, CycleAudio, AudioDelayUp, AudioDelayDown,
    // Subtitles
    CycleSubtitles, ToggleSubtitles, SubtitleDelayUp, SubtitleDelayDown, SubtitleBigger, SubtitleSmaller,
    // Video
    Fullscreen, ExitFullscreen, HideUI, PictureInPicture, Snapshot, Record, ZoomIn, ZoomOut, ZoomReset,
    CycleAspect, Rotate, Deinterlace, ToggleStats,
    // Media & navigation
    OpenFile, OpenFolder, OpenNetwork, OpenSubtitles, CloseMedia, GoHome, ToggleSidebar, TogglePlaylist,
    Search, Back, Forward, AddBookmark, ToggleFavorite, ShowInfo, ShowInExplorer,
    // App
    Settings, KeyboardShortcuts, CustomizePlayer, Quit,
    // Jump to 0%, 10% ... 90% of the file (no default keys; the "YouTube" key preset binds 0-9).
    JumpTo0, JumpTo10, JumpTo20, JumpTo30, JumpTo40, JumpTo50, JumpTo60, JumpTo70, JumpTo80, JumpTo90,
    // Settings > Controls > Long skip (5 min by default).
    SeekForwardXL, SeekBackwardXL,
    Count,
};

struct ActionInfo {
    Action action;
    const char* id;            // stable id for settings ("playback.play_pause")
    const wchar_t* label;      // English; translate with Tr()
    const wchar_t* group;      // "Playback", "Audio"...
    std::vector<std::pair<UINT, ui::Mods>> keys;  // default bindings
};

const std::vector<ActionInfo>& AllActions();
const ActionInfo& Info(Action a);
// "Ctrl + O" style text for the first binding of `a` (empty if none).
std::wstring ShortcutText(Action a);
std::wstring KeyText(UINT vk, const ui::Mods& mods);
// The action bound to a key press, honoring user overrides from Config.
Action ActionForKey(const ui::KeyEvent& e);
// Overrides a binding (Settings > Controls). Empty `keys` restores the default.
void SetBindings(Action a, std::vector<std::pair<UINT, ui::Mods>> keys);
const std::vector<std::pair<UINT, ui::Mods>>& Bindings(Action a);

// Runs an action against the app (player, navigation, windows).
void Run(Action a);
// Relative seek with the "0:49 / 10:00" message (keys, the wheel over the seek bar).
void SeekBy(double seconds);
// The language of an ISO 639 code ("rus", "ru", "en-US") in the UI language, or the code itself.
std::wstring LanguageName(const std::wstring& code);
// Shows the on-screen messages that wait for mpv (new track, new chapter).
void OnPlayerChangeForOsd(unsigned changes);
// Mouse on the video, as set in Settings > Controls > Mouse. Each returns true when it
// did something. `delta` is in wheel notches (positive = up/right).
bool VideoSingleClick();
bool VideoDoubleClick();
bool VideoWheel(float delta, bool horizontal);
// Speaker icon for a volume level ("close_value-none" when muted).
const char* VolumeIcon(double volume, bool mute);
bool Enabled(Action a);

}  // namespace app
