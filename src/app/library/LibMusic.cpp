// Music: Songs, Albums, Artists, Genres and their detail pages. Uses only the tags in the library
// snapshot (the scanner reads and caches them): no file is opened on the UI path.
#include "Util.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/Pages.h"
#include "library/MediaTypes.h"
#include "ui/Host.h"

#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace app::lib {

using namespace ui;

namespace {

// --- helpers --------------------------------------------------------------------------------

// Albums, artists and genres are the tags only, empty = unknown. Albums aren't guessed from
// folder names: the Songs list, the album page and Artists then agree on what's untagged.
// Who an album is by: its album artist tag ("Various Artists" on a compilation), else the
// track's artist.
const std::wstring& AlbumArtistOf(const library::MediaItem& m) {
    return m.albumArtist.empty() ? m.artist : m.albumArtist;
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

enum class MusicMode { Songs, Albums, Artists, Genres, Album, Artist, Genre };

// The tabs of the Music section, in the order of Apple Music and Spotify: albums first.
struct MusicTab {
    const wchar_t* label;
    const wchar_t* path;
};
constexpr MusicTab kMusicTabs[] = {{L"Albums", L"albums"}, {L"Artists", L"artists"}, {L"Songs", L"songs"}, {L"Genres", L"genres"}};

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
    std::wstring name;     // display name
    std::wstring key;      // route argument: the name, or AlbumRouteArg for albums
    std::wstring artist;   // albums: common artist, or empty when mixed
    bool mixed = false;
    std::vector<const library::MediaItem*> songs;
};

class MusicPage : public Page {
public:
    MusicPage(Route r, bool compact) : Page(r, compact, "music") {
        // "Music" itself opens the tab used last (UX-7): no hub of four tiles before the music.
        const std::wstring p = route_.path.empty() ? LastMusicTab() : route_.path;
        auto starts = [&](const wchar_t* prefix, std::wstring* rest) {
            const size_t n = wcslen(prefix);
            if (p.compare(0, n, prefix) != 0) return false;
            *rest = p.substr(n);
            return true;
        };
        if (p == L"songs") mode_ = MusicMode::Songs;
        else if (p == L"albums") mode_ = MusicMode::Albums;
        else if (p == L"artists") mode_ = MusicMode::Artists;
        else if (p == L"genres") mode_ = MusicMode::Genres;
        else if (starts(L"album:", &arg_)) {
            mode_ = MusicMode::Album;
            const size_t nl = arg_.find(L'\n');  // "<name>\n<folder key>", see AlbumRouteArg
            if (nl != std::wstring::npos) {
                albumFolder_ = arg_.substr(nl + 1);
                arg_.resize(nl);
            }
        }
        else if (starts(L"artist:", &arg_)) mode_ = MusicMode::Artist;
        else if (starts(L"genre:", &arg_)) mode_ = MusicMode::Genre;
        key_ = std::string("music.") + ModeKey();
        const bool listDefault = mode_ == MusicMode::Songs || mode_ == MusicMode::Album || mode_ == MusicMode::Artist ||
                                 mode_ == MusicMode::Genre;
        list_ = ListModeFor(key_, listDefault);
        queueName_ = Title();
        if (const wchar_t* tab = TabPath()) {
            TheApp().SetUiValue("music.tab", WideToUtf8(tab));
            if (!compact) AddTabs(tab);
        }
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
            if (mode_ == MusicMode::Artist) AddAlbumsBlock();
        } else if (mode_ != MusicMode::Albums && mode_ != MusicMode::Artists && mode_ != MusicMode::Genres) {
            auto* play = toolbar_->Add<Button>(Tr(L"Play All"), ButtonKind::Primary, "play");
            play->onClick = [this] { PlayInOrder(queue_, queueName_); };
            auto* shuffle = toolbar_->Add<Button>(Tr(L"Shuffle"), ButtonKind::Secondary, "shuffle_right");
            shuffle->onClick = [this] { PlayShuffled(queue_, queueName_); };
            // "1355 songs · 9:31:45" takes the rest of the row, right-aligned: in a narrow window it
            // ends with "…" (full text in the tooltip) instead of running past the window edge.
            summary_ = toolbar_->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
            summary_->width = Length::Grow();
            summary_->halign = HAlign::Trailing;
            summary_->interactive = true;  // hover shows the tooltip when cut
            summary_->visible = !compact;  // no room next to the buttons in the side panel
            toolbar_->visible = true;
        }
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        std::vector<Crumb> c = {{Tr(L"Music"), Route{Section::Music, LastMusicTab()}}};
        switch (mode_) {
            case MusicMode::Songs:
            case MusicMode::Albums:
            case MusicMode::Artists:
            case MusicMode::Genres: c[0].route = route_; break;  // the tabs say which list it is
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

    bool HasViewModes() const override { return !compact_; }
    void OnLayout() override {
        Page::OnLayout();
        if (summary_ && summary_->visible) {
            // Too narrow for "1355 songs · 9:31:45": the count alone, then the count with "…";
            // the tooltip always has the whole line.
            auto& t = gfx::Text::Get();
            const float w = summary_->rect.w + 0.5f;
            const std::wstring& shown =
                t.Measure(summaryFull_, summary_->style).width <= w || summaryShort_.empty() ? summaryFull_ : summaryShort_;
            summary_->SetText(shown);
            summary_->tooltip = shown != summaryFull_ || t.Measure(shown, summary_->style).width > w ? summaryFull_ : std::wstring();
        }
    }
    // Song lists say "8 songs · 6:15" beside their buttons; the header doesn't repeat the count.
    long long ItemCount() const override { return countShown_ ? -1 : count_; }

    std::vector<MenuItem> SortMenu() override {
        std::vector<MenuItem> m;
        switch (mode_) {
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
        if (compact_ && TabPath()) {
            // The side panel has no room for the tabs: they are the first items of its "⋯" menu.
            for (const MusicTab& t : kMusicTabs) {
                MenuItem it;
                it.label = Tr(t.label);
                it.radio = true;
                it.checked = wcscmp(TabPath(), t.path) == 0;
                const std::wstring p = t.path;
                it.action = [p] { TheApp().Navigate(Route{Section::Music, p}); };
                m.push_back(std::move(it));
            }
            m.push_back(MenuItem::Separator());
        }
        if (!queue_.empty()) {
            const SharedQueue q = ShareQueue(queue_);
            const std::wstring name = queueName_;
            m.push_back(Item(L"Play All", "playback", [q, name] { PlayInOrder(*q, name); }));
            m.push_back(Item(L"Shuffle", "shuffle_right", [q, name] { PlayShuffled(*q, name); }));
            m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(*q); }));
            MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr);
            pl.submenu = AddToPlaylistMenu(q);
            m.push_back(std::move(pl));
            m.push_back(MenuItem::Separator());
        }
        m.push_back(Item(L"Rescan Library", "refresh", [] { RescanLibrary(); }, !TheApp().library().Scanning()));
        return m;
    }

