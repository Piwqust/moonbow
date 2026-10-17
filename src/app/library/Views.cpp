// Library pages: shared page base, folder browsing (Videos / Music folders), Recent and the
// CreateLibraryView factory. Home, Music, Network and Playlists live in Lib*.cpp.
#include "Util.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/library/Pages.h"
#include "library/MediaTypes.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>

namespace app {

using namespace ui;

namespace lib {

// --- EmptyState ------------------------------------------------------------------------------

EmptyState::EmptyState() {
    dir = Dir::Column;
    align = Align::Center;
    justify = Justify::Center;
    gap = 12;
    pad = Insets::All(24);
    width = Length::Grow();
    height = Length::Grow();
}

void EmptyState::Set(const char* icon, const std::wstring& title, const std::wstring& text, std::vector<Btn> buttons) {
    ClearChildren();
    auto* ic = Add<IconView>(icon ? icon : "info", 48, Tone::Tertiary);
    ic->pad = {0, 0, 0, 4};
    auto* t = Add<Label>(title, TextStyle::Title, Tone::Primary);
    t->halign = HAlign::Center;
    if (!text.empty()) {
        auto* l = Add<Label>(text, TextStyle::Body, Tone::Tertiary);
        l->wrap = true;
        l->maxW = 460;
        l->halign = HAlign::Center;
    }
    if (!buttons.empty()) {
        auto* row = Add<Widget>();
        row->dir = Dir::Row;
        row->gap = 8;
        row->pad = {0, 8, 0, 0};
        for (auto& b : buttons) {
            auto* btn = row->Add<Button>(b.label, b.kind, b.icon);
            // Run after the click: the action usually refreshes the page, which rebuilds this
            // empty state (and destroys the button) while it is still being clicked.
            btn->onClick = [this, action = b.action] {
                if (Host* h = GetHost())
                    h->Post(action);
                else if (action)
                    action();
            };
        }
    }
    InvalidateLayout();
}

Widget* EmptyState::HitTest(float x, float y) {
    Widget* hit = Widget::HitTest(x, y);
    if (hit == this) return nullptr;
    for (Widget* w = hit; w && w != this; w = w->Parent())
        if (w->interactive) return hit;
    return nullptr;
}

// --- ScanBadge ---------------------------------------------------------------------------------

ScanBadge::ScanBadge(bool compact) : compact_(compact) {
    width = Length::Grow();
    height = Length::Grow();
    visible = false;
}

void ScanBadge::Update() {
    App& app = TheApp();
    visible = app.library().Scanning();
    if (!visible) return;
    const size_t files = app.library().Current()->items.size();
    text_ = files && !compact_ ? Fmt(Tr(L"Scanning library… {0} files so far"), static_cast<long long>(files))
                  : std::wstring(Tr(L"Scanning library…"));
    Invalidate();
}

void ScanBadge::Paint(Painter& p) {
    const Palette& c = P();
    const float tw = std::ceil(p.Measure(text_, TextStyle::Small).width);
    // Bottom center: the mini player owns a corner of the library.
    const float w = std::min(tw + 46, rect.w - 24);
    const Rect pill{std::round(rect.Cx() - w / 2), rect.Bottom() - (compact_ ? 12.0f : 20.0f) - 32, w, 32};
    p.Shadow(pill, 16, 12, 4);
    p.Fill(pill, c.bgMenu, 16);
    p.Stroke(pill, c.line, 16);
    p.Icon("refresh", {pill.x + 12, pill.Cy() - 8, 16, 16}, c.textSecondary);
    p.Text(text_, {pill.x + 34, pill.y, pill.w - 44, pill.h}, TextStyle::Small, c.textSecondary);
}

// --- SectionTitle ------------------------------------------------------------------------------

SectionTitle::SectionTitle(std::wstring t, bool compact) : text(std::move(t)), compact_(compact) {
    interactive = true;
    focusable = true;
    cursor = IDC_HAND;
    height = Length::Px(compact ? 32.0f : 24.0f);
    if (compact) width = Length::Grow();
}

Size SectionTitle::MeasureContent(float availW, float) {
    const float w = gfx::Text::Get().Measure(text, compact_ ? TextStyle::Body : TextStyle::Section).width;
    return {std::min(availW, w + 5 + 24), compact_ ? 32.0f : 24.0f};
}

void SectionTitle::Paint(Painter& p) {
    const Palette& c = P();
    const bool hot = IsHot();
    const Color fg = hot ? c.textPrimary : c.textSecondary;
    if (compact_) {
        if (hot) p.Fill({rect.x - 8, rect.y, rect.w + 16, rect.h}, c.hover, 6);
        p.Text(text, {rect.x, rect.y, rect.w - 28, rect.h}, TextStyle::Body, fg);
        p.Icon("chevron_right", {rect.Right() - 22, rect.Cy() - 10, 20, 20}, fg);
    } else {
        const float tw = std::min(rect.w - 29, gfx::Text::Get().Measure(text, TextStyle::Section).width + 1);
        p.Text(text, {rect.x, rect.y - 2, tw, rect.h + 4}, TextStyle::Section, fg);
        p.Icon("chevron_right", {rect.x + tw + 5, rect.Cy() - 12, 24, 24}, fg);
    }
    PaintFocusRing(p, rect.Inset(-2), 4);
}

// --- Page ------------------------------------------------------------------------------------

Page::Page(Route route, bool compact, std::string key) : route_(std::move(route)), compact_(compact), key_(std::move(key)) {
    dir = Dir::Stack;
    align = Align::Stretch;
    scroll_ = Add<ScrollView>();
    content_ = scroll_->SetContent<Widget>();
    content_->dir = Dir::Column;
    content_->width = Length::Grow();
    content_->pad = compact ? Insets{12, 6, 12, 16} : Insets{28, 20, 20, 32};
    content_->gap = 0;

    toolbar_ = content_->Add<Widget>();
    toolbar_->dir = Dir::Row;
    toolbar_->align = Align::Center;
    toolbar_->gap = 8;
    toolbar_->width = Length::Grow();
    toolbar_->pad = {0, 0, 0, compact ? 12.0f : 24.0f};
    toolbar_->visible = false;

    grid_ = content_->Add<ItemGrid>();
    grid_->onActivate = [this](int i) { Activate(i); };
    grid_->onMenu = [this](int i, Point at) { ShowMenu(i, at); };
    grid_->onDelete = [this](int i) { Delete(i); };
    grid_->onMove = [this](int from, int to) { Move(from, to); };

    empty_ = Add<EmptyState>();
    empty_->visible = false;
    scan_ = Add<ScanBadge>(compact);

    libConn_ = Connect(TheApp().libraryChanged, [this] { Refresh(); });
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        // Watched progress and history change when a file starts or stops.
        if (m & kChangeFile) Refresh();
    });
}

