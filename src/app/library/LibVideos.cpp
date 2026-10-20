// Videos: "Movies / Shows / Folders" tabs, a show's page and the show cards Home shares.
// Folders is the folder browser in Views.cpp.
#include "ui/Host.h"
#include "Util.h"
#include "app/I18n.h"
#include "app/library/Pages.h"
#include "library/MediaTypes.h"

#include <algorithm>
#include <cmath>

namespace app::lib {

using namespace ui;

namespace {

constexpr const wchar_t* kTabPaths[] = {L"movies", L"shows", L"folders"};
constexpr const wchar_t* kTabLabels[] = {L"Movies", L"Shows", L"Folders"};
constexpr wchar_t kShowPrefix[] = L"show:";

std::wstring Code(const library::EpisodeInfo& ep) {
    wchar_t code[32];
    swprintf_s(code, L"S%02dE%02d", ep.season, ep.episode);
    return code;
}

class VideosPage : public Page {
public:
    VideosPage(Route r, std::wstring tab, bool compact)
        : Page(r, compact, tab == L"movies" ? "videos.movies" : tab == L"shows" ? "videos.shows" : "videos.show"),
          tab_(std::move(tab)) {
        list_ = ListModeFor(key_, false);
        if (ShowPage()) {
            showKey_ = tab_.substr(wcslen(kShowPrefix));
            // "Continue S01E02" and "Play All", like a folder of episodes.
            next_ = toolbar_->Add<Button>(L"", ButtonKind::Primary, "play");
            next_->onClick = [this] {
                if (nextRef_ >= 0) PlayQueue(queue_, nextRef_, queueName_);
            };
            if (!compact) {
                auto* all = toolbar_->Add<Button>(Tr(L"Play All"), ButtonKind::Secondary, "playback");
                all->onClick = [this] {
                    if (!queue_.empty()) PlayQueue(queue_, 0, queueName_);
                };
            }
        } else {
            AddVideoTabs(toolbar_, tab_ == L"shows" ? 1 : 0);
            queueName_ = Tr(tab_ == L"shows" ? L"Shows" : L"Movies");
        }
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        auto c = Page::Breadcrumbs();
        if (ShowPage()) {
            c.push_back({Tr(L"Shows"), Route{Section::Videos, L"shows"}});
            c.push_back({showName_.empty() ? showKey_ : showName_, route_});
        }
        return c;
    }

    std::vector<MenuItem> SortMenu() override {
        if (tab_ != L"movies") return {};
        std::vector<MenuItem> m;
        SortMenuItems(m,
                      {{L"Unwatched First", "unwatched"}, {L"Watched First", "watched"}, {L"Title A-Z", "name"},
                       {L"Title Z-A", "name:desc"}, {L"Newest First", "date:desc"}, {L"Oldest First", "date"},
                       {L"Largest First", "size:desc"}, {L"Smallest First", "size"},
                       {L"Longest First", "duration:desc"}, {L"Shortest First", "duration"}},
                      "name");
        return m;
    }

    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        m.push_back(Item(L"Add Folder to Library…", "new-folder", [] { AddFolderToLibrary(); }));
        m.push_back(Item(L"Rescan Library", "refresh", [] { RescanLibrary(); }, !TheApp().library().Scanning()));
        m.push_back(LibraryFoldersItem());
        return m;
    }

protected:
    unsigned RowColumns() const override { return tab_ == L"shows" ? kColSubtitle : Page::RowColumns(); }