protected:
    GridStyle CardStyle() const override { return GridStyle::SquareCards; }
    unsigned RowColumns() const override {
        if (mode_ == MusicMode::Albums || mode_ == MusicMode::Artists || mode_ == MusicMode::Genres)
            return kColSubtitle;
        // Album page: every row has the same album (and usually artist); artist page: same artist.
        // The track number only means something on the album page: in a list of many albums it
        // runs 1–4, then 1–2 again and reads like an error (UX-14).
        if (mode_ == MusicMode::Album) return kColTrack | kColDuration | (mixedArtists_ ? kColSubtitle : 0u);
        if (mode_ == MusicMode::Artist) return kColExtra | kColDuration;
        return kColSubtitle | kColExtra | kColDuration;
    }

    void Build() override {
        if (!snap_) return;
        auto songs = snap_->Music();
        grid_->roundImages = mode_ == MusicMode::Artists;
        grid_->thumbs = mode_ != MusicMode::Album;  // every row would repeat the album's cover
        if (tabs_) tabs_->Parent()->visible = !songs.empty();  // no music at all: the empty state alone

        switch (mode_) {
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
                    const bool in = mode_ == MusicMode::Album
                                        ? SameText(m->album, arg_) && (albumFolder_.empty() || AlbumFolderKey(*m) == albumFolder_)
                                    : mode_ == MusicMode::Artist ? SameText(m->artist, arg_)
                                                                 : SameText(m->genre, arg_);
                    if (in) v.push_back(m);
                }
                mixedArtists_ = false;
                for (const library::MediaItem* m : v)
                    if (mode_ == MusicMode::Album ? !SameText(AlbumArtistOf(*m), AlbumArtistOf(*v.front()))
                                                  : !SameText(m->artist, v.front()->artist))
                        mixedArtists_ = true;
                if (mode_ == MusicMode::Artist) {
                    // The artist's albums first, then all of their songs.
                    auto albums = Groups(v, MusicMode::Albums);
                    const bool tagged = std::any_of(albums.begin(), albums.end(), [](const Group& g) { return !g.name.empty(); });
                    const bool show = tagged && folded_.empty();
                    if (albums_) {
                        FillAlbums(show ? std::move(albums) : std::vector<Group>{});
                    } else if (show) {
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
            summaryShort_ = CountSongs(static_cast<long long>(queue_.size()));
            summaryFull_ = summaryShort_;
            if (total > 0) summaryFull_ += L" · " + FormatDuration(total);
            if (queue_.empty()) {
                summaryFull_.clear();
                summaryShort_.clear();
            }
            summary_->SetText(summaryFull_);
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
            if (genre.empty()) genre = m->genre;
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
                                           : ArtistLabel(all.empty() ? std::wstring() : AlbumArtistOf(*all.front())));
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
        if (TheApp().library().Roots().empty()) {
            e.Set("songs", Tr(L"Add your music folders"),
                  Tr(L"Lumen shows the music from the folders you add. Nothing is moved or copied."),
                  {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }}});
            return;
        }
        // G-09: the same two buttons as the empty Videos page.
        e.Set("songs", Tr(L"No music found"), Tr(L"The library folders don't contain supported audio files yet."),
              {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }},
               {Tr(L"Rescan"), ButtonKind::Secondary, "refresh", [] { RescanLibrary(); }}});
    }

    void ShowMenu(int index, Point at) override {
        const Entry* e = EntryAt(index);
        if (!e || !GetHost()) return;
        if (e->kind == EntryKind::Album || e->kind == EntryKind::Artist || e->kind == EntryKind::Genre) {
            const Route* r = RouteAt(index);
            GroupMenu(e->kind, r ? *r : Route{}, GroupQueue(index), e->kind == EntryKind::Artist ? ArtistLabel(e->path) : e->title, at);
            return;
        }
        Page::ShowMenu(index, at);
    }