Page::~Page() = default;

std::vector<Crumb> Page::Breadcrumbs() const {
    return {{Tr(SectionName(route_.section)), Route{route_.section, {}}}};
}

void Page::SetViewMode(ViewMode m) {
    list_ = m == ViewMode::List;
    SetListModeFor(key_, list_);
    Refresh();
}

void Page::SetFilter(const std::wstring& text) {
    if (text == filter_) return;
    filter_ = text;
    Refresh();
    if (scroll_) scroll_->ScrollTo(0, false);
}

GridStyle Page::CurrentStyle() const {
    if (compact_) return GridStyle::CompactRows;
    return list_ ? GridStyle::Rows : CardStyle();
}

int Page::Push(Entry e) {
    entries_.push_back(std::move(e));
    nextRoutes_.emplace_back();
    nextActions_.emplace_back();
    return static_cast<int>(entries_.size()) - 1;
}

int Page::AddRoute(Entry e, Route target) {
    const int i = Push(std::move(e));
    nextRoutes_[static_cast<size_t>(i)] = std::move(target);
    return i;
}

int Page::AddAction(Entry e, std::function<void()> action) {
    e.kind = e.kind == EntryKind::Media ? EntryKind::Action : e.kind;
    const int i = Push(std::move(e));
    nextActions_[static_cast<size_t>(i)] = std::move(action);
    return i;
}

void Page::AddHeader(const std::wstring& title, const std::wstring& right) {
    Entry e;
    e.kind = EntryKind::Header;
    e.title = title;
    e.subtitle = right;
    Push(std::move(e));
}

HistoryInfo Page::Progress(const std::wstring& path) const {
    auto it = history_.find(library::PathKey(path));
    return it == history_.end() ? HistoryInfo{} : it->second;
}

