#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ui {
class Host;
}

// Entry points of the dialogs module (src/app/dialogs) and the settings
// window (src/app/settings). All run on the UI thread and return immediately.
namespace app {

// Called once after the main window and the stores are up (App::Run): installs
// the dialogs module's app-wide hooks (remembered subtitle pairs).
void InitDialogs();

void ShowCustomizePlayerDialog();
void ShowKeyboardShortcutsDialog();
void ShowNetworkStreamDialog();
// A file the New Playlist dialog starts with (the item "Add to Playlist › New Playlist…" was
// opened on). Title and length are kept when it is added.
struct NewPlaylistFile {
    std::wstring path, title;
    double duration = 0;
};
// Creates a playlist; `done` receives the new playlist id (empty if cancelled). `files` are listed
// in the dialog (the user can remove them) and added on Create.
void ShowNewPlaylistDialog(std::function<void(const std::string& playlistId)> done = {},
                           std::vector<NewPlaylistFile> files = {});
// "Add Subtitles to Playlist/Folder": pairs subtitle files with videos by name.
void ShowAddSubtitlesDialog(const std::wstring& folderOrPlaylist = {});
// OpenSubtitles search for a video (the playing file when empty); explains the setting when off.
void ShowOnlineSubtitlesDialog(const std::wstring& videoPath = {});
// TMDB: pick another movie or series for a library video ("Find another match").
void ShowMetadataMatchDialog(const std::wstring& videoPath);
void ShowMediaInfoDialog();
// Version, libmpv/FFmpeg versions and licences; over `host` (Settings) or the main window.
void ShowAboutDialog(ui::Host* host = nullptr);
// What the versions after `since` up to this one bring (setup/ReleaseNotes.h); all of this
// version's notes when `since` is empty. Nothing when there are none.
void ShowWhatsNew(const std::wstring& since, ui::Host* host = nullptr);

// First-run setup window; `done` runs after it closes (finished or skipped).
void ShowOnboarding(std::function<void()> done);

// Separate settings window; `category` is a schema category id ("general", "video"...).
// `query` opens it on a search for those words (the command palette's settings).
// `section`: the part of the page a link in the app points at ("*" = the whole page). The page then
// shows every row for this visit even in Basic mode, scrolled to that section.
void ShowSettingsWindow(const std::string& category, const std::wstring& query = {}, const std::string& section = {});
void CloseSettingsWindow();

}  // namespace app
