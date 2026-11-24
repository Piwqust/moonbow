#pragma once
// "Listening to" / "Watching" on the user's Discord profile (privacy.discordStatus: off by default,
// "music" or "all"). Moonbow talks only to the Discord app on this computer, over its local pipe
// (\\.\pipe\discord-ipc-N, Discord's RPC protocol); the Discord app shares the status.
//
// - The pipe lives on a worker thread that starts the first time there is something to show, so
//   a missing or closed Discord never blocks the UI. It reconnects every 15 s while needed.
// - Updates are coalesced: the latest status goes out at most once every 4 s (Discord allows
//   5 updates in 20 s); a pause or a seek is one update, the playback clock runs in Discord.
// - The application is the user's own (privacy.discordAppId, its Application ID); without one it
//   stays off.
//
// UI thread only (the worker is internal).
namespace app {

void InitDiscordPresence();
void ShutdownDiscordPresence();  // at exit: clears the status and stops the worker

}  // namespace app