bool Page::Matches(const std::wstring& text) const {
    return folded_.empty() || library::FoldedContains(library::SearchFold(text), folded_);
}

int Page::AddMedia(const library::MediaItem& m, bool fileTitle) {
    Entry e;
    e.kind = EntryKind::Media;
    e.path = m.path;
    e.audio = m.kind == library::MediaKind::Audio;
    if (e.audio) {
        e.title = m.DisplayTitle();
        e.subtitle = m.artist;
        e.extra = m.album;
    } else {
        e.title = fileTitle ? FileNameOf(m.path) : m.DisplayTitle();
    }
    if (!m.offline) e.image = m.path;
    e.icon = e.audio ? "music" : "video";
    e.duration = m.duration;
    e.size = m.size;
    e.date = m.mtime;
    const HistoryInfo h = Progress(m.path);
    e.progress = h.progress;
    e.watched = h.watched && !e.audio;
    e.ref = static_cast<int>(queue_.size());
    queue_.push_back(MakeQueueItem(m.path, e.audio ? m.DisplayTitle() : std::wstring(), m.duration));
    return Push(std::move(e));
}

Entry Page::FolderEntry(const library::FolderNode& f, library::MediaKind kind) const {
    Entry e;
    e.kind = EntryKind::Folder;
    e.title = f.name.empty() ? f.path : f.name;
    e.path = f.path;
    e.icon = "folder";
    e.count = static_cast<int>(kind == library::MediaKind::Audio ? f.totalAudios
                               : kind == library::MediaKind::Video ? f.totalVideos
                                                                   : f.totalAudios + f.totalVideos);
    return e;
}

const Entry* Page::EntryAt(int index) const {
    const auto& v = grid_->Entries();
    return index >= 0 && index < static_cast<int>(v.size()) ? &v[static_cast<size_t>(index)] : nullptr;
}

const Route* Page::RouteAt(int index) const {
    if (index < 0 || index >= static_cast<int>(routes_.size())) return nullptr;
    const Route& r = routes_[static_cast<size_t>(index)];
    return r.path.empty() && r.section == Section::Home ? nullptr : &r;
}

void Page::Refresh() {
    App& app = TheApp();
    snap_ = app.library().Current();
    folded_ = filter_.empty() ? std::wstring() : library::SearchFold(filter_);
    history_.clear();
    for (const library::HistoryEntry& h : app.history().Entries()) {
        HistoryInfo info;
        const double p = h.Progress();
        if (h.duration > 0 && p >= 0.95) {
            info.watched = true;
            info.progress = 1;
        } else if (p > 0.02) {
            info.progress = p;
        }
        history_.emplace(library::PathKey(h.path), info);
    }
    entries_.clear();
    nextRoutes_.clear();
    nextActions_.clear();
    queue_.clear();
    Build();

    long long n = 0;
    bool actions = false;
    for (const Entry& e : entries_) {
        if (e.kind == EntryKind::Header) continue;
        if (e.kind == EntryKind::Action) {
            actions = true;
            continue;
        }
        ++n;
    }
    count_ = n;
    routes_ = std::move(nextRoutes_);
    actions_ = std::move(nextActions_);
    grid_->SetStyle(CurrentStyle());
    grid_->columns = compact_ ? kColDuration : RowColumns();
    grid_->reorderable = Reorderable() && filter_.empty();
    grid_->SetEntries(std::move(entries_));
    entries_.clear();

    scan_->Update();

    const bool empty = n == 0;
    empty_->visible = empty;
    if (empty) {
        if (!filter_.empty())
            empty_->Set("search", Tr(L"Nothing found"),
                        Fmt(Tr(L"No matches for “{0}” here. Try a shorter or different word."), {filter_}));
        else
            FillEmpty(*empty_);
        // Pages that still offer actions (Add folder, Create playlist) keep the grid visible.
        if (actions && compact_) empty_->visible = false;
        if (empty_->visible) scan_->visible = false;  // the empty state explains the scan itself
    }
    InvalidateLayout();
    headerChanged.Emit();
}

void Page::FillEmpty(EmptyState& e) {
    e.Set(SectionIcon(route_.section), Tr(L"Nothing here yet"), {});
}