    void Build() override {
        if (!snap_) return;
        const ShowIndex& index = ShowsOf(snap_);
        auto watched = [this](const std::wstring& path) { return Progress(path).watched; };
        if (tab_ == L"movies") {
            const std::string sort = SortValue("name");
            AddColumns(sort, {{kHeadTitle, L"Title", "name"},
                              {kHeadDuration, L"Length", "duration"},
                              {kHeadSize, L"Size", "size"},
                              {kHeadDate, L"Modified", "date"}});
            std::vector<const library::MediaItem*> items;
            for (const library::MediaItem* m : index.movies)
                if (folded_.empty() || library::FoldedContains(m->searchKey, folded_)) items.push_back(m);
            SortMedia(items, sort);
            for (const library::MediaItem* m : items) AddMedia(*m);
            return;
        }
        if (tab_ == L"shows") {
            for (const library::Show& s : index.shows) {
                if (!Matches(s.name)) continue;
                Entry e = ShowEntry(s, watched);
                // A counted row hides its subtitle, and "Next: S01E03" says more than "4 items".
                if (list_ || compact_) e.count = -1;
                AddRoute(std::move(e), ShowRoute(s));
            }
            return;
        }
        nextRef_ = -1;
        const auto it = std::find_if(index.shows.begin(), index.shows.end(),
                                     [&](const library::Show& s) { return s.key == showKey_; });
        if (it != index.shows.end()) {
            showName_ = it->name;
            queueName_ = it->name;
            const int next = NextUpOf(*it, watched);
            int lastSeason = -1;
            for (size_t i = 0; i < it->episodes.size(); ++i) {
                const library::Episode& ep = it->episodes[i];
                if (!folded_.empty() && !library::FoldedContains(ep.item->searchKey, folded_)) continue;
                if (ep.info.season != lastSeason) {
                    AddHeader(Fmt(Tr(L"Season {0}"), static_cast<long long>(ep.info.season)));
                    lastSeason = ep.info.season;
                }
                AddMedia(*ep.item);
                if (static_cast<int>(i) == next) {
                    nextRef_ = static_cast<int>(queue_.size()) - 1;
                    nextLabel_ = Fmt(Tr(Progress(ep.item->path).progress > 0 ? L"Continue {0}" : L"Play {0}"), {Code(ep.info)});
                }
            }
        }
        if (next_) {
            toolbar_->visible = nextRef_ >= 0;
            next_->SetText(nextLabel_);
        }
    }

    void ShowMenu(int index, Point at) override {
        const Route* r = RouteAt(index);
        if (!r || !GetHost() || !snap_) {
            Page::ShowMenu(index, at);
            return;
        }
        const Route target = *r;
        const std::wstring key = target.path.substr(wcslen(kShowPrefix));
        std::vector<QueueItem> q;
        std::wstring name, folder;
        for (const library::Show& s : ShowsOf(snap_).shows)
            if (s.key == key) {
                name = s.name;
                folder = s.folder;
                for (const library::Episode& e : s.episodes) q.push_back(MakeQueueItem(e.item->path, {}, e.item->duration));
            }
        const SharedQueue shared = ShareQueue(std::move(q));
        std::vector<MenuItem> m;
        m.push_back(Item(L"Open", "folder", [target] { TheApp().Navigate(target); }));
        m.push_back(Item(L"Play All", "playback", [shared, name] { PlayQueue(*shared, 0, name); }, !shared->empty()));
        m.push_back(Item(L"Add to Queue", "queue", [shared] { AddToQueue(*shared); }, !shared->empty()));
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Show in File Explorer", "open", [folder] {
            ui::Host::ShellOpen(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }));
        ShowContextMenu(GetHost(), at, std::move(m));
    }

    void FillEmpty(EmptyState& e) override {
        App& app = TheApp();
        if (app.library().Roots().empty()) {
            e.Set("new-folder", Tr(L"Add your media folders"),
                  Tr(L"Lumen shows the videos from the folders you add. Nothing is moved or copied."),
                  {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }},
                   {Tr(L"Open file"), ButtonKind::Secondary, "file", [] { TheApp().OpenFileDialog(); }}});
        } else if (app.library().Scanning()) {
            e.Set("refresh", Tr(L"Scanning your folders…"), Tr(L"Files appear here as soon as they are found."));
        } else if (tab_ == L"movies") {
            e.Set("videos", Tr(L"No movies"), Tr(L"Videos that aren't episodes of a show appear here."));
        } else if (tab_ == L"shows") {
            e.Set("videos", Tr(L"No shows"),
                  Tr(L"Episodes named like “Show S01E02” or kept in “Season 1” folders appear here."));
        } else {
            e.Set("videos", Tr(L"This show isn't in the library"), {},
                  {{Tr(L"Shows"), ButtonKind::Secondary, "videos", [] { TheApp().Navigate(Route{Section::Videos, L"shows"}); }}});
        }
    }

