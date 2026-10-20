// Playlists: list (Create / Import), playlist page (Play all, Shuffle, drag to reorder,
// remove, rename, delete, export .m3u8).
#include "Util.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/library/Pages.h"
#include "library/MediaTypes.h"

#include <shlwapi.h>

#include <algorithm>
#include <random>

namespace app::lib {

using namespace ui;

namespace {

std::vector<QueueItem> PlaylistQueue(const library::Playlist& p) {
    std::vector<QueueItem> q;
    q.reserve(p.items.size());
    for (const auto& it : p.items) q.push_back(MakeQueueItem(it.path, it.title, it.duration));
    return q;
}

void PlayPlaylist(const std::string& id, bool shuffle, int index = 0) {
    const library::Playlist* p = TheApp().playlists().Find(id);
    if (!p || p->items.empty()) return;
    auto q = PlaylistQueue(*p);
    if (shuffle) {
        std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
        index = static_cast<int>(rng() % q.size());
    }
    CountPlaylistPlay(id);
    TheApp().queue().SetShuffle(false);
    PlayQueue(std::move(q), index, p->name);
    if (shuffle) TheApp().queue().SetShuffle(true);
}

void RenamePlaylist(const std::string& id) {
    const library::Playlist* p = TheApp().playlists().Find(id);
    if (!p) return;
    ShowTextPrompt(Tr(L"Rename playlist"), Tr(L"Name"), p->name, Tr(L"Rename"), [id](const std::wstring& name) {
        TheApp().playlists().Rename(id, name);
        TheApp().playlists().Flush();
        TheApp().libraryChanged.Emit();
    });
}

void DeletePlaylist(const std::string& id, bool leavePage) {
    const library::Playlist* p = TheApp().playlists().Find(id);
    if (!p) return;
    ShowConfirm(Tr(L"Delete playlist"),
                Fmt(Tr(L"“{0}” will be deleted. The media files themselves are not touched."), {p->name}), Tr(L"Delete"),
                [id, leavePage] {
                    auto& lists = TheApp().playlists();
                    const library::Playlist* gone = lists.Find(id);
                    if (!gone) return;
                    // Kept for the toast's Undo, as "Remove from playlist" keeps its item.
                    const library::Playlist copy = *gone;
                    const size_t index = static_cast<size_t>(gone - lists.All().data());
                    const std::string cover = TheApp().UiValue("playlist.cover." + id);
                    lists.Delete(id);
                    TheApp().SetUiValue("playlist.cover." + id, {});
                    lists.Flush();
                    if (leavePage) TheApp().Navigate(Route{Section::Playlists, {}});
                    TheApp().libraryChanged.Emit();
                    Toast(Tr(L"Playlist deleted"), "remove", Tr(L"Undo"), [copy, index, cover] {
                        if (!TheApp().playlists().Restore(copy, index)) return;
                        if (!cover.empty()) TheApp().SetUiValue("playlist.cover." + copy.id, cover);
                        TheApp().playlists().Flush();
                        TheApp().libraryChanged.Emit();
                    });
                });
}

// No question first, and no loss either: the toast brings the items back to their places (UX-17).
void RemoveFromPlaylist(const std::string& id, std::vector<size_t> indices) {
    const library::Playlist* p = TheApp().playlists().Find(id);
    if (!p) return;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::vector<std::pair<size_t, library::PlaylistItem>> removed;
    for (size_t i : indices)
        if (i < p->items.size()) removed.emplace_back(i, p->items[i]);
    if (removed.empty()) return;
    TheApp().playlists().RemoveIndices(id, indices);
    TheApp().libraryChanged.Emit();
    const std::wstring text = removed.size() == 1 ? std::wstring(Tr(L"Removed from playlist"))
                                                  : Fmt(Tr(L"Removed from playlist: {0}"), static_cast<long long>(removed.size()));
    Toast(text, "remove", Tr(L"Undo"), [id, removed] {
        // In ascending order each item lands back at its old index.
        for (const auto& [index, item] : removed)
            if (!TheApp().playlists().Add(id, {item}, static_cast<int>(index))) return;  // the playlist is gone
        TheApp().libraryChanged.Emit();
    });
}

void ExportPlaylist(const std::string& id) {
    const library::Playlist* p = TheApp().playlists().Find(id);
    if (!p) return;
    const std::wstring file = PickPlaylistSaveFile(p->name + L".m3u8");
    if (file.empty()) return;
    if (TheApp().playlists().Export(id, file))
        Toast(Fmt(Tr(L"Exported to {0}"), {FileNameOf(file)}), "save");
    else
        Toast(Tr(L"Couldn't save the playlist file"), "info");
}

void ImportPlaylist() {
    const std::wstring file = PickPlaylistFile();
    if (file.empty()) return;
    const std::string id = TheApp().playlists().Import(file);
    if (id.empty()) {
        Toast(Tr(L"Couldn't read this playlist file"), "info");
        return;
    }
    TheApp().playlists().Flush();
    TheApp().libraryChanged.Emit();
    TheApp().Navigate(Route{Section::Playlists, Utf8ToWide(id)});
}

void CreatePlaylist() {
    ShowNewPlaylistDialog([](const std::string& id) {
        if (id.empty()) return;
        TheApp().libraryChanged.Emit();
        TheApp().Navigate(Route{Section::Playlists, Utf8ToWide(id)});
    });
}

std::vector<MenuItem> PlaylistMenu(const library::Playlist& p, bool onPage) {
    const std::string id = p.id;
    const bool has = !p.items.empty();
    std::vector<MenuItem> m;
    if (!onPage) m.push_back(Item(L"Open", "playlist", [id] { TheApp().Navigate(Route{Section::Playlists, Utf8ToWide(id)}); }));
    m.push_back(Item(L"Play", "playback", [id] { PlayPlaylist(id, false); }, has));
    m.push_back(Item(L"Shuffle", "shuffle_right", [id] { PlayPlaylist(id, true); }, has));
    auto q = PlaylistQueue(p);
    m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(q); }, has));
    const bool videos = std::any_of(p.items.begin(), p.items.end(), [](const library::PlaylistItem& it) {
        return library::KindOf(it.path) == library::MediaKind::Video;
    });
    if (videos) m.push_back(Item(L"Add Subtitles…", "subtitles", [id] { ShowAddSubtitlesDialog(Utf8ToWide(id)); }));
    m.push_back(MenuItem::Separator());
    m.push_back(Item(L"Rename…", "edit", [id] { RenamePlaylist(id); }));
    m.push_back(Item(L"Export Playlist…", "save", [id] { ExportPlaylist(id); }, has));
    m.push_back(Item(L"Delete…", "remove", [id, onPage] { DeletePlaylist(id, onPage); }));
    return m;
}