void Page::Activate(int index) {
    const Entry* e = EntryAt(index);
    if (!e) return;
    if (index < static_cast<int>(actions_.size()) && actions_[static_cast<size_t>(index)]) {
        auto action = actions_[static_cast<size_t>(index)];
        action();
        return;
    }
    if (const Route* r = RouteAt(index)) {
        TheApp().Navigate(*r);
        return;
    }
    if (e->kind == EntryKind::Media && e->ref >= 0) PlayQueue(queue_, e->ref, queueName_);
}

std::vector<QueueItem> Page::FolderQueue(const std::wstring& folder, library::MediaKind kind) const {
    std::vector<QueueItem> q;
    if (!snap_) return q;
    auto items = snap_->ItemsIn(folder, true);
    library::SortItems(items, library::SortKey::Name);
    // Group by subfolder order: ItemsIn already walks folders in natural order; keep paths sorted.
    std::stable_sort(items.begin(), items.end(), [](const library::MediaItem* a, const library::MediaItem* b) {
        return StrCmpLogicalW(a->folder.c_str(), b->folder.c_str()) < 0;
    });
    for (const library::MediaItem* m : items)
        if (kind == library::MediaKind::Other || m->kind == kind)
            q.push_back(MakeQueueItem(m->path, m->kind == library::MediaKind::Audio ? m->DisplayTitle() : std::wstring(),
                                      m->duration));
    return q;
}

std::vector<MenuItem> Page::FolderMenu(const std::wstring& folder, library::MediaKind kind, Section section) const {
    std::vector<MenuItem> m;
    auto q = FolderQueue(folder, kind);
    const std::wstring name = FileNameOf(folder).empty() ? folder : FileNameOf(folder);
    m.push_back(Item(L"Open", "folder", [folder, section] { TheApp().Navigate(Route{section, folder}); }));
    m.push_back(Item(L"Play All", "play", [q, name] { PlayQueue(q, 0, name); }, !q.empty()));
    m.push_back(Item(L"Add to Queue", "queue", [q] { AddToQueue(q); }, !q.empty()));
    MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr, !q.empty());
    if (!q.empty()) pl.submenu = AddToPlaylistMenu(q);
    m.push_back(std::move(pl));
    if (kind == library::MediaKind::Video)
        m.push_back(Item(L"Add Subtitles…", "subtitles", [folder] { ShowAddSubtitlesDialog(folder); }, !q.empty()));
    m.push_back(MenuItem::Separator());
    m.push_back(Item(L"Show in File Explorer", "open", [folder] { ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }));
    const auto& roots = TheApp().library().Roots();
    if (std::any_of(roots.begin(), roots.end(), [&](const std::wstring& r) { return SamePath(r, folder); })) {
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Remove from Library", "remove", [folder, name] {
            ShowConfirm(Tr(L"Remove folder from library"),
                        Fmt(Tr(L"“{0}” will no longer appear in Lumen. The files stay on your disk."), {name}),
                        Tr(L"Remove"), [folder] { RemoveLibraryRoot(folder); });
        }));
    }
    return m;
}

void Page::ShowMenu(int index, Point at) {
    const Entry* e = EntryAt(index);
    if (!e || !GetHost()) return;
    std::vector<MenuItem> items;
    if (e->kind == EntryKind::Media) {
        items = MediaMenu(e->path, queue_, e->ref, queueName_);
    } else if (e->kind == EntryKind::Folder) {
        const Route* r = RouteAt(index);
        const Section sec = r ? r->section : route_.section;
        items = FolderMenu(e->path, sec == Section::Music ? library::MediaKind::Audio : library::MediaKind::Video, sec);
    }
    ExtraMenu(index, items);
    if (!items.empty()) ShowContextMenu(GetHost(), at, std::move(items));
}

std::string Page::SortValue(const std::string& fallback) const {
    return TheApp().UiValue("sort." + key_, fallback);
}

void Page::SetSort(const std::string& value) {
    TheApp().SetUiValue("sort." + key_, value);
    Refresh();
}

void Page::SortMenuItems(std::vector<MenuItem>& out, const std::vector<std::pair<const wchar_t*, std::string>>& options) {
    const std::string cur = SortValue(options.empty() ? std::string() : options.front().second);
    for (const auto& [label, value] : options) {
        MenuItem m;
        m.label = Tr(label);
        m.radio = true;
        m.checked = cur == value;
        const std::string v = value;
        m.action = [this, v] { SetSort(v); };
        out.push_back(std::move(m));
    }
}

