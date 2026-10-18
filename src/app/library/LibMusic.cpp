// Music: Songs, Albums, Artists, Genres and their detail pages. Uses the tags in the library
// snapshot; genres are read on a worker thread (the snapshot has no genre field).
#include "Util.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/Pages.h"
#include "library/MediaTypes.h"

#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>

namespace app::lib {

using namespace ui;

namespace {

// --- genres ---------------------------------------------------------------------------------

class GenreIndex {
public:
    static GenreIndex& Get() {
        static GenreIndex g;
        return g;
    }
    ~GenreIndex() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

    // Reads genres of audio files that are not known yet; posts the library message when done.
    void Request(const library::Snapshot& snap) {
        if (busy_) return;
        std::vector<std::wstring> todo;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const library::MediaItem& m : snap.items)
                if (m.kind == library::MediaKind::Audio && !m.offline && !genres_.count(library::PathKey(m.path)))
                    todo.push_back(m.path);
        }
        if (todo.empty()) return;
        if (thread_.joinable()) thread_.join();
        busy_ = true;
        HWND hwnd = TheApp().main() ? TheApp().main()->Hwnd() : nullptr;
        thread_ = std::thread([this, todo = std::move(todo), hwnd] {
            const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (const std::wstring& path : todo) {
                if (stop_) break;
                std::wstring genre;
                IPropertyStore* store = nullptr;
                if (SUCCEEDED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store)))) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(store->GetValue(PKEY_Music_Genre, &v)) && v.vt != VT_EMPTY) {
                        PWSTR s = nullptr;
                        if (SUCCEEDED(PropVariantToStringAlloc(v, &s)) && s) {
                            genre = s;
                            CoTaskMemFree(s);
                        }
                    }
                    PropVariantClear(&v);
                    store->Release();
                }
                // Several genres come joined with "; ": the first one names the group.
                const size_t semi = genre.find(L';');
                if (semi != std::wstring::npos) genre.resize(semi);
                while (!genre.empty() && iswspace(genre.back())) genre.pop_back();
                std::lock_guard<std::mutex> lock(mutex_);
                genres_[library::PathKey(path)] = genre;
            }
            if (SUCCEEDED(co)) CoUninitialize();
            busy_ = false;
            if (hwnd && !stop_) PostMessageW(hwnd, kLibraryMessage, 1, 0);
        });
    }

    bool Busy() const { return busy_; }
    std::wstring GenreOf(const std::wstring& path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = genres_.find(library::PathKey(path));
        return it == genres_.end() ? std::wstring() : it->second;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::wstring, std::wstring> genres_;
    std::thread thread_;
    std::atomic<bool> busy_{false}, stop_{false};
};

// --- helpers --------------------------------------------------------------------------------

// Tags only, empty = unknown. Albums aren't guessed from folder names: the Songs list, the
// album page and Artists then agree on what's untagged.
std::wstring AlbumOf(const library::MediaItem& m) {
    return m.album;
}

std::wstring ArtistOf(const library::MediaItem& m) {
    return m.artist;
}

std::wstring GenreLabel(const std::wstring& g) {
    return g.empty() ? std::wstring(Tr(L"Unknown genre")) : g;
}