private:
    bool ShowPage() const { return tab_.rfind(kShowPrefix, 0) == 0; }

    std::wstring tab_;  // "movies", "shows" or "show:<key>"
    std::wstring showKey_, showName_;
    Button* next_ = nullptr;  // "Continue S01E02" on a show's page
    int nextRef_ = -1;        // its queue index
    std::wstring nextLabel_;
};

}  // namespace

void AddVideoTabs(Widget* toolbar, int current) {
    std::vector<Choice> choices;
    float widest = 96;  // segments share one width: the longest label sets it
    for (const wchar_t* label : kTabLabels) {
        choices.push_back({Tr(label), {}, {}});
        widest = std::max(widest, std::ceil(gfx::Text::Get().Measure(choices.back().label, TextStyle::Body).width) + 24);
    }
    auto* tabs = toolbar->Add<Segmented>(std::move(choices), current);
    tabs->segmentWidth = widest;
    tabs->onChange = [tabs](int i) {
        const std::wstring path = kTabPaths[i];
        // Posted: navigating replaces this page while its control is still handling the click.
        if (Host* h = tabs->GetHost()) h->Post([path] { TheApp().Navigate(Route{Section::Videos, path}); });
    };
    toolbar->visible = true;
    TheApp().SetUiValue("tab.videos", WideToUtf8(kTabPaths[current]));
}

int NextUpOf(const library::Show& show, const std::function<bool(const std::wstring&)>& watched) {
    std::vector<bool> w;
    for (const library::Episode& e : show.episodes) w.push_back(watched(e.item->path));
    return library::NextUp(w);
}

const ShowIndex& ShowsOf(const std::shared_ptr<const library::Snapshot>& snap) {
    static ShowIndex cache;  // UI thread only
    if (cache.snap != snap) {
        cache.snap = snap;
        cache.movies.clear();
        cache.shows = snap ? library::GroupShows(snap->Videos(), &cache.movies) : std::vector<library::Show>{};
    }
    return cache;
}

Entry ShowEntry(const library::Show& show, const std::function<bool(const std::wstring&)>& watched) {
    Entry e;
    e.kind = EntryKind::Folder;
    e.title = show.name;
    e.path = show.folder;
    e.icon = "videos";
    e.count = static_cast<int>(show.episodes.size());
    const int next = NextUpOf(show, watched);
    const library::Episode& cover = show.episodes[next >= 0 ? static_cast<size_t>(next) : 0];
    if (!cover.item->offline) e.image = cover.item->path;
    e.subtitle = next >= 0 ? Fmt(Tr(L"Next: {0}"), {Code(cover.info)}) : std::wstring(Tr(L"All watched"));
    e.watched = next < 0;
    return e;
}

Route ShowRoute(const library::Show& show) {
    return Route{Section::Videos, kShowPrefix + show.key};
}

std::unique_ptr<LibraryView> MakeVideosPage(const Route& route, bool compact) {
    std::wstring tab = route.path;
    if (tab.empty()) tab = Utf8ToWide(TheApp().UiValue("tab.videos", "movies"));
    if (LooksLikeFolder(route.path)) return MakeFolderPage(route, compact);
    if (tab == L"folders") return MakeFolderPage(Route{route.section, {}}, compact);
    if (tab != L"movies" && tab != L"shows" && tab.rfind(kShowPrefix, 0) != 0) tab = L"movies";
    return std::make_unique<VideosPage>(route, std::move(tab), compact);
}

}  // namespace app::lib
