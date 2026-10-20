// Home: Continue Watching, Listen Again, Frequently Visited Folders / Playlists, Frequently
#include "ui/Host.h"
// Listened Artists and Recently Added, each one row of cards with a title that opens the full
// page (design/ref/Player_6261-15078.png). Searching shows matching videos and music instead.
#include "Util.h"
#include "app/I18n.h"
#include "app/library/Pages.h"
#include "app/player/Episodes.h"
#include "library/MediaTypes.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <map>
#include <set>

namespace app::lib {

using namespace ui;

namespace {

// One titled row of cards (or up to three rows in the side panel).
class HomeSection : public Widget {
public:
    HomeSection(std::wstring title, Route more, bool compact, GridStyle style) : more_(std::move(more)), compact_(compact) {
        dir = Dir::Column;
        width = Length::Grow();
        gap = compact ? 4.0f : 20.0f;
        title_ = Add<SectionTitle>(std::move(title), compact);
        title_->onClick = [this] { TheApp().Navigate(more_); };
        grid_ = Add<ItemGrid>();
        grid_->SetStyle(compact ? GridStyle::CompactRows : style);
        grid_->maxRows = compact ? 3 : 1;
        grid_->stretch = false;
        grid_->columns = kColDuration;
        grid_->onActivate = [this](int i) { Activate(i); };
        grid_->onMenu = [this](int i, Point at) { Menu(i, at); };
    }

    void Clear() {
        entries_.clear();
        routes_.clear();
        actions_.clear();
        queue_.clear();
    }
    void AddEntry(Entry e, Route r = {}, std::function<void()> action = {}) {
        entries_.push_back(std::move(e));
        routes_.push_back(std::move(r));
        actions_.push_back(std::move(action));
    }
    int AddQueue(QueueItem q) {
        queue_.push_back(std::move(q));
        return static_cast<int>(queue_.size()) - 1;
    }
    void Commit(const std::wstring& queueName) {
        queueName_ = queueName;
        visible = !entries_.empty();
        grid_->SetEntries(std::move(entries_));
        entries_.clear();
    }
    void SetGridStyle(GridStyle s, int rows, unsigned columns = kColDuration) {
        grid_->SetStyle(compact_ ? GridStyle::CompactRows : s);
        grid_->maxRows = compact_ && rows > 0 ? 3 : rows;
        grid_->stretch = rows == 0;
        grid_->columns = columns;
    }
    ItemGrid* Grid() const { return grid_; }
    SectionTitle* Title() const { return title_; }
    std::vector<Route> routes_;
    std::vector<std::function<void()>> actions_;
    std::vector<QueueItem> queue_;

private:
    void Activate(int i) {
        const auto& v = grid_->Entries();
        if (i < 0 || i >= static_cast<int>(v.size())) return;
        if (i < static_cast<int>(actions_.size()) && actions_[static_cast<size_t>(i)]) {
            auto a = actions_[static_cast<size_t>(i)];
            a();
            return;
        }
        const Entry& e = v[static_cast<size_t>(i)];
        if (e.kind == EntryKind::Media && e.ref >= 0) {
            PlayQueue(queue_, e.ref, queueName_);
            return;
        }
        if (i < static_cast<int>(routes_.size()) && routes_[static_cast<size_t>(i)].section != Section::Home)
            TheApp().Navigate(routes_[static_cast<size_t>(i)]);
    }

