#pragma once
#include <functional>
#include <string>

// Entry points of the dialogs module (src/app/dialogs) and the settings
// window (src/app/settings). All run on the UI thread and return immediately.
namespace app {

// Called once after the main window and the stores are up (App::Run): installs
// the dialogs module's app-wide hooks (remembered subtitle pairs).
void InitDialogs();

void ShowCustomizePlayerDialog();
void ShowKeyboardShortcutsDialog();
void ShowNetworkStreamDialog();
// Creates a playlist; `done` receives the new playlist id (empty if cancelled).
void ShowNewPlaylistDialog(std::function<void(const std::string& playlistId)> done = {});
// "Add Subtitles to Playlist/Folder": pairs subtitle files with videos by name.
void ShowAddSubtitlesDialog(const std::wstring& folderOrPlaylist = {});
void ShowMediaInfoDialog();

// First-run setup window; `done` runs after it closes (finished or skipped).
void ShowOnboarding(std::function<void()> done);

// Separate settings window; `category` is a schema category id ("general", "video"...).
void ShowSettingsWindow(const std::string& category);
void CloseSettingsWindow();

}  // namespace app