bool SameText(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring Upper(const std::wstring& s) {
    std::wstring u = s;
    if (!u.empty()) CharUpperBuffW(u.data(), static_cast<DWORD>(u.size()));
    return u;
}

void PlayShuffled(std::vector<QueueItem> q, const std::wstring& name) {
    if (q.empty()) return;
    std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    const int start = static_cast<int>(rng() % q.size());
    PlayQueue(std::move(q), start, name);
    TheApp().queue().SetShuffle(true);
}

void PlayInOrder(std::vector<QueueItem> q, const std::wstring& name) {
    if (q.empty()) return;
    TheApp().queue().SetShuffle(false);
    PlayQueue(std::move(q), 0, name);
}

enum class MusicMode { Root, Songs, Albums, Artists, Genres, Album, Artist, Genre };

// Cover of the album / artist page header: the embedded art of a track, or a placeholder.
class CoverArt : public Widget {
public:
    CoverArt(float size, bool round) : size_(size), round_(round) {
        width = Length::Px(size);
        height = Length::Px(size);
        imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
    }
    void SetImage(const std::wstring& path) {
        if (path == image_) return;
        image_ = path;
        Invalidate();
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const float radius = round_ ? rect.w / 2 : 12.0f;
        ID2D1Bitmap* bmp = nullptr;
        if (!image_.empty()) {
            const float scale = GetHost() ? GetHost()->Scale() : 1.0f;
            bmp = TheApp().images().Get(image_, static_cast<int>(std::ceil(size_ * scale)));
        }
        if (bmp) {
            if (round_) {
                p.PushRoundClip(rect, radius);
                p.Image(bmp, rect, 0, true);
                p.PopClip();
            } else {
                p.Fill(rect, c.bgDeep, radius);
                p.Image(bmp, rect, radius, true);
            }
            return;
        }
        p.Fill(rect, c.bgCard, radius);
        p.Icon(round_ ? "artist" : "album", rect.Center(64, 64), c.textTertiary, c.accentMid);
    }

private:
    float size_;
    bool round_;
    std::wstring image_;
    ScopedConnection imagesConn_;
};

struct Group {
    std::wstring name;     // display / route key
    std::wstring artist;   // albums: common artist, or empty when mixed
    bool mixed = false;
    std::vector<const library::MediaItem*> songs;
};

class MusicPage : public Page {
public:
    MusicPage(Route r, bool compact) : Page(r, compact, "music") {
        const std::wstring& p = route_.path;
        auto starts = [&](const wchar_t* prefix, std::wstring* rest) {
            const size_t n = wcslen(prefix);
            if (p.compare(0, n, prefix) != 0) return false;
            *rest = p.substr(n);
            return true;
        };
        if (p.empty()) mode_ = MusicMode::Root;
        else if (p == L"songs") mode_ = MusicMode::Songs;
        else if (p == L"albums") mode_ = MusicMode::Albums;
        else if (p == L"artists") mode_ = MusicMode::Artists;
        else if (p == L"genres") mode_ = MusicMode::Genres;
        else if (starts(L"album:", &arg_)) mode_ = MusicMode::Album;
        else if (starts(L"artist:", &arg_)) mode_ = MusicMode::Artist;
        else if (starts(L"genre:", &arg_)) mode_ = MusicMode::Genre;
        key_ = std::string("music.") + ModeKey();
        const bool listDefault = mode_ == MusicMode::Songs || mode_ == MusicMode::Album || mode_ == MusicMode::Artist ||
                                 mode_ == MusicMode::Genre;
        list_ = ListModeFor(key_, listDefault);
        queueName_ = Title();
        if ((mode_ == MusicMode::Album || mode_ == MusicMode::Artist) && !compact) {
            // Header: cover, what it is, the name, artist / albums, year · genre · songs · length,
            // then the buttons.
            toolbar_->gap = 24;
            toolbar_->align = Align::End;
            cover_ = toolbar_->Add<CoverArt>(168.0f, mode_ == MusicMode::Artist);
            auto* info = toolbar_->Add<Widget>();
            info->dir = Dir::Column;
            info->gap = 6;
            info->width = Length::Grow();
            auto* kicker = info->Add<Label>(Tr(mode_ == MusicMode::Album ? L"Album" : L"Artist"), TextStyle::SmallMedium,
                                            Tone::Tertiary);
            kicker->width = Length::Grow();
            auto* heading = info->Add<Label>(Title(), TextStyle::Display, Tone::Primary);
            heading->width = Length::Grow();
            byline_ = info->Add<Label>(L"", TextStyle::Large, Tone::Secondary);
            byline_->width = Length::Grow();
            meta_ = info->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
            meta_->width = Length::Grow();
            auto* buttons = info->Add<Widget>();
            buttons->dir = Dir::Row;
            buttons->gap = 8;
            buttons->pad = {0, 10, 0, 0};
            auto* play = buttons->Add<Button>(Tr(L"Play All"), ButtonKind::Primary, "play");
            play->onClick = [this] { PlayInOrder(queue_, queueName_); };
            auto* shuffle = buttons->Add<Button>(Tr(L"Shuffle"), ButtonKind::Secondary, "shuffle_right");
            shuffle->onClick = [this] { PlayShuffled(queue_, queueName_); };
            toolbar_->visible = true;
        } else if (mode_ != MusicMode::Root && mode_ != MusicMode::Albums && mode_ != MusicMode::Artists &&
                   mode_ != MusicMode::Genres) {
            auto* play = toolbar_->Add<Button>(Tr(L"Play All"), ButtonKind::Primary, "play");
            play->onClick = [this] { PlayInOrder(queue_, queueName_); };
            auto* shuffle = toolbar_->Add<Button>(Tr(L"Shuffle"), ButtonKind::Secondary, "shuffle_right");
            shuffle->onClick = [this] { PlayShuffled(queue_, queueName_); };
            toolbar_->Add<Spacer>();
            summary_ = toolbar_->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
        summary_->visible = !compact;  // no room next to the buttons in the side panel
            toolbar_->visible = true;
        }
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        std::vector<Crumb> c = {{Tr(L"Music"), Route{Section::Music, {}}}};
        switch (mode_) {
            case MusicMode::Root: break;
            case MusicMode::Songs: c.push_back({Tr(L"Songs"), route_}); break;
            case MusicMode::Albums: c.push_back({Tr(L"Albums"), route_}); break;
            case MusicMode::Artists: c.push_back({Tr(L"Artists"), route_}); break;
            case MusicMode::Genres: c.push_back({Tr(L"Genres"), route_}); break;
            case MusicMode::Album:
                c.push_back({Tr(L"Albums"), Route{Section::Music, L"albums"}});
                c.push_back({AlbumLabel(arg_), route_});
                break;
            case MusicMode::Artist:
                c.push_back({Tr(L"Artists"), Route{Section::Music, L"artists"}});
                c.push_back({ArtistLabel(arg_), route_});
                break;
            case MusicMode::Genre:
                c.push_back({Tr(L"Genres"), Route{Section::Music, L"genres"}});
                c.push_back({GenreLabel(arg_), route_});
                break;
        }
        return c;
    }

    bool HasViewModes() const override { return !compact_ && mode_ != MusicMode::Root; }
    // Song lists say "8 songs · 6:15" beside their buttons; the header doesn't repeat the count.
    long long ItemCount() const override { return countShown_ ? -1 : count_; }

    std::vector<MenuItem> SortMenu() override {
        std::vector<MenuItem> m;
        switch (mode_) {
            case MusicMode::Root: return m;
            case MusicMode::Albums:
            case MusicMode::Artists:
            case MusicMode::Genres:
                SortMenuItems(m, {{L"Title A-Z", "name"}, {L"Title Z-A", "name:desc"}, {L"Most Songs First", "count"}});
                return m;
            default:
                SortMenuItems(m, {{L"Track Number", "track"},
                                  {L"Title A-Z", "title"},
                                  {L"Title Z-A", "title:desc"},
                                  {L"Artist A-Z", "artist"},
                                  {L"Artist Z-A", "artist:desc"},
                                  {L"Album A-Z", "album"},
                                  {L"Album Z-A", "album:desc"},
                                  {L"File Name", "name"},
                                  {L"Newest First", "date:desc"},
                                  {L"Oldest First", "date"},
                                  {L"Longest First", "duration:desc"},
                                  {L"Shortest First", "duration"}});
                return m;
        }
    }

    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        if (!queue_.empty() && mode_ != MusicMode::Root) {
            auto q = queue_;
            const std::wstring name = queueName_;
            m.push_back(Item(L"Play All", "playback", [q, name] { PlayInOrder(q, name); }));
            m.push_back(Item(L"Shuffle", "shuffle_right", [q, name] { PlayShuffled(q, name); }));
            m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(q); }));
            MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr);
            pl.submenu = AddToPlaylistMenu(q);
            m.push_back(std::move(pl));
            m.push_back(MenuItem::Separator());
        }
        m.push_back(Item(L"Add Folder to Library…", "new-folder", [] { AddFolderToLibrary(); }));
        m.push_back(Item(L"Rescan Library", "refresh", [] { RescanLibrary(); }, !TheApp().library().Scanning()));
        return m;
    }