void Page::SortMedia(std::vector<const library::MediaItem*>& items, const std::string& sort) const {
    const bool desc = sort.size() > 5 && sort.compare(sort.size() - 5, 5, ":desc") == 0;
    const std::string k = desc ? sort.substr(0, sort.size() - 5) : sort;
    auto byText = [&](auto get) {
        std::stable_sort(items.begin(), items.end(), [&](const library::MediaItem* a, const library::MediaItem* b) {
            const std::wstring& x = get(a);
            const std::wstring& y = get(b);
            if (x.empty() != y.empty()) return y.empty();  // untagged last
            const int c = StrCmpLogicalW(x.c_str(), y.c_str());
            return desc ? c > 0 : c < 0;
        });
    };
    if (k == "date") {
        library::SortItems(items, library::SortKey::Date, desc);
    } else if (k == "size") {
        library::SortItems(items, library::SortKey::Size, desc);
    } else if (k == "duration") {
        library::SortItems(items, library::SortKey::Duration, desc);
    } else if (k == "title") {
        library::SortItems(items, library::SortKey::Name);
        byText([](const library::MediaItem* m) -> const std::wstring& { return m->DisplayTitle(); });
    } else if (k == "artist") {
        library::SortItems(items, library::SortKey::Name);
        byText([](const library::MediaItem* m) -> const std::wstring& { return m->artist; });
    } else if (k == "album") {
        library::SortItems(items, library::SortKey::Name);
        byText([](const library::MediaItem* m) -> const std::wstring& { return m->album; });
    } else if (k == "unwatched" || k == "watched") {
        library::SortItems(items, library::SortKey::Name);
        const bool watchedFirst = k == "watched";
        std::stable_partition(items.begin(), items.end(), [&](const library::MediaItem* m) {
            return Progress(m->path).watched == watchedFirst;
        });
    } else {
        library::SortItems(items, library::SortKey::Name, desc);
    }
}

std::vector<Crumb> FolderCrumbs(const library::Snapshot& snap, Section section, const std::wstring& folder) {
    std::vector<Crumb> out;
    out.push_back({Tr(SectionName(section)), Route{section, {}}});
    int idx = snap.FindFolder(folder);
    std::vector<Crumb> chain;
    for (int guard = 0; idx >= 0 && guard < 256; ++guard) {
        const library::FolderNode& f = snap.folders[static_cast<size_t>(idx)];
        chain.push_back({f.name.empty() ? f.path : f.name, Route{section, f.path}});
        idx = f.parent;
    }
    if (chain.empty() && !folder.empty()) chain.push_back({FileNameOf(folder).empty() ? folder : FileNameOf(folder), Route{section, folder}});
    out.insert(out.end(), chain.rbegin(), chain.rend());
    return out;
}

// --- Folder page (Videos and Music folders) -------------------------------------------------------

namespace {

class FolderPage : public Page {
public:
    FolderPage(Route r, bool compact)
        : Page(r, compact, r.section == Section::Music ? "music.folders" : "videos"),
          kind_(r.section == Section::Music ? library::MediaKind::Audio : library::MediaKind::Video) {
        list_ = ListModeFor(key_, false);
        const std::wstring name = FileNameOf(route_.path);
        queueName_ = route_.path.empty() ? std::wstring(Tr(SectionName(route_.section))) : name;
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        if (route_.path.empty() || !snap_) return Page::Breadcrumbs();
        return FolderCrumbs(*snap_, route_.section, route_.path);
    }