// --- list of playlists ------------------------------------------------------------------------

class PlaylistsPage : public Page {
public:
    PlaylistsPage(Route r, bool compact) : Page(r, compact, "playlists") {
        list_ = ListModeFor(key_, false);
        Refresh();
    }

    std::wstring CountText(long long n) const override { return CountPlaylists(n); }

    std::vector<MenuItem> SortMenu() override {
        std::vector<MenuItem> m;
        SortMenuItems(m, {{L"Title A-Z", "name"},
                          {L"Title Z-A", "name:desc"},
                          {L"Newest First", "created:desc"},
                          {L"Oldest First", "created"},
                          {L"Recently Changed", "modified:desc"},
                          {L"Most Played", "plays"}});
        return m;
    }

    std::vector<MenuItem> MoreMenu() override {
        return {Item(L"Create Playlist…", "add", [] { CreatePlaylist(); }),
                Item(L"Import Playlist…", "open", [] { ImportPlaylist(); })};
    }

protected:
    GridStyle CardStyle() const override { return GridStyle::SquareCards; }
    unsigned RowColumns() const override { return kColDate; }

    void Build() override {
        // Favorites lead the playlists as a list of their own (UX-17), no longer a tab of History.
        const size_t favs = TheApp().favorites().All().size();
        if (favs > 0 && Matches(Tr(L"Favorites"))) {
            Entry f;
            f.kind = EntryKind::Playlist;
            f.title = Tr(L"Favorites");
            f.subtitle = CountItems(static_cast<long long>(favs));
            f.icon = "favorite";
            f.count = list_ || compact_ ? static_cast<int>(favs) : -1;
            AddRoute(std::move(f), Route{Section::Playlists, L"favorites"});
        }
        Entry create;
        create.kind = EntryKind::Action;
        create.title = Tr(L"Create playlist");
        create.icon = "add";
        AddAction(std::move(create), [] { CreatePlaylist(); });
        if (list_ && !compact_) {
            Entry imp;
            imp.kind = EntryKind::Action;
            imp.title = Tr(L"Import playlist");
            imp.icon = "open";
            AddAction(std::move(imp), [] { ImportPlaylist(); });
        }
        std::vector<const library::Playlist*> v;
        for (const library::Playlist& p : TheApp().playlists().All())
            if (Matches(p.name)) v.push_back(&p);
        const std::string sort = SortValue("name");
        std::stable_sort(v.begin(), v.end(), [&](const library::Playlist* a, const library::Playlist* b) {
            if (sort == "created:desc") return a->created > b->created;
            if (sort == "created") return a->created < b->created;
            if (sort == "modified:desc") return a->modified > b->modified;
            if (sort == "plays") return PlaylistPlays(a->id) > PlaylistPlays(b->id);
            const int c = StrCmpLogicalW(a->name.c_str(), b->name.c_str());
            return sort == "name:desc" ? c > 0 : c < 0;
        });
        for (const library::Playlist* p : v) {
            Entry e;
            e.kind = EntryKind::Playlist;
            e.title = p->name;
            e.subtitle = CountItems(static_cast<long long>(p->items.size()));
            e.key = p->id;
            e.icon = "playlist";
            e.date = p->modified;
            e.count = list_ || compact_ ? static_cast<int>(p->items.size()) : -1;
            e.image = PlaylistImage(*p);
            AddRoute(std::move(e), Route{Section::Playlists, Utf8ToWide(p->id)});
        }
    }

