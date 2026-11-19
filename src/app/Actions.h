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
    // Pick a word in the subtitles from the keyboard (SubWords.h).
    SubtitleWords,
    // Opens or closes the audio meters window (Settings > Interface > Separate windows).
    AudioMeters,
    // Ctrl+K: one search over the commands and the settings.
    CommandPalette,
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
// "Subtitles (V)": the key as bound now (Settings > Controls can change it); `tip` alone when unbound.
std::wstring WithKey(const std::wstring& tip, Action a);
// Key names follow the UI language ("Пробел", "Громкость +"); `localized` false gives the
// English ones ("Space"), e.g. so a search for either finds the key.
std::wstring KeyText(UINT vk, const ui::Mods& mods, bool localized = true);
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
// The player waited for a file's first frame (App::PlayerPending): shows the message it held
// back meanwhile (`show`), or drops it when the player did not come.
void ReleaseHeldOsd(bool show);
// A track was chosen anywhere (Player::onTrackChoice): "Audio 2/2: Russian" once mpv switched.
void OnTrackChoice(const char* prop);
// A message from elsewhere in the player (the next-episode card). It joins the "Resuming from"
// message the same file showed a moment before, so neither hides the other.
void ShowOsd(const std::wstring& text, const char* icon);
// Mouse on the video, as set in Settings > Controls > Mouse. Each returns true when it
// did something. `delta` is in wheel notches (positive = up/right).
bool VideoSingleClick();
bool VideoDoubleClick();
bool VideoWheel(float delta, bool horizontal);
// Volume by `delta` notches at the Settings step (the video and the bar's volume control),
// unmuting; partial notches (touchpads) add up. `toast` shows the new level.
void WheelVolume(float delta, bool toast);
// Speaker icon for a volume level ("close_value-none" when muted).
const char* VolumeIcon(double volume, bool mute);
bool Enabled(Action a);
// The command palette (Ctrl+K) over the main window.
void ShowCommandPalette();

}  // namespace app