protected:
    GridStyle CardStyle() const override { return GridStyle::SquareCards; }
    unsigned RowColumns() const override {
        if (mode_ == MusicMode::Albums || mode_ == MusicMode::Artists || mode_ == MusicMode::Genres || mode_ == MusicMode::Root)
            return kColSubtitle;
        // Album page: every row has the same album (and usually artist); artist page: same artist.
        if (mode_ == MusicMode::Album) return kColTrack | kColDuration | (mixedArtists_ ? kColSubtitle : 0u);
        if (mode_ == MusicMode::Artist) return kColTrack | kColExtra | kColDuration;
        return kColTrack | kColSubtitle | kColExtra | kColDuration;
    }

    void Build() override {
        if (!snap_) return;
        auto songs = snap_->Music();
        const bool needGenres = mode_ == MusicMode::Genres || mode_ == MusicMode::Genre || mode_ == MusicMode::Root ||
                                mode_ == MusicMode::Album;
        if (needGenres) GenreIndex::Get().Request(*snap_);
        grid_->roundImages = mode_ == MusicMode::Artists;

        switch (mode_) {
            case MusicMode::Root: BuildRoot(songs); break;
            case MusicMode::Songs: BuildSongs(songs, "track"); break;
            case MusicMode::Albums: BuildGroups(Groups(songs, MusicMode::Albums), EntryKind::Album, "album", L"album:"); break;
            case MusicMode::Artists:
                BuildGroups(Groups(songs, MusicMode::Artists), EntryKind::Artist, "artist", L"artist:");
                break;
            case MusicMode::Genres: BuildGroups(Groups(songs, MusicMode::Genres), EntryKind::Genre, "genre", L"genre:"); break;
            case MusicMode::Album:
            case MusicMode::Artist:
            case MusicMode::Genre: {
                std::vector<const library::MediaItem*> v;
                for (const library::MediaItem* m : songs) {
                    const bool in = mode_ == MusicMode::Album    ? SameText(AlbumOf(*m), arg_)
                                    : mode_ == MusicMode::Artist ? SameText(ArtistOf(*m), arg_)
                                                                 : SameText(GenreIndex::Get().GenreOf(m->path), arg_);
                    if (in) v.push_back(m);
                }
                mixedArtists_ = false;
                for (const library::MediaItem* m : v)
                    if (!SameText(m->artist, v.front()->artist)) mixedArtists_ = true;
                if (mode_ == MusicMode::Artist) {
                    // The artist's albums first, then all of their songs.
                    auto albums = Groups(v, MusicMode::Albums);
                    const bool tagged = std::any_of(albums.begin(), albums.end(), [](const Group& g) { return !g.name.empty(); });
                    if (tagged && folded_.empty()) {
                        AddHeader(Tr(L"Albums"));
                        BuildGroups(std::move(albums), EntryKind::Album, "album", L"album:", true);
                        AddHeader(Tr(L"Songs"));
                    }
                }
                BuildSongs(v, "track");
                UpdateHeader(v);
                break;
            }
        }
        countShown_ = false;
        if (summary_) {
            double total = 0;
            for (const QueueItem& q : queue_) total += q.duration;
            std::wstring s = CountSongs(static_cast<long long>(queue_.size()));
            if (total > 0) s += L" · " + FormatDuration(total);
            summary_->SetText(queue_.empty() ? std::wstring() : s);
            toolbar_->visible = !queue_.empty();
            countShown_ = !queue_.empty() && summary_->visible;
        }
        if (cover_) countShown_ = !queue_.empty() || !folded_.empty();
    }

    // Album / artist header: cover, byline and the facts line.
    void UpdateHeader(const std::vector<const library::MediaItem*>& all) {
        if (!cover_) return;
        std::wstring image;
        int year = 0;
        std::wstring genre;
        for (const library::MediaItem* m : all) {
            if (image.empty() && !m->offline) image = m->path;
            year = std::max(year, m->year);
            if (genre.empty()) genre = GenreIndex::Get().GenreOf(m->path);
        }
        cover_->SetImage(image);
        double total = 0;
        for (const library::MediaItem* m : all) total += m->duration;
        std::wstring facts;
        auto add = [&](const std::wstring& part) {
            if (part.empty()) return;
            if (!facts.empty()) facts += L" · ";
            facts += part;
        };
        if (mode_ == MusicMode::Album) {
            byline_->SetText(mixedArtists_ ? std::wstring(Tr(L"Various artists"))
                                           : ArtistLabel(all.empty() ? std::wstring() : all.front()->artist));
            if (year > 0) add(std::to_wstring(year));
            add(genre);
        } else {
            std::vector<std::wstring> albums;
            for (const library::MediaItem* m : all)
                if (!m->album.empty() &&
                    std::none_of(albums.begin(), albums.end(), [&](const std::wstring& a) { return SameText(a, m->album); }))
                    albums.push_back(m->album);
            byline_->SetText(albums.empty() ? std::wstring() : CountAlbums(static_cast<long long>(albums.size())));
        }
        add(CountSongs(static_cast<long long>(all.size())));
        if (total > 0) add(FormatDuration(total));
        meta_->SetText(facts);
        byline_->visible = !byline_->Text().empty();
    }

    void FillEmpty(EmptyState& e) override {
        const bool scanning = TheApp().library().Scanning();
        if (scanning) {
            e.Set("refresh", Tr(L"Scanning your folders…"), Tr(L"Songs appear here as soon as they are found."));
            return;
        }
        if (mode_ == MusicMode::Genres && GenreIndex::Get().Busy()) {
            e.Set("genre", Tr(L"Reading genres…"), Tr(L"Lumen is reading the genre tags of your songs."));
            return;
        }
        if (TheApp().library().Roots().empty()) {
            e.Set("songs", Tr(L"Add your music folders"),
                  Tr(L"Lumen shows the music from the folders you add. Nothing is moved or copied."),
                  {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }}});
            return;
        }
        e.Set("songs", Tr(L"No music found"), Tr(L"The library folders don't contain supported audio files yet."),
              {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }}});
    }

    void ShowMenu(int index, Point at) override {
        const Entry* e = EntryAt(index);
        if (!e || !GetHost()) return;
        if (e->kind == EntryKind::Album || e->kind == EntryKind::Artist || e->kind == EntryKind::Genre) {
            const Route* r = RouteAt(index);
            const Route target = r ? *r : Route{};
            auto q = GroupQueue(index);
            const std::wstring name = e->kind == EntryKind::Artist ? ArtistLabel(e->path) : e->title;
            std::vector<MenuItem> m;
            m.push_back(Item(L"Open", e->kind == EntryKind::Album ? "album" : e->kind == EntryKind::Artist ? "artist" : "genre",
                             [target] { TheApp().Navigate(target); }));
            m.push_back(Item(L"Play", "playback", [q, name] { PlayInOrder(q, name); }));
            m.push_back(Item(L"Shuffle", "shuffle_right", [q, name] { PlayShuffled(q, name); }));
            m.push_back(Item(L"Play Next", "next_twotone", [q] { PlayNext(q); }));
            m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(q); }));
            MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr);
            pl.submenu = AddToPlaylistMenu(q);
            m.push_back(std::move(pl));
            ShowContextMenu(GetHost(), at, std::move(m));
            return;
        }
        Page::ShowMenu(index, at);
    }