    std::vector<MenuItem> SortMenu() override {
        std::vector<MenuItem> m;
        std::vector<std::pair<const wchar_t*, std::string>> o = {
            {L"Title A-Z", "name"},          {L"Title Z-A", "name:desc"},     {L"Newest First", "date:desc"},
            {L"Oldest First", "date"},       {L"Largest First", "size:desc"}, {L"Smallest First", "size"},
            {L"Longest First", "duration:desc"}, {L"Shortest First", "duration"}};
        if (kind_ == library::MediaKind::Video) {
            o.insert(o.begin(), {{L"Unwatched First", "unwatched"}, {L"Watched First", "watched"}});
            // Keep "Title A-Z" as the default although unwatched comes first in the list.
        }
        const std::string cur = SortValue("name");
        for (const auto& [label, value] : o) {
            MenuItem item;
            item.label = Tr(label);
            item.radio = true;
            item.checked = cur == value;
            const std::string v = value;
            item.action = [this, v] { SetSort(v); };
            m.push_back(std::move(item));
        }
        return m;
    }

    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        const std::wstring path = route_.path;
        if (!path.empty()) {
            auto q = FolderQueue(path, kind_);
            const std::wstring name = FileNameOf(path);
            m.push_back(Item(L"Play All", "play", [q, name] { PlayQueue(q, 0, name); }, !q.empty()));
            m.push_back(Item(L"Show in File Explorer", "open",
                             [path] { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }));
            m.push_back(MenuItem::Separator());
        }
        m.push_back(Item(L"Add Folder to Library…", "new-folder", [] { AddFolderToLibrary(); }));
        m.push_back(Item(L"Rescan Library", "refresh", [] { RescanLibrary(); }, !TheApp().library().Scanning()));
        MenuItem hidden = Item(L"Show Hidden Files", "view", [] {
            Config& cfg = TheApp().config();
            cfg.SetBool("library.showHidden", !cfg.GetBool("library.showHidden"));
        });
        hidden.checked = TheApp().config().GetBool("library.showHidden");
        m.push_back(std::move(hidden));
        return m;
    }

protected:
    GridStyle CardStyle() const override {
        return kind_ == library::MediaKind::Audio ? GridStyle::SquareCards : GridStyle::VideoCards;
    }
    unsigned RowColumns() const override {
        return kind_ == library::MediaKind::Audio ? (kColSubtitle | kColDuration | kColSize)
                                                  : (kColDuration | kColSize | kColDate);
    }

    void Build() override {
        if (!snap_) return;
        const bool video = kind_ == library::MediaKind::Video;
        const std::string sort = SortValue("name");
        const bool desc = sort == "name:desc";

        if (!filter_.empty()) {
            std::vector<const library::MediaItem*> items;
            if (route_.path.empty()) {
                items = snap_->Search(filter_, kind_);
            } else {
                for (const library::MediaItem* m : snap_->ItemsIn(route_.path, true))
                    if (m->kind == kind_ && library::FoldedContains(m->searchKey, folded_)) items.push_back(m);
                for (const library::FolderNode* f : snap_->Children(route_.path))
                    if (Matches(f->name) && (video ? f->totalVideos : f->totalAudios) > 0)
                        AddRoute(FolderEntry(*f, kind_), Route{route_.section, f->path});
            }
            SortMedia(items, sort);
            for (const library::MediaItem* m : items) AddMedia(*m, video);
            return;
        }

        if (route_.path.empty()) {
            // The side panel and the list keep the design's "Add" row; the grid has it in the menus.
            if (compact_ || list_) {
                Entry add;
                add.kind = EntryKind::Action;
                add.title = Tr(L"Add folder");
                add.icon = "add";
                AddAction(std::move(add), [] { AddFolderToLibrary(); });
            }
            auto roots = snap_->Roots();
            if (desc) std::reverse(roots.begin(), roots.end());
            for (const library::FolderNode* f : roots) {
                // A music-only folder doesn't belong on the Videos page (and vice versa).
                if ((video ? f->totalVideos : f->totalAudios) == 0) continue;
                Entry e = FolderEntry(*f, kind_);
                if (compact_ || list_) e.subtitle = f->path;
                AddRoute(std::move(e), Route{route_.section, f->path});
            }
            return;
        }

        const library::FolderNode* folder = snap_->Folder(route_.path);
        if (!folder) return;
        auto children = snap_->Children(route_.path);
        if (desc) std::reverse(children.begin(), children.end());
        for (const library::FolderNode* f : children)
            if ((video ? f->totalVideos : f->totalAudios) > 0) AddRoute(FolderEntry(*f, kind_), Route{route_.section, f->path});
        std::vector<const library::MediaItem*> items;
        for (int i : folder->items) {
            const library::MediaItem& m = snap_->items[static_cast<size_t>(i)];
            if (m.kind == kind_) items.push_back(&m);
        }
        SortMedia(items, sort);
        for (const library::MediaItem* m : items) AddMedia(*m, video);
    }