    void Menu(int i, Point at) {
        const auto& v = grid_->Entries();
        if (i < 0 || i >= static_cast<int>(v.size()) || !GetHost()) return;
        const Entry& e = v[static_cast<size_t>(i)];
        std::vector<MenuItem> m;
        if (e.kind == EntryKind::Media) {
            m = MediaMenu(e.path, ShareQueue(queue_), e.ref, queueName_);
            if (more_.section == Section::Recent) {
                const std::wstring path = e.path;
                m.push_back(MenuItem::Separator());
                m.push_back(Item(L"Remove from History", "remove", [path] {
                    TheApp().history().Remove(path);
                    TheApp().libraryChanged.Emit();
                }));
            }
        } else {
            const Route r = i < static_cast<int>(routes_.size()) ? routes_[static_cast<size_t>(i)] : Route{};
            auto open = actions_.size() > static_cast<size_t>(i) ? actions_[static_cast<size_t>(i)] : nullptr;
            const char* openIcon = e.kind == EntryKind::Playlist ? "playlist"
                                   : e.kind == EntryKind::Album  ? "album"
                                   : e.kind == EntryKind::Artist ? "artist"
                                                                 : "folder";
            m.push_back(Item(L"Open", openIcon, [r, open] {
                if (open) open();
                else if (r.section != Section::Home) TheApp().Navigate(r);
            }));
            if (e.kind == EntryKind::Playlist) {
                const std::string plId = e.key;
                m.push_back(Item(L"Play", "playback", [plId] {
                    if (const library::Playlist* p = TheApp().playlists().Find(plId)) {
                        std::vector<QueueItem> q;
                        for (const auto& it : p->items) q.push_back(MakeQueueItem(it.path, it.title, it.duration));
                        CountPlaylistPlay(plId);
                        PlayQueue(std::move(q), 0, p->name);
                    }
                }));
            }
            if (e.kind == EntryKind::Folder && !IsUrl(e.path)) {
                const std::wstring path = e.path;
                m.push_back(Item(L"Play All", "playback", [path] { TheApp().Open({path}); }));
                m.push_back(Item(L"Show in File Explorer", "open", [path] {
                    ui::Host::ShellOpen(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }));
            }
        }
        if (!m.empty()) ShowContextMenu(GetHost(), at, std::move(m));
    }

    Route more_;
    bool compact_;
    SectionTitle* title_ = nullptr;
    ItemGrid* grid_ = nullptr;
    std::vector<Entry> entries_;
    std::wstring queueName_;
};

class HomePage : public LibraryView {
public:
    explicit HomePage(bool compact) : compact_(compact) {
        dir = Dir::Stack;
        align = Align::Stretch;
        // The scan banner sits above the scrolling sections and pushes them down while it shows.
        auto* body = Add<Widget>();
        body->dir = Dir::Column;
        body->align = Align::Stretch;
        body->width = Length::Grow();
        body->height = Length::Grow();
        scan_ = body->Add<ScanBanner>(compact);
        scroll_ = body->Add<ScrollView>();
        content_ = scroll_->SetContent<Widget>();
        content_->dir = Dir::Column;
        content_->width = Length::Grow();
        // Title of the first section sits 37 DIP under the header line, sections are 77 apart.
        content_->pad = compact ? Insets{12, 6, 12, 16} : Insets{28, 37, 20, 40};
        content_->gap = compact ? 26.0f : 77.0f;

        // Search results: artists and albums first (they lead to many songs), then files.
        results_[0] = AddSection(L"Artists", Route{Section::Music, L"artists"}, GridStyle::SquareCards);
        results_[1] = AddSection(L"Albums", Route{Section::Music, L"albums"}, GridStyle::SquareCards);
        results_[2] = AddSection(L"Videos", Route{Section::Videos, {}}, GridStyle::VideoCards);
        results_[3] = AddSection(L"Music", Route{Section::Music, L"songs"}, GridStyle::SquareCards);
        results_[0]->Grid()->roundImages = true;
        continue_ = AddSection(L"Continue Watching", Route{Section::Recent, L"continue"}, GridStyle::VideoCards);
        listen_ = AddSection(L"Listen Again", Route{Section::Recent, L"audio"}, GridStyle::SquareCards);
        favorites_ = AddSection(L"Favorites", Route{Section::Playlists, L"favorites"}, GridStyle::VideoCards);
        folders_ = AddSection(L"Frequently Visited Folders", Route{Section::Videos, L"folders"}, GridStyle::SquareCards);
        playlists_ = AddSection(L"Frequently Visited Playlists", Route{Section::Playlists, {}}, GridStyle::SquareCards);
        artists_ = AddSection(L"Frequently Listened Artists", Route{Section::Music, L"artists"}, GridStyle::SquareCards);
        artists_->Grid()->roundImages = true;
        added_ = AddSection(L"Recently Added", Route{Section::Videos, {}}, GridStyle::VideoCards);
        addedMusic_ = AddSection(L"Recently Added Music", Route{Section::Music, L"songs"}, GridStyle::SquareCards);
        results_[0]->SetGridStyle(GridStyle::SquareCards, 1);
        results_[1]->SetGridStyle(GridStyle::SquareCards, 1);
        results_[2]->SetGridStyle(GridStyle::VideoCards, 0);
        // Songs are found by the dozen: list rows with artist and album read better than big covers.
        results_[3]->SetGridStyle(GridStyle::Rows, 0, kColSubtitle | kColExtra | kColDuration);
        // Folders have no artwork: a short list of three, not tiles of one folder icon each, which ran
        // off the bottom of a 1280×780 window under Continue Watching.
        folders_->SetGridStyle(GridStyle::CompactRows, 3, 0);

        empty_ = Add<EmptyState>();
        empty_->visible = false;

        libConn_ = Connect(TheApp().libraryChanged, [this] { Rebuild(); });
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            // Behind the player screen: rebuild when Home shows again (see Page).
            if (!(m & kChangeFile)) return;
            // The player about to show (App::PlayerPending) counts: the page must not change just before.
            if (!compact_ && (TheApp().PlayerShown() || TheApp().PlayerPending())) stale_ = true;
            else Rebuild();
        });
        screenConn_ = Connect(TheApp().screenChanged, [this] {
            if (stale_ && !TheApp().PlayerShown()) Rebuild();
            else MarkSeenIfShown(Section::Home, compact_);
        });
        Rebuild();
    }