private:
    const char* ModeKey() const {
        switch (mode_) {
            case MusicMode::Root: return "root";
            case MusicMode::Songs: return "songs";
            case MusicMode::Albums: return "albums";
            case MusicMode::Artists: return "artists";
            case MusicMode::Genres: return "genres";
            case MusicMode::Album: return "album";
            case MusicMode::Artist: return "artist";
            case MusicMode::Genre: return "genre";
        }
        return "root";
    }

    std::wstring Title() const {
        switch (mode_) {
            case MusicMode::Album: return AlbumLabel(arg_);
            case MusicMode::Artist: return ArtistLabel(arg_);
            case MusicMode::Genre: return GenreLabel(arg_);
            case MusicMode::Songs: return Tr(L"Songs");
            default: return Tr(L"Music");
        }
    }

    std::vector<Group> Groups(const std::vector<const library::MediaItem*>& songs, MusicMode kind) const {
        std::map<std::wstring, size_t> index;  // upper-cased name -> group
        std::vector<Group> groups;
        for (const library::MediaItem* m : songs) {
            std::wstring name = kind == MusicMode::Albums    ? AlbumOf(*m)
                                : kind == MusicMode::Artists ? ArtistOf(*m)
                                                             : GenreIndex::Get().GenreOf(m->path);
            const std::wstring key = Upper(name);
            auto it = index.find(key);
            if (it == index.end()) {
                it = index.emplace(key, groups.size()).first;
                Group g;
                g.name = name;
                g.artist = m->artist;
                groups.push_back(std::move(g));
            }
            Group& g = groups[it->second];
            if (!g.mixed && !SameText(g.artist, m->artist)) g.mixed = true;
            g.songs.push_back(m);
        }
        return groups;
    }

    std::vector<QueueItem> GroupQueue(int index) const {
        std::vector<QueueItem> q;
        auto it = groupSongs_.find(index);
        if (it == groupSongs_.end()) return q;
        for (const library::MediaItem* m : it->second) q.push_back(MakeQueueItem(m->path, m->DisplayTitle(), m->duration));
        return q;
    }

    void BuildRoot(const std::vector<const library::MediaItem*>& songs) {
        if (songs.empty()) return;  // the empty state offers "Add folder"
        // Same items in grid and list: the four categories and an "Add folder" tile/row, which
        // leads the side panel's list (its design) and closes the page's grid.
        auto addFolder = [&] {
            Entry add;
            add.kind = EntryKind::Action;
            add.title = Tr(L"Add folder");
            add.icon = "add";
            AddAction(std::move(add), [] { AddFolderToLibrary(); });
        };
        if (compact_) addFolder();
        auto cat = [&](const wchar_t* title, const char* icon, const wchar_t* path, std::wstring sub, int count) {
            Entry e;
            e.kind = EntryKind::Category;
            e.title = Tr(title);
            e.subtitle = std::move(sub);
            e.icon = icon;
            e.count = list_ || compact_ ? count : -1;
            AddRoute(std::move(e), Route{Section::Music, path});
        };
        const auto albums = Groups(songs, MusicMode::Albums);
        const auto artists = Groups(songs, MusicMode::Artists);
        const auto genres = Groups(songs, MusicMode::Genres);
        const long long n = static_cast<long long>(songs.size());
        cat(L"Songs", "songs", L"songs", CountSongs(n), static_cast<int>(n));
        cat(L"Albums", "album", L"albums", CountAlbums(static_cast<long long>(albums.size())), static_cast<int>(albums.size()));
        cat(L"Artists", "artist", L"artists", CountArtists(static_cast<long long>(artists.size())),
            static_cast<int>(artists.size()));
        const bool reading = GenreIndex::Get().Busy();
        cat(L"Genres", "genre", L"genres", reading ? std::wstring(Tr(L"Reading tags…")) : CountGenres(static_cast<long long>(genres.size())),
            static_cast<int>(genres.size()));
        if (!compact_) addFolder();
    }

    void BuildSongs(std::vector<const library::MediaItem*> v, const std::string& defaultSort) {
        std::vector<const library::MediaItem*> filtered;
        for (const library::MediaItem* m : v)
            if (folded_.empty() || library::FoldedContains(m->searchKey, folded_)) filtered.push_back(m);
        const std::string sort = SortValue(defaultSort);
        SortMedia(filtered, sort);
        if (mode_ == MusicMode::Album)
            AddColumns(sort, {{kHeadTrack, L"#", "track"},
                              {kHeadTitle, L"Title", "title"},
                              {kHeadSubtitle, L"Artist", "artist"},
                              {kHeadDuration, L"Length", "duration"}});
        else if (mode_ == MusicMode::Artist)
            AddColumns(sort, {{kHeadTrack, L"#", "track"},
                              {kHeadTitle, L"Title", "title"},
                              {kHeadExtra, L"Album", "album"},
                              {kHeadDuration, L"Length", "duration"}});
        else
            AddColumns(sort, {{kHeadTrack, L"#", "track"},
                              {kHeadTitle, L"Title", "title"},
                              {kHeadSubtitle, L"Artist", "artist"},
                              {kHeadExtra, L"Album", "album"},
                              {kHeadDuration, L"Length", "duration"}});
        for (const library::MediaItem* m : filtered) AddMedia(*m);
    }

    // byArtist: the albums block of an artist page (no artist name on every album, years instead).
    void BuildGroups(std::vector<Group> groups, EntryKind kind, const char* icon, const wchar_t* prefix,
                     bool byArtist = false) {
        const std::string sort = byArtist ? std::string("name") : SortValue("name");
        std::stable_sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
            if (sort == "count" && a.songs.size() != b.songs.size()) return a.songs.size() > b.songs.size();
            if (a.name.empty() != b.name.empty()) return b.name.empty();  // "Unknown" last
            const int c = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
            return sort == "name:desc" ? c > 0 : c < 0;
        });
        groupSongs_.clear();
        for (Group& g : groups) {
            const std::wstring label = kind == EntryKind::Artist ? ArtistLabel(g.name)
                                       : kind == EntryKind::Genre ? GenreLabel(g.name)
                                                                  : AlbumLabel(g.name);
            if (!Matches(label + L" " + g.artist)) continue;
            Entry e;
            e.kind = kind;
            e.title = label;
            e.path = g.name;  // raw key (empty = unknown)
            e.icon = icon;
            // Albums always name the artist (like the songs list), artists and genres count songs.
            int year = 0;
            for (const library::MediaItem* m : g.songs) year = std::max(year, m->year);
            if (byArtist)
                e.subtitle = year > 0 ? std::to_wstring(year) + L" · " + CountSongs(static_cast<long long>(g.songs.size()))
                                      : CountSongs(static_cast<long long>(g.songs.size()));
            else if (kind == EntryKind::Album)
                e.subtitle = g.mixed ? std::wstring(Tr(L"Various artists")) : ArtistLabel(g.artist);
            else
                e.subtitle = CountSongs(static_cast<long long>(g.songs.size()));
            if (list_ && kind == EntryKind::Album) e.count = static_cast<int>(g.songs.size());
            library::SortItems(g.songs, library::SortKey::Name);
            for (const library::MediaItem* m : g.songs)
                if (!m->offline) {
                    e.image = m->path;  // cover art of the first track
                    break;
                }
            const int i = AddRoute(std::move(e), Route{Section::Music, prefix + g.name});
            groupSongs_[i] = std::move(g.songs);
        }
    }

    MusicMode mode_ = MusicMode::Root;
    std::wstring arg_;
    Label* summary_ = nullptr;
    CoverArt* cover_ = nullptr;  // album / artist header (not in the side panel)
    Label* byline_ = nullptr;
    Label* meta_ = nullptr;
    bool mixedArtists_ = false;
    bool countShown_ = false;
    std::map<int, std::vector<const library::MediaItem*>> groupSongs_;
};

bool LooksLikeFolder(const std::wstring& p) {
    return p.size() >= 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'));
}

}  // namespace

std::unique_ptr<LibraryView> MakeMusicPage(const Route& route, bool compact) {
    if (LooksLikeFolder(route.path)) return MakeFolderPage(route, compact);
    return std::make_unique<MusicPage>(route, compact);
}

}  // namespace app::lib