    void FillEmpty(EmptyState& e) override {
        e.Set("playlist", Tr(L"No playlists yet"),
              Tr(L"Collect videos and songs in playlists. Right-click any file and choose “Add to Playlist”."),
              {{Tr(L"Create playlist"), ButtonKind::Primary, "add", [] { CreatePlaylist(); }},
               {Tr(L"Import playlist"), ButtonKind::Secondary, "open", [] { ImportPlaylist(); }}});
    }

    void ShowMenu(int index, Point at) override {
        const Entry* e = EntryAt(index);
        if (!e || e->kind != EntryKind::Playlist || !GetHost()) return;
        if (const library::Playlist* p = TheApp().playlists().Find(e->key))
            ShowContextMenu(GetHost(), at, PlaylistMenu(*p, false));
    }

    void Delete(int index) override {
        const Entry* e = EntryAt(index);
        if (e && e->kind == EntryKind::Playlist) DeletePlaylist(e->key, false);
    }
};

// --- one playlist -------------------------------------------------------------------------------

class PlaylistPage : public Page {
public:
    PlaylistPage(Route r, bool compact) : Page(r, compact, "playlist"), id_(WideToUtf8(r.path)) {
        list_ = ListModeFor(key_, true);
        auto* play = toolbar_->Add<Button>(Tr(L"Play All"), ButtonKind::Primary, "play");
        play->onClick = [this] { PlayPlaylist(id_, false); };
        auto* shuffle = toolbar_->Add<Button>(Tr(L"Shuffle"), ButtonKind::Secondary, "shuffle_right");
        shuffle->onClick = [this] { PlayPlaylist(id_, true); };
        toolbar_->Add<Spacer>();
        summary_ = toolbar_->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
        summary_->visible = !compact;  // no room next to the buttons in the side panel
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        auto c = Page::Breadcrumbs();
        const library::Playlist* p = TheApp().playlists().Find(id_);
        c.push_back({p ? p->name : std::wstring(Tr(L"Playlist")), route_});
        return c;
    }

    // "2 items · 1:30" beside the buttons says it; the header doesn't repeat it (UX-15).
    long long ItemCount() const override { return summary_->visible && toolbar_->visible && filter_.empty() ? -1 : count_; }
    std::wstring CountText(long long n) const override { return songs_ ? CountSongs(n) : CountItems(n); }

    std::vector<MenuItem> SortMenu() override {
        std::vector<MenuItem> m;
        SortMenuItems(m, {{L"Playlist Order", "manual"},
                          {L"Title A-Z", "title"},
                          {L"Title Z-A", "title:desc"},
                          {L"Longest First", "duration:desc"},
                          {L"Shortest First", "duration"}});
        return m;
    }