    // While searching, the header names what the page shows now: the results, not Home (UX-9).
    std::vector<Crumb> Breadcrumbs() const override {
        if (!filter_.empty()) return {{Fmt(Tr(L"Results: “{0}”"), {ShortQuery(filter_)}), Route{Section::Home, {}}}};
        return {{Tr(L"Home"), Route{Section::Home, {}}}};
    }
    long long ItemCount() const override { return count_; }
    void SetFilter(const std::wstring& text) override {
        searchDebounce_.Run(this, text, [this, text] {
            if (text == filter_) return;
            filter_ = text;
            Rebuild();
            scroll_->ScrollTo(0, false);
        });
    }
    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        // Adding and opening live in "+ Add" and the app menu; the page menu keeps what is about the page.
        m.push_back(Item(L"Rescan Library", "refresh", [] { RescanLibrary(); }, !TheApp().library().Scanning()));
        return m;
    }
    Widget* InitialFocus() override {
        if (restoreGrid_ && restoreGrid_->Parent() && restoreGrid_->Parent()->visible) return restoreGrid_;
        for (auto& c : content_->Children())
            if (auto* s = dynamic_cast<HomeSection*>(c.get()); s && s->visible) return s->Grid();
        return nullptr;
    }
    ViewState SaveState() const override {
        ViewState st;
        if (filter_.empty()) st.scroll = scroll_->Offset();  // search results are laid out differently (see Page)
        // The selection of the focused row, else of the first row that has one.
        const Widget* focus = GetHost() ? GetHost()->Focus() : nullptr;
        const ItemGrid* chosen = nullptr;
        for (HomeSection* s : All())
            if (s->visible && s->Grid()->Selected() >= 0 && (!chosen || s->Grid() == focus)) chosen = s->Grid();
        if (chosen) SaveSelection(*chosen, st);
        return st;
    }
    void RestoreState(const ViewState& st) override {
        pendingState_ = st;
        restorePending_ = true;
        restoreGrid_ = nullptr;
        for (HomeSection* s : All())
            if (s->visible && FindStateEntry(*s->Grid(), st) >= 0) {
                restoreGrid_ = s->Grid();
                break;
            }
        InvalidateLayout();
    }
    // With nothing selected, Enter (and Space while nothing is loaded) resumes the first Continue
    // Watching card, as Enter on that card does (UX-31).
    bool OnKeyDown(const KeyEvent& e) override {
        const bool space = e.key == VK_SPACE && !TheApp().player().State().fileLoaded;
        if ((e.key != VK_RETURN && !space) || e.repeat || e.mods.ctrl || e.mods.alt || e.mods.shift || !filter_.empty() ||
            !continue_->visible)
            return false;
        for (HomeSection* s : All())
            if (s->visible && s->Grid()->Selected() >= 0) return false;
        ItemGrid* g = continue_->Grid();
        if (g->JustOpened() || g->Entries().empty()) return true;
        g->Select(0, true);
        if (g->onActivate) g->onActivate(0);
        return true;
    }
    Widget* HitTest(float x, float y) override {
        auto* first = static_cast<ItemGrid*>(InitialFocus());
        return GuardedHit(first ? first : continue_->Grid(), LibraryView::HitTest(x, y));
    }
    void OnLayout() override {
        LibraryView::OnLayout();
        scan_->Attach();
        if (restorePending_) {
            restorePending_ = false;
            if (restoreGrid_) {
                ApplyViewState(*restoreGrid_, *scroll_, pendingState_);
            } else if (pendingState_.scroll >= 0) {
                scroll_->ScrollTo(pendingState_.scroll, false, false);
            }
        }
        TakeFreeFocus(*this, compact_, focusTaken_);
    }

private:
    HomeSection* AddSection(const wchar_t* title, Route more, GridStyle style) {
        return content_->Add<HomeSection>(Tr(title), std::move(more), compact_, style);
    }

