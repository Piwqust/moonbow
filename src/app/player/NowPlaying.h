#pragma once
#include <string>

namespace app {

// Title, artist and album of the playing file as the player screens show them. For a local file
// the library knows, its cached tags (Moonbow's tag reader: code pages, every artist of a list)
// win over mpv's, so the music view and the library agree; mpv's fill what the library lacks.
struct PlayingTags {
    std::wstring title, artist, album;
};
// UI thread; the reference stays valid until the next call.
const PlayingTags& CurrentTags();

}  // namespace app