private:
    void GroupMenu(EntryKind kind, Route target, std::vector<QueueItem> items, const std::wstring& name, Point at) {
        if (!GetHost()) return;
        const SharedQueue q = ShareQueue(std::move(items));
        std::vector<MenuItem> m;
        m.push_back(Item(L"Open", kind == EntryKind::Album ? "album" : kind == EntryKind::Artist ? "artist" : "genre",
                         [target] { TheApp().Navigate(target); }));
        m.push_back(Item(L"Play", "playback", [q, name] { PlayInOrder(*q, name); }));
        m.push_back(Item(L"Shuffle", "shuffle_right", [q, name] { PlayShuffled(*q, name); }));
        m.push_back(Item(L"Play Next", "next_twotone", [q] { PlayNext(*q); }));
        m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(*q); }));
        MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr);
        pl.submenu = AddToPlaylistMenu(q);
        m.push_back(std::move(pl));
        ShowContextMenu(GetHost(), at, std::move(m));
    }

    // Artist page: "Albums" as a row of cover cards (art + year) above the "Songs" list, as in
    // Apple Music and Spotify. A grid of its own, so the songs below can stay a list.
    void AddAlbumsBlock() {
        auto title = [&](const wchar_t* text, int index, float top) {
            auto* box = content_->AddChild(std::make_unique<Widget>(), index);
            box->dir = Dir::Column;
            box->width = Length::Grow();
            box->pad = {0, top, 0, 16};
            box->visible = false;
            box->Add<Label>(Tr(text), TextStyle::Section, Tone::Secondary)->width = Length::Grow();
            return box;
        };
        albumsTitle_ = title(L"Albums", 1, 0);
        albums_ = static_cast<ItemGrid*>(content_->AddChild(std::make_unique<ItemGrid>(), 2));
        albums_->SetStyle(GridStyle::SquareCards);
        albums_->stretch = false;
        albums_->visible = false;
        albums_->onActivate = [this](int i) {
            if (i >= 0 && i < static_cast<int>(albumRoutes_.size())) TheApp().Navigate(albumRoutes_[static_cast<size_t>(i)]);
        };
        albums_->onMenu = [this](int i, Point at) {
            if (i < 0 || i >= static_cast<int>(albumRoutes_.size())) return;
            std::vector<QueueItem> q;
            for (const library::MediaItem* m : albumSongs_[static_cast<size_t>(i)])
                q.push_back(MakeQueueItem(m->path, m->DisplayTitle(), m->duration));
            GroupMenu(EntryKind::Album, albumRoutes_[static_cast<size_t>(i)], std::move(q),
                      albums_->Entries()[static_cast<size_t>(i)].title, at);
        };
        songsTitle_ = title(L"Songs", 3, 40);
    }

    // Newest first (unknown years last), like an artist's discography.
    void FillAlbums(std::vector<Group> groups) {
        auto yearOf = [](const Group& g) {
            int y = 0;
            for (const library::MediaItem* m : g.songs) y = std::max(y, m->year);
            return y;
        };
        std::stable_sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
            if (a.name.empty() != b.name.empty()) return b.name.empty();
            const int ya = yearOf(a), yb = yearOf(b);
            if (ya != yb) return ya > yb;
            return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
        });
        std::vector<Entry> entries;
        albumRoutes_.clear();
        albumSongs_.clear();
        for (Group& g : groups) {
            Entry e;
            e.kind = EntryKind::Album;
            e.title = AlbumLabel(g.name);
            e.path = g.name;
            e.icon = "album";
            const int year = yearOf(g);
            e.subtitle = year > 0 ? std::to_wstring(year) : CountSongs(static_cast<long long>(g.songs.size()));
            library::SortItems(g.songs, library::SortKey::Track);
            for (const library::MediaItem* m : g.songs)
                if (!m->offline) {
                    e.image = m->path;
                    break;
                }
            entries.push_back(std::move(e));
            albumRoutes_.push_back(Route{Section::Music, L"album:" + g.key});
            albumSongs_.push_back(std::move(g.songs));
        }
        const bool any = !entries.empty();
        albums_->SetEntries(std::move(entries));
        albums_->visible = albumsTitle_->visible = songsTitle_->visible = any;
    }

    // The tab this page is (Albums, Artists, Songs, Genres), null on an album, artist or genre page.
    const wchar_t* TabPath() const {
        switch (mode_) {
            case MusicMode::Albums: return L"albums";
            case MusicMode::Artists: return L"artists";
            case MusicMode::Songs: return L"songs";
            case MusicMode::Genres: return L"genres";
            default: return nullptr;
        }
    }

    // The tabs as segments above the content, as on the History page.
    void AddTabs(const wchar_t* current) {
        auto* row = content_->AddChild(std::make_unique<Widget>(), 0);
        row->dir = Dir::Row;
        row->width = Length::Grow();
        row->pad = {0, 0, 0, 24};
        int selected = 0;
        std::vector<Choice> choices;
        float widest = 96;  // segments share one width: the longest label sets it
        for (const MusicTab& t : kMusicTabs) {
            if (wcscmp(t.path, current) == 0) selected = static_cast<int>(choices.size());
            choices.push_back({Tr(t.label), {}, {}});
            widest = std::max(widest, std::ceil(gfx::Text::Get().Measure(choices.back().label, TextStyle::Body).width) + 24);
        }
        tabs_ = row->Add<Segmented>(std::move(choices), selected);
        tabs_->segmentWidth = widest;
        tabs_->onChange = [this](int i) {
            const std::wstring path = kMusicTabs[i].path;
            // Posted: navigating replaces this page while its control is still handling the click.
            if (Host* h = GetHost()) h->Post([path] { TheApp().Navigate(Route{Section::Music, path}); });
        };
    }

    const char* ModeKey() const {
        switch (mode_) {
            case MusicMode::Songs: return "songs";
            case MusicMode::Albums: return "albums";
            case MusicMode::Artists: return "artists";
            case MusicMode::Genres: return "genres";
            case MusicMode::Album: return "album";
            case MusicMode::Artist: return "artist";
            case MusicMode::Genre: return "genre";
        }
        return "albums";
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
            const std::wstring& name = kind == MusicMode::Albums    ? m->album
                                       : kind == MusicMode::Artists ? m->artist
                                                                    : m->genre;
            // Albums: same name and folder (AlbumFolderKey), so two artists' "Greatest Hits" stay apart.
            const bool album = kind == MusicMode::Albums && !name.empty();
            const std::wstring key = album ? Upper(name) + L"\n" + AlbumFolderKey(*m) : Upper(name);
            auto it = index.find(key);
            if (it == index.end()) {
                it = index.emplace(key, groups.size()).first;
                Group g;
                g.name = name;
                g.key = album ? AlbumRouteArg(*m) : name;
                g.artist = album ? AlbumArtistOf(*m) : m->artist;
                groups.push_back(std::move(g));
            }
            Group& g = groups[it->second];
            if (!g.mixed && !SameText(g.artist, album ? AlbumArtistOf(*m) : m->artist)) g.mixed = true;
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
            AddColumns(sort, {{kHeadTitle, L"Title", "title"},
                              {kHeadExtra, L"Album", "album"},
                              {kHeadDuration, L"Length", "duration"}});
        else
            AddColumns(sort, {{kHeadTitle, L"Title", "title"},
                              {kHeadSubtitle, L"Artist", "artist"},
                              {kHeadExtra, L"Album", "album"},
                              {kHeadDuration, L"Length", "duration"}});
        // A multi-disc album in album order gets "Disc 1", "Disc 2" titles, like a box set's sleeve.
        bool discs = false;
        if (mode_ == MusicMode::Album && (sort == "track" || sort == "track:desc"))
            for (const library::MediaItem* m : filtered)
                if (m->disc > 0 && m->disc != filtered.front()->disc) discs = true;
        int lastDisc = -1;
        for (const library::MediaItem* m : filtered) {
            if (discs && m->disc != lastDisc && m->disc > 0)
                AddHeader(Fmt(Tr(L"Disc {0}"), static_cast<long long>(m->disc)));
            lastDisc = m->disc;
            const int i = AddMedia(*m);
            // Filtered by text that only the file name holds: the file name says why the row is here.
            if (!folded_.empty() && !library::FoldedContains(library::SearchFold(m->title + L"\n" + m->artist + L"\n" + m->album), folded_))
                SetSubtitle(i, library::FileNameOf(m->path));
        }
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
            // List rows put the song count at the row's right edge, like folder counts, and give
            // the name the whole width (artists and genres too, not only albums).
            if (list_ && (kind == EntryKind::Album || !byArtist)) e.count = static_cast<int>(g.songs.size());
            library::SortItems(g.songs, library::SortKey::Name);
            for (const library::MediaItem* m : g.songs)
                if (!m->offline) {
                    e.image = m->path;  // cover art of the first track
                    break;
                }
            const int i = AddRoute(std::move(e), Route{Section::Music, prefix + g.key});
            groupSongs_[i] = std::move(g.songs);
        }
    }

    MusicMode mode_ = MusicMode::Albums;
    Segmented* tabs_ = nullptr;
    std::wstring arg_;
    std::wstring albumFolder_;  // album page: AlbumFolderKey of the album, empty = any folder
    Label* summary_ = nullptr;
    std::wstring summaryFull_, summaryShort_;  // "8 songs · 6:15" and "8 songs"
    CoverArt* cover_ = nullptr;  // album / artist header (not in the side panel)
    Label* byline_ = nullptr;
    Label* meta_ = nullptr;
    bool mixedArtists_ = false;
    bool countShown_ = false;
    std::map<int, std::vector<const library::MediaItem*>> groupSongs_;
    // Artist page albums block (not in the side panel).
    Widget* albumsTitle_ = nullptr;
    ItemGrid* albums_ = nullptr;
    Widget* songsTitle_ = nullptr;
    std::vector<Route> albumRoutes_;
    std::vector<std::vector<const library::MediaItem*>> albumSongs_;
};

}  // namespace

std::wstring LastMusicTab() {
    const std::wstring t = Utf8ToWide(TheApp().UiValue("music.tab", "albums"));
    for (const MusicTab& m : kMusicTabs)
        if (t == m.path) return t;
    return L"albums";
}

std::unique_ptr<LibraryView> MakeMusicPage(const Route& route, bool compact) {
    if (LooksLikeFolder(route.path)) return MakeFolderPage(route, compact);
    return std::make_unique<MusicPage>(route, compact);
}

}  // namespace app::lib