    void FillEmpty(EmptyState& e) override {
        App& app = TheApp();
        const bool scanning = app.library().Scanning();
        const bool video = kind_ == library::MediaKind::Video;
        if (route_.path.empty()) {
            if (app.library().Roots().empty()) {
                e.Set("new-folder", Tr(L"Add your media folders"),
                      video ? Tr(L"Lumen shows the videos from the folders you add. Nothing is moved or copied.")
                            : Tr(L"Lumen shows the music from the folders you add. Nothing is moved or copied."),
                      {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }},
                       {Tr(L"Open file"), ButtonKind::Secondary, "file", [] { TheApp().OpenFileDialog(); }}});
            } else if (scanning) {
                e.Set("refresh", Tr(L"Scanning your folders…"), Tr(L"Files appear here as soon as they are found."));
            } else {
                e.Set(video ? "videos" : "songs", video ? Tr(L"No videos found") : Tr(L"No music found"),
                      Tr(L"The library folders don't contain supported files yet."),
                      {{Tr(L"Add folder"), ButtonKind::Primary, "new-folder", [] { AddFolderToLibrary(); }},
                       {Tr(L"Rescan"), ButtonKind::Secondary, "refresh", [] { RescanLibrary(); }}});
            }
            return;
        }
        if (!snap_ || !snap_->Folder(route_.path)) {
            if (scanning) {
                e.Set("refresh", Tr(L"Scanning your folders…"), Tr(L"Files appear here as soon as they are found."));
                return;
            }
            const std::wstring path = route_.path;
            const Section sec = route_.section;
            e.Set("folder", Tr(L"This folder isn't in the library"),
                  Tr(L"It may have been removed, renamed or it lives outside your library folders."),
                  {{Tr(L"Play folder"), ButtonKind::Primary, "play", [path] { TheApp().Open({path}); }},
                   {Tr(SectionName(sec)), ButtonKind::Secondary, SectionIcon(sec), [sec] { TheApp().Navigate(Route{sec, {}}); }}});
            return;
        }
        e.Set("folder", video ? Tr(L"No videos in this folder") : Tr(L"No music in this folder"), {});
    }

private:
    library::MediaKind kind_;
};

// --- Recent ----------------------------------------------------------------------------------------

class RecentPage : public Page {
public:
    RecentPage(Route r, bool compact) : Page(r, compact, "recent") {
        modes_ = false;
        list_ = true;
        queueName_ = Tr(L"Recent");
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        auto c = Page::Breadcrumbs();
        if (route_.path == L"continue") c.push_back({Tr(L"Continue Watching"), route_});
        if (route_.path == L"audio") c.push_back({Tr(L"Listen Again"), route_});
        return c;
    }

    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        auto view = [&](const wchar_t* label, const wchar_t* path) {
            MenuItem it;
            it.label = Tr(label);
            it.radio = true;
            it.checked = route_.path == path;
            const std::wstring p = path;
            it.action = [p] { TheApp().Navigate(Route{Section::Recent, p}); };
            m.push_back(std::move(it));
        };
        view(L"Everything", L"");
        view(L"Continue Watching", L"continue");
        view(L"Listen Again", L"audio");
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Clear History…", "remove", [] {
            ShowConfirm(Tr(L"Clear history"), Tr(L"Recently played items and saved positions will be forgotten."),
                        Tr(L"Clear"), [] {
                            TheApp().history().Clear();
                            TheApp().history().Flush();
                            TheApp().libraryChanged.Emit();
                        });
        }, TheApp().history().size() > 0));
        return m;
    }