    // A card for a file or stream that may or may not be in the library.
    Entry MediaCard(const std::wstring& path, const library::MediaItem* m, const library::HistoryEntry* h) const {
        Entry e;
        e.kind = EntryKind::Media;
        e.path = path;
        const bool url = IsUrl(path);
        e.audio = m ? m->kind == library::MediaKind::Audio
                    : h ? h->kind == library::MediaKind::Audio : library::KindOf(path) != library::MediaKind::Video;
        e.title = MediaTitle(path, m, h ? h->title : std::wstring());
        // Untagged songs leave the card's second line empty (G-08): "Unknown artist" under every
        // cover said nothing.
        if (e.audio) e.subtitle = m ? m->artist : url ? std::wstring(Tr(L"Network stream")) : std::wstring();
        if (!url && !(m && m->offline)) e.image = path;
        e.icon = url ? "streaming" : e.audio ? "music" : "video";
        e.duration = m && m->duration > 0 ? m->duration : h ? h->duration : 0;
        e.favorite = TheApp().favorites().Contains(path);
        e.damaged = m && m->damaged;
        e.isNew = m && ShowsNew(*m, h || played_.count(library::PathKey(path)));
        return e;
    }

    void Rebuild() {
        stale_ = false;
        App& app = TheApp();
        auto snap = app.library().Current();
        library::History& hist = app.history();
        count_ = -1;  // Home is a mix of sections: no single count describes it
        played_.clear();
        for (const library::HistoryEntry& h : hist.Entries()) played_.insert(library::PathKey(h.path));

        const bool searching = !filter_.empty();
        for (HomeSection* s : All()) s->Clear();

        if (searching) {
            int n = SearchGroups(*snap);
            for (int k = 0; k < 2; ++k) {
                HomeSection* s = results_[2 + k];
                const library::MediaKind kind = k == 0 ? library::MediaKind::Video : library::MediaKind::Audio;
                auto found = snap->Search(filter_, kind);
                library::SortItems(found, library::SortKey::Name);
                const std::wstring q = library::SearchFold(filter_);
                for (const library::MediaItem* m : found) {
                    Entry e = MediaCard(m->path, m, nullptr);
                    if (k == 1) {
                        e.subtitle = ArtistLabel(m->artist);  // a list column: says "Unknown artist"
                        e.extra = AlbumLabel(m->album);
                    }
                    // Matched by the file name only: show it, or the row wouldn't say why it is here.
                    if (k == 1 && !library::FoldedContains(library::SearchFold(m->title + L"\n" + m->artist + L"\n" + m->album), q))
                        e.subtitle = library::FileNameOf(m->path);
                    e.ref = s->AddQueue(MakeQueueItem(m->path, k == 1 ? m->DisplayTitle() : std::wstring(), m->duration));
                    s->AddEntry(std::move(e));
                    ++n;
                }
                s->Title()->text = Fmt(Tr(k == 0 ? L"Videos · {0}" : L"Music · {0}"), static_cast<long long>(found.size()));
                s->Commit(Tr(L"Search results"));
            }
            for (HomeSection* s : Sections()) s->Commit({});
            count_ = n;  // the header counts the results while searching
            empty_->visible = n == 0;
            if (n == 0)
                empty_->Set("search", Tr(L"Nothing found"),
                            Fmt(Tr(L"No videos or music match “{0}”. Try a shorter or different\u00A0word."), {ShortQuery(filter_)}));
            InvalidateLayout();
            headerChanged.Emit();
            return;
        }
        for (HomeSection* s : results_) s->Commit({});

        // Continue Watching: videos stopped halfway, and after a finished episode the show's next
        // one ("Up next", as in Infuse and Plex), newest first. Shows are grouped as the Shows tab
        // groups them ("Season N" folders too), so the card here is the one its "Next:" names.
        struct Card {
            int64_t when;
            Entry entry;
            QueueItem item;
        };
        std::vector<Card> cards;
        const ShowIndex& index = ShowsOf(snap);
        std::unordered_map<std::wstring, size_t> showOfPath;  // PathKey -> index.shows
        for (size_t s = 0; s < index.shows.size(); ++s)
            for (const library::Episode& ep : index.shows[s].episodes)
                showOfPath.emplace(library::PathKey(ep.item->path), s);
        auto findShow = [&](const std::wstring& path) {
            const auto it = showOfPath.find(library::PathKey(path));
            return it == showOfPath.end() ? -1 : static_cast<int>(it->second);
        };
        std::set<std::wstring> watched;  // PathKey
        for (const library::HistoryEntry& h : hist.Entries())
            if (h.duration > 0 && h.Progress() >= 0.95) watched.insert(library::PathKey(h.path));
        auto isWatched = [&](const std::wstring& path) { return watched.count(library::PathKey(path)) > 0; };
        std::set<int> shows;  // already represented
        std::set<std::wstring> looseShows;  // episodes outside the library: show name + folder
        for (const library::HistoryEntry& h : hist.ContinueWatching(20)) {
            // One card per show: the newest episode in progress (the list is newest first). Episodes
            // outside the library count by their name and folder.
            const int show = findShow(h.path);
            const episodes::Episode ep = episodes::Parse(h.path);
            const bool seen = show >= 0 ? !shows.insert(show).second
                                        : ep.Valid() && !IsUrl(h.path) &&
                                              !looseShows.insert(ep.show + L"\n" + library::PathKey(library::ParentFolder(h.path))).second;
            if (seen) continue;
            const library::MediaItem* m = snap->FindItem(h.path);
            Entry e = MediaCard(h.path, m, &h);
            e.audio = false;
            // An episode as the window and the mini player name it ("Show — S01E02"), not by the
            // container's title tag.
            if (ep.Valid() && !IsUrl(h.path)) e.title = episodes::Title(h.path);
            e.subtitle.clear();
            e.icon = IsUrl(h.path) ? "streaming" : "video";
            e.progress = h.Progress();
            e.duration = h.duration;
            if (h.duration > 0 && h.position > 0 && h.position < h.duration)
                e.subtitle = Fmt(Tr(L"{0} left"), {FormatDuration(h.duration - h.position)});
            // The frame where playback stopped, as Continue Watching rows in other players show.
            if (!e.image.empty() && h.position > 0) e.image = library::FramePath(h.path, h.position);
            cards.push_back({h.lastPlayed, std::move(e), MakeQueueItem(h.path, h.title, h.duration)});
        }
        size_t scanned = 0;
        for (const library::HistoryEntry& h : hist.Entries()) {
            if (++scanned > 80 || cards.size() >= 20) break;
            const int show = findShow(h.path);
            if (h.kind != library::MediaKind::Video || h.duration <= 0 || h.Progress() < 0.95 || show < 0 ||
                !shows.insert(show).second)
                continue;
            const library::Show& s = index.shows[static_cast<size_t>(show)];
            const int up = NextUpOf(s, isWatched);
            if (up < 0) continue;
            const library::MediaItem* next = s.episodes[static_cast<size_t>(up)].item;
            if (next->damaged) continue;
            if (const library::HistoryEntry* seen = hist.Find(next->path); seen && seen->Progress() > 0.02) continue;
            Entry e = MediaCard(next->path, next, nullptr);
            e.audio = false;
            // As the show's card says it: "Night Garden", "Next: S02E01" (a file may be just "01").
            e.title = s.name;
            e.subtitle = ShowEntry(s, isWatched).subtitle;
            cards.push_back({h.lastPlayed, std::move(e), MakeQueueItem(next->path, next->DisplayTitle(), next->duration)});
        }
        std::stable_sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) { return a.when > b.when; });
        for (Card& c : cards) {
            c.entry.ref = continue_->AddQueue(std::move(c.item));
            continue_->AddEntry(std::move(c.entry));
        }
        continue_->Commit(Tr(L"Continue Watching"));

        // Listen Again: recently played audio.
        std::map<std::wstring, uint32_t> artistPlays;
        std::map<std::wstring, std::wstring> artistCover;  // artist -> a song with cover art
        for (const library::HistoryEntry& h : hist.Entries()) {
            if (h.kind != library::MediaKind::Audio) continue;
            const library::MediaItem* m = snap->FindItem(h.path);
            if (m && !m->artist.empty()) {
                artistPlays[m->artist] += std::max<uint32_t>(1, h.playCount);
                if (!m->offline) artistCover.emplace(m->artist, m->path);
            }
        }
        for (const library::HistoryEntry& h : hist.ListenAgain(20)) {
            const library::MediaItem* m = snap->FindItem(h.path);
            Entry e = MediaCard(h.path, m, &h);
            e.audio = true;
            e.icon = IsUrl(h.path) ? "streaming" : "music";
            e.ref = listen_->AddQueue(MakeQueueItem(h.path, e.title, e.duration));
            listen_->AddEntry(std::move(e));
        }
        listen_->Commit(Tr(L"Listen Again"));

        // Favorites, latest first. Files that are gone from the disk are left out.
        int favVideos = 0, favCount = 0;
        const auto& favs = app.favorites().All();
        for (auto it = favs.rbegin(); it != favs.rend() && favCount < 20; ++it) {
            const std::wstring& path = *it;
            const library::MediaItem* m = IsUrl(path) ? nullptr : snap->FindItem(path);
            if (!m && !IsUrl(path) && !MaybeExists(path)) continue;
            const library::HistoryEntry* h = hist.Find(path);
            Entry e = MediaCard(path, m, h);
            e.favorite = false;  // every card here is one
            if (!e.audio) {
                ++favVideos;
                const double p = h ? h->Progress() : 0.0;
                if (p > 0.02) e.progress = std::min(1.0, p);
                e.watched = p >= 0.95;
            }
            e.ref = favorites_->AddQueue(MakeQueueItem(path, e.audio ? e.title : std::wstring(), e.duration));
            favorites_->AddEntry(std::move(e));
            ++favCount;
        }
        favorites_->SetGridStyle(favVideos * 2 > favCount ? GridStyle::VideoCards : GridStyle::SquareCards, 1);
        favorites_->Commit(Tr(L"Favorites"));

        // Frequently visited folders.
        for (const library::FolderStat& f : hist.FrequentFolders(10)) {
            if (!LooksLikeFolder(f.folder)) continue;  // URLs report their host as a "folder"
            Entry e;
            e.kind = EntryKind::Folder;
            e.path = f.folder;
            e.icon = "folder";
            e.title = FileNameOf(f.folder).empty() ? f.folder : FileNameOf(f.folder);
            if (const library::FolderNode* node = snap->Folder(f.folder)) {
                const Section sec = node->totalAudios > node->totalVideos ? Section::Music : Section::Videos;
                e.count = static_cast<int>(node->totalAudios + node->totalVideos);
                folders_->AddEntry(std::move(e), Route{sec, node->path});
            } else {
                const std::wstring path = f.folder;
                folders_->AddEntry(std::move(e), {}, [path] { TheApp().Open({path}); });
            }
        }
        folders_->Commit({});

        // Playlists, most played first.
        std::vector<const library::Playlist*> pls;
        for (const library::Playlist& p : app.playlists().All())
            if (!p.items.empty()) pls.push_back(&p);
        std::stable_sort(pls.begin(), pls.end(), [](const library::Playlist* a, const library::Playlist* b) {
            const int pa = PlaylistPlays(a->id), pb = PlaylistPlays(b->id);
            if (pa != pb) return pa > pb;
            return a->modified > b->modified;
        });
        for (size_t i = 0; i < pls.size() && i < 10; ++i) {
            const library::Playlist& p = *pls[i];
            Entry e;
            e.kind = EntryKind::Playlist;
            e.title = p.name;
            e.subtitle = CountItems(static_cast<long long>(p.items.size()));
            e.icon = "playlist";
            e.key = p.id;
            e.count = static_cast<int>(p.items.size());
            e.image = PlaylistImage(p);
            playlists_->AddEntry(std::move(e), Route{Section::Playlists, Utf8ToWide(p.id)});
        }
        playlists_->Commit({});

        // Artists by plays.
        std::vector<std::pair<std::wstring, uint32_t>> artists(artistPlays.begin(), artistPlays.end());
        std::stable_sort(artists.begin(), artists.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < artists.size() && i < 10; ++i) {
            Entry e;
            e.kind = EntryKind::Artist;
            e.title = artists[i].first;
            e.subtitle = CountPlays(artists[i].second);
            e.icon = "artist";
            if (auto c = artistCover.find(artists[i].first); c != artistCover.end()) e.image = c->second;
            artists_->AddEntry(std::move(e), Route{Section::Music, L"artist:" + artists[i].first});
        }
        artists_->Commit({});

        // Recently added videos (newest files first). Files whose length couldn't be read are
        // usually broken or not media at all: they don't open the Home page. A show with several
        // episodes is one card (its page lists them in order) where its newest episode would be.
        auto vids = snap->Videos();
        library::SortItems(vids, library::SortKey::Date, true);
        std::unordered_map<const library::MediaItem*, size_t> showOf;
        for (size_t s = 0; s < index.shows.size(); ++s)
            if (index.shows[s].episodes.size() > 1)
                for (const library::Episode& ep : index.shows[s].episodes) showOf.emplace(ep.item, s);
        std::set<size_t> showsAdded;
        for (size_t i = 0, n = 0; i < vids.size() && n < 20; ++i) {
            const library::MediaItem* m = vids[i];
            if (!(m->duration > 0)) continue;
            if (auto s = showOf.find(m); s != showOf.end()) {
                if (showsAdded.insert(s->second).second) {
                    added_->AddEntry(ShowEntry(index.shows[s->second], isWatched), ShowRoute(index.shows[s->second]));
                    ++n;
                }
                continue;
            }
            const library::HistoryEntry* h = hist.Find(m->path);
            Entry e = MediaCard(m->path, m, nullptr);
            e.ref = added_->AddQueue(MakeQueueItem(m->path, {}, m->duration));
            const double p = h ? h->Progress() : 0.0;
            if (p > 0.02) e.progress = std::min(1.0, p);
            e.watched = p >= 0.95;
            added_->AddEntry(std::move(e));
            ++n;
        }
        added_->Commit(Tr(L"Recently Added"));

        // New music, when there's no listening history to show yet.
        if (!listen_->visible) {
            auto songs = snap->Music();
            library::SortItems(songs, library::SortKey::Date, true);
            for (size_t i = 0, n = 0; i < songs.size() && n < 20; ++i) {
                const library::MediaItem* m = songs[i];
                if (!(m->duration > 0)) continue;
                Entry e = MediaCard(m->path, m, nullptr);
                e.ref = addedMusic_->AddQueue(MakeQueueItem(m->path, m->DisplayTitle(), m->duration));
                addedMusic_->AddEntry(std::move(e));
                ++n;
            }
        }
        addedMusic_->Commit(Tr(L"Recently Added Music"));

        bool any = false;
        for (HomeSection* s : Sections()) any |= s->visible;
        const bool scanning = app.library().Scanning();
        scan_->Update(!any);  // without sections the empty state says it already
        MarkSeenIfShown(Section::Home, compact_);
        empty_->visible = !any;
        if (!any) {
            const bool noRoots = app.library().Roots().empty();
            std::vector<EmptyState::Btn> buttons = {
                {Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }},
                {Tr(L"Open file"), ButtonKind::Secondary, "file", [] { TheApp().OpenFileDialog(); }}};
            if (scanning)
                empty_->Set("refresh", Tr(L"Scanning your library…"), Tr(L"Your videos and music will appear here in a moment."));
            else if (noRoots)
                empty_->Set("lumen", Tr(L"Welcome to Lumen"),
                            Tr(L"Add a folder with videos or music and Lumen will keep it in order here. "
                               L"Nothing is moved or copied."),
                            std::move(buttons));
            else
                empty_->Set("folder", Tr(L"No media found yet"),
                            Tr(L"Your library folders don't contain videos or music. Add another folder or open a file."),
                            std::move(buttons));
        }
        InvalidateLayout();
        headerChanged.Emit();
    }

    // Artists and albums whose names match the search (each one card, opening its page).
    // Returns how many were added.
    int SearchGroups(const library::Snapshot& snap) {
        const std::wstring q = library::SearchFold(filter_);
        struct Hit {
            std::wstring name, artist, image, route;
            int songs = 0;
            bool mixed = false;
        };
        std::map<std::wstring, Hit> artists, albums;  // folded name (+ album folder) -> hit
        for (const library::MediaItem& m : snap.items) {
            if (m.kind != library::MediaKind::Audio) continue;
            if (!m.artist.empty()) {
                Hit& a = artists[library::SearchFold(m.artist)];
                if (a.name.empty()) a.name = m.artist;
                if (a.image.empty() && !m.offline) a.image = m.path;
                ++a.songs;
            }
            if (!m.album.empty()) {
                // One card per album, as on the Albums page: name and folder (AlbumFolderKey).
                Hit& b = albums[library::SearchFold(m.album) + L"\n" + AlbumFolderKey(m)];
                if (b.name.empty()) {
                    b.name = m.album;
                    b.artist = m.artist;
                    b.route = AlbumRouteArg(m);
                }
                if (b.artist != m.artist) b.mixed = true;
                if (b.image.empty() && !m.offline) b.image = m.path;
                ++b.songs;
            }
        }
        int n = 0;
        for (const auto& [key, a] : artists) {
            if (!library::FoldedContains(key, q)) continue;
            Entry e;
            e.kind = EntryKind::Artist;
            e.title = a.name;
            e.subtitle = CountSongs(a.songs);
            e.icon = "artist";
            e.image = a.image;
            results_[0]->AddEntry(std::move(e), Route{Section::Music, L"artist:" + a.name});
            ++n;
        }
        const int artistHits = n;
        for (const auto& entry : albums) {
            const Hit& b = entry.second;
            const std::wstring by = b.mixed ? std::wstring(Tr(L"Various artists")) : ArtistLabel(b.artist);
            if (!library::FoldedContains(library::SearchFold(b.name), q) && !library::FoldedContains(library::SearchFold(by), q))
                continue;
            Entry e;
            e.kind = EntryKind::Album;
            e.title = b.name;
            e.subtitle = by;
            e.icon = "album";
            e.image = b.image;
            results_[1]->AddEntry(std::move(e), Route{Section::Music, L"album:" + b.route});
            ++n;
        }
        results_[0]->Title()->text = Fmt(Tr(L"Artists · {0}"), static_cast<long long>(artistHits));
        results_[1]->Title()->text = Fmt(Tr(L"Albums · {0}"), static_cast<long long>(n - artistHits));
        results_[0]->Commit({});
        results_[1]->Commit({});
        return n;
    }

    bool compact_;
    std::wstring filter_;
    long long count_ = 0;
    ScrollView* scroll_ = nullptr;
    Widget* content_ = nullptr;
    ScanBanner* scan_ = nullptr;
    EmptyState* empty_ = nullptr;
    HomeSection* results_[4] = {};  // artists, albums, videos, music
    HomeSection *continue_ = nullptr, *listen_ = nullptr, *favorites_ = nullptr, *folders_ = nullptr,
                *playlists_ = nullptr, *artists_ = nullptr, *added_ = nullptr, *addedMusic_ = nullptr;
    ScopedConnection libConn_, playerConn_, screenConn_;
    bool focusTaken_ = false;
    bool stale_ = false;  // a file started or stopped while the player screen hid Home
    std::unordered_set<std::wstring> played_;  // PathKey of every history entry (the "New" chip)
    SearchDebounce searchDebounce_;
    ViewState pendingState_;  // Back/Forward: applied after the first layout
    bool restorePending_ = false;
    ItemGrid* restoreGrid_ = nullptr;  // the row that holds the restored selection

    // The regular sections (not the search results).
    std::vector<HomeSection*> Sections() const {
        return {continue_, listen_, favorites_, folders_, playlists_, artists_, added_, addedMusic_};
    }
    std::vector<HomeSection*> All() const {
        auto v = Sections();
        for (HomeSection* r : results_) v.push_back(r);
        return v;
    }
};

}  // namespace

std::unique_ptr<LibraryView> MakeHomePage(bool compact) {
    return std::make_unique<HomePage>(compact);
}

}  // namespace app::lib
