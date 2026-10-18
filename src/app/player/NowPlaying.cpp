#include "app/player/NowPlaying.h"

#include "app/App.h"

namespace app {

const PlayingTags& CurrentTags() {
    // Painted every frame by the music view: the library lookup (a path key) runs only when the
    // file, mpv's tags or the library snapshot changed.
    static PlayingTags tags, mpv;
    static std::wstring path;
    // Held, not a raw pointer: a new snapshot could reuse a freed one's address.
    static std::shared_ptr<const library::Snapshot> snapshot;
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    const QueueItem* item = app.queue().CurrentItem();
    const std::wstring& now = !s.path.empty() ? s.path : item ? item->path : s.path;
    const auto snap = app.library().Current();
    if (now == path && snap == snapshot && s.title == mpv.title && s.artist == mpv.artist && s.album == mpv.album)
        return tags;
    path = now;
    snapshot = snap;
    mpv = {s.title, s.artist, s.album};
    tags = mpv;
    const library::MediaItem* m = snap && path.find(L"://") == std::wstring::npos ? snap->FindItem(path) : nullptr;
    if (m && !m->title.empty()) tags.title = m->title;
    if (m && !m->artist.empty()) tags.artist = m->artist;
    if (m && !m->album.empty()) tags.album = m->album;
    return tags;
}

}  // namespace app