protected:
    unsigned RowColumns() const override { return kColSubtitle | kColDuration | kColDate | kColTime; }

    void Build() override {
        library::History& h = TheApp().history();
        std::vector<library::HistoryEntry> list;
        if (route_.path == L"continue")
            list = h.ContinueWatching(library::History::kMaxEntries);
        else if (route_.path == L"audio")
            list = h.ListenAgain(library::History::kMaxEntries);
        else
            list = h.Entries();
        const bool grouped = route_.path.empty();
        int lastBucket = -1;
        // Counts per group for the header's right side.
        int counts[3] = {0, 0, 0};
        std::vector<const library::HistoryEntry*> shown;
        for (const library::HistoryEntry& he : list) {
            const std::wstring title = he.title.empty() ? (IsUrl(he.path) ? he.path : FileNameOf(he.path)) : he.title;
            if (!Matches(title + L" " + he.path)) continue;
            shown.push_back(&he);
            ++counts[DayBucket(he.lastPlayed)];
        }
        static const wchar_t* kGroups[] = {L"Today", L"Yesterday", L"Earlier"};
        for (const library::HistoryEntry* hp : shown) {
            const library::HistoryEntry& he = *hp;
            const int bucket = DayBucket(he.lastPlayed);
            if (grouped && bucket != lastBucket) {
                AddHeader(Tr(kGroups[bucket]), CountItems(counts[bucket]));
                lastBucket = bucket;
            }
            const library::MediaItem* m = snap_ ? snap_->FindItem(he.path) : nullptr;
            Entry e;
            e.kind = EntryKind::Media;
            e.path = he.path;
            const bool url = IsUrl(he.path);
            e.audio = he.kind == library::MediaKind::Audio;
            if (m && e.audio)
                e.title = m->DisplayTitle();
            else
                e.title = he.title.empty() ? (url ? he.path : FileNameOf(he.path)) : he.title;
            if (!url && !(m && m->offline)) e.image = he.path;
            e.icon = url ? "streaming" : e.audio ? "music" : "video";
            e.duration = he.duration > 0 ? he.duration : (m ? m->duration : 0);
            e.date = he.lastPlayed;
            const double p = he.Progress();
            if (he.duration > 0 && p >= 0.95) {
                e.watched = !e.audio;
                e.progress = 1;
            } else if (p > 0.02) {
                e.progress = p;
            }
            if (e.progress > 0 && e.progress < 1 && he.duration > 0)
                e.subtitle = Fmt(Tr(L"{0} left"), {FormatDuration(he.duration - he.position)});
            else if (m && e.audio && !m->artist.empty())
                e.subtitle = m->artist;
            else if (url)
                e.subtitle = Tr(L"Network stream");
            else
                e.subtitle = FileNameOf(library::ParentFolder(he.path));
            e.ref = static_cast<int>(queue_.size());
            queue_.push_back(MakeQueueItem(he.path, he.title, e.duration));
            Push(std::move(e));
        }
    }

    void ExtraMenu(int index, std::vector<MenuItem>& m) override {
        const Entry* e = EntryAt(index);
        if (!e) return;
        const std::wstring path = e->path;
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Remove from History", "remove", [path] {
            TheApp().history().Remove(path);
            TheApp().libraryChanged.Emit();
        }));
    }

    void Delete(int index) override {
        const Entry* e = EntryAt(index);
        if (!e || e->kind != EntryKind::Media) return;
        const int keep = index;
        TheApp().history().Remove(e->path);
        TheApp().libraryChanged.Emit();
        // Keep the keyboard position on the next row.
        const int n = static_cast<int>(grid_->Entries().size());
        for (int i = std::min(keep, n - 1); i >= 0; --i)
            if (grid_->Entries()[static_cast<size_t>(i)].kind != EntryKind::Header) {
                grid_->Select(i, true);
                break;
            }
    }

    void FillEmpty(EmptyState& e) override {
        if (route_.path == L"continue")
            e.Set("videos", Tr(L"Nothing to continue"), Tr(L"Videos you stop halfway will wait for you here."));
        else if (route_.path == L"audio")
            e.Set("songs", Tr(L"No music played yet"), Tr(L"Songs you listen to will appear here."));
        else
            e.Set("recent", Tr(L"No recent items"), Tr(L"Files and streams you play will appear here."),
                  {{Tr(L"Open file"), ButtonKind::Secondary, "file", [] { TheApp().OpenFileDialog(); }}});
    }
};

}  // namespace

std::unique_ptr<LibraryView> MakeFolderPage(const Route& route, bool compact) {
    return std::make_unique<FolderPage>(route, compact);
}

std::unique_ptr<LibraryView> MakeRecentPage(const Route& route, bool compact) {
    return std::make_unique<RecentPage>(route, compact);
}

}  // namespace lib

std::unique_ptr<LibraryView> CreateLibraryView(const Route& route, bool compact) {
    switch (route.section) {
        case Section::Home: return lib::MakeHomePage(compact);
        case Section::Videos: return lib::MakeFolderPage(route, compact);
        case Section::Music: return lib::MakeMusicPage(route, compact);
        case Section::Network: return lib::MakeNetworkPage(route, compact);
        case Section::Playlists: return lib::MakePlaylistsPage(route, compact);
        case Section::Recent: return lib::MakeRecentPage(route, compact);
    }
    return lib::MakeHomePage(compact);
}

}  // namespace app
