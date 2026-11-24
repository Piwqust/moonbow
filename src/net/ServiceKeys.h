#pragma once
// The keys of the online services this build of Moonbow carries, or empty strings. Official builds
// get them at build time (tools/gen-service-keys.ps1); the repository holds none. OpenSubtitles
// requires one key per application and forbids asking users for their own, so the settings have no
// key fields; a hidden setting can still override a key for builds made without one.
#include <string>

namespace net::keys {

std::string OpenSubtitles();
std::string Tmdb();
std::string LastfmKey();
std::string LastfmSecret();
std::string DiscordApp();  // the Discord application's client id (for the "Listening to" status)

}  // namespace net::keys