    std::vector<MenuItem> MoreMenu() override {
        const library::Playlist* p = TheApp().playlists().Find(id_);
        if (!p) return {};
        auto m = PlaylistMenu(*p, true);
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Import Playlist…", "open", [] { ImportPlaylist(); }));
        return m;
    }

protected:
    GridStyle CardStyle() const override { return videos_ ? GridStyle::VideoCards : GridStyle::SquareCards; }
    unsigned RowColumns() const override { return kColSubtitle | kColDuration; }
    bool Reorderable() const override { return SortValue("manual") == "manual"; }

    void Build() override {
        plIndex_.clear();
        const library::Playlist* p = TheApp().playlists().Find(id_);
        toolbar_->visible = p && !p->items.empty();
        if (!p) return;
        queueName_ = p->name;
        struct Row {
            size_t index;
            std::wstring title;
            double duration;
        };
        std::vector<Row> rows;
        size_t videos = 0, songs = 0;
        double total = 0;
        for (size_t i = 0; i < p->items.size(); ++i) {
            const library::PlaylistItem& it = p->items[i];
            const library::MediaItem* m = snap_ && !IsUrl(it.path) ? snap_->FindItem(it.path) : nullptr;
            // A title saved in the playlist (#EXTINF, a stream name) wins unless it's just the file name.
            std::wstring title = !it.title.empty() && !SamePath(it.title, FileNameOf(it.path)) ? it.title
                                                                                               : MediaTitle(it.path, m);
            const double dur = it.duration > 0 ? it.duration : (m ? m->duration : 0);
            total += dur;
            if (library::KindOf(it.path) == library::MediaKind::Video) ++videos;
            if (library::KindOf(it.path) == library::MediaKind::Audio) ++songs;
            if (Matches(title + L" " + it.path)) rows.push_back({i, std::move(title), dur});
        }
        videos_ = videos * 2 >= p->items.size() && !p->items.empty();
        songs_ = songs == p->items.size();
        const std::string sort = SortValue("manual");
        AddColumns(sort, {{kHeadTitle, L"Title", "title"}, {kHeadSubtitle, L"Details", ""}, {kHeadDuration, L"Length", "duration"}});
        if (sort != "manual") {
            std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                if (sort == "duration:desc") return a.duration > b.duration;
                if (sort == "duration") return a.duration < b.duration;
                const int c = StrCmpLogicalW(a.title.c_str(), b.title.c_str());
                return sort == "title:desc" ? c > 0 : c < 0;
            });
        }
        // The queue follows the shown order, so "Play" from a sorted view plays it that way.
        for (const Row& r : rows) {
            const library::PlaylistItem& it = p->items[r.index];
            const library::MediaItem* m = snap_ && !IsUrl(it.path) ? snap_->FindItem(it.path) : nullptr;
            Entry e;
            e.kind = EntryKind::Media;
            e.path = it.path;
            e.title = r.title;
            e.duration = r.duration;
            e.audio = library::KindOf(it.path) != library::MediaKind::Video;
            const bool url = IsUrl(it.path);
            if (!url && !(m && m->offline)) e.image = it.path;
            e.icon = url ? "streaming" : e.audio ? "music" : "video";
            if (m && e.audio)
                e.subtitle = ArtistLabel(m->artist);
            else if (url)
                e.subtitle = Tr(L"Network stream");
            else
                e.subtitle = FileNameOf(library::ParentFolder(it.path));
            const HistoryInfo h = Progress(it.path);
            e.progress = h.progress;
            e.watched = h.watched && !e.audio;
            e.favorite = Favorite(it.path);
            e.ref = static_cast<int>(queue_.size());
            queue_.push_back(MakeQueueItem(it.path, it.title, r.duration));
            const int at = Push(std::move(e));
            plIndex_.resize(static_cast<size_t>(at), kNoItem);  // the column titles' row
            plIndex_.push_back(r.index);
        }
        std::wstring s = CountText(static_cast<long long>(p->items.size()));
        if (total > 0) s += L" · " + FormatDuration(total);
        summary_->SetText(s);
    }

    void Activate(int index) override {
        const Entry* e = EntryAt(index);
        if (!e) return;
        CountPlaylistPlay(id_);
        Page::Activate(index);
    }

    void ExtraMenu(int index, std::vector<MenuItem>& m) override {
        const size_t pi = PlIndex(index);
        if (pi == kNoItem) return;
        const std::string plId = id_;
        m.push_back(MenuItem::Separator());
        // Keyboard and menu reorder, as in the queue (UX-11); the list must show the playlist's order.
        if (grid_->reorderable) {
            const bool up = PlIndex(index - 1) != kNoItem, down = PlIndex(index + 1) != kNoItem;
            MenuItem u = Item(L"Move up", "arrow_up", [this, index] { Move(index, index - 1); }, up);
            u.shortcut = L"Alt+↑";
            m.push_back(std::move(u));
            MenuItem d = Item(L"Move down", "arrow_down", [this, index] { Move(index, index + 1); }, down);
            d.shortcut = L"Alt+↓";
            m.push_back(std::move(d));
            m.push_back(MenuItem::Separator());
        }
        m.push_back(Item(L"Remove from Playlist", "remove", [plId, pi] { RemoveFromPlaylist(plId, {pi}); }));
    }

    void ExtraSelectionMenu(const std::vector<int>& sel, std::vector<MenuItem>& m) override {
        std::vector<size_t> picked;
        for (int i : sel)
            if (PlIndex(i) != kNoItem) picked.push_back(PlIndex(i));
        if (picked.empty()) return;
        const std::string plId = id_;
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Remove from Playlist", "remove", [plId, picked] { RemoveFromPlaylist(plId, picked); }));
    }

    void Delete(int index) override {
        const size_t pi = PlIndex(index);
        if (pi == kNoItem) return;
        // Delete removes every selected row when several are selected.
        const std::vector<int> sel = grid_->Marked(index) ? grid_->Selection() : std::vector<int>{index};
        std::vector<size_t> picked;
        for (int i : sel)
            if (PlIndex(i) != kNoItem) picked.push_back(PlIndex(i));
        if (picked.empty()) picked.push_back(pi);
        index = std::min(index, sel.front());
        RemoveFromPlaylist(id_, picked);
        // Keep the keyboard position on the next row.
        const auto& v = grid_->Entries();
        for (int i = std::min(index, static_cast<int>(v.size()) - 1); i >= 0; --i)
            if (v[static_cast<size_t>(i)].kind == EntryKind::Media) {
                grid_->Select(i, true);
                break;
            }
    }

    void Move(int from, int to) override {
        if (PlIndex(from) == kNoItem || PlIndex(to) == kNoItem) return;
        TheApp().playlists().Move(id_, plIndex_[static_cast<size_t>(from)], plIndex_[static_cast<size_t>(to)]);
        TheApp().libraryChanged.Emit();
        grid_->Select(to, true);
    }

    void FillEmpty(EmptyState& e) override {
        if (!TheApp().playlists().Find(id_)) {
            e.Set("playlist", Tr(L"Playlist not found"), Tr(L"It may have been deleted."),
                  {{Tr(L"All playlists"), ButtonKind::Secondary, "playlist",
                    [] { TheApp().Navigate(Route{Section::Playlists, {}}); }}});
            return;
        }
        e.Set("playlist", Tr(L"This playlist is empty"),
              Tr(L"Right-click videos or songs anywhere in the library and choose “Add to Playlist”."));
    }

private:
    static constexpr size_t kNoItem = static_cast<size_t>(-1);
    // The playlist position of a grid entry; kNoItem for the column titles' row.
    size_t PlIndex(int index) const {
        return index < 0 || index >= static_cast<int>(plIndex_.size()) ? kNoItem : plIndex_[static_cast<size_t>(index)];
    }

    std::string id_;
    bool videos_ = false;
    bool songs_ = false;  // only songs: counted as songs, not items
    Label* summary_ = nullptr;
    std::vector<size_t> plIndex_;  // grid entry -> playlist position
};

}  // namespace

std::unique_ptr<LibraryView> MakePlaylistsPage(const Route& route, bool compact) {
    if (route.path.empty()) return std::make_unique<PlaylistsPage>(route, compact);
    return std::make_unique<PlaylistPage>(route, compact);
}

}  // namespace app::lib
