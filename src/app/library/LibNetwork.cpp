// Network: "Add network stream", saved streams, Podcasts and Radio link groups and recently
// played URLs (History entries with "://"). Radio starts with a few public stations; every
// group is a plain list of saved links kept in ui.json.
#include "Util.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/Pages.h"
#include "ui/Host.h"

#include <shlwapi.h>

#include <algorithm>

namespace app::lib {

using namespace ui;

namespace {

std::wstring HostOf(const std::wstring& url) {
    size_t s = url.find(L"://");
    if (s == std::wstring::npos) return url;
    s += 3;
    const size_t e = url.find_first_of(L"/?#", s);
    std::wstring host = url.substr(s, e == std::wstring::npos ? std::wstring::npos : e - s);
    if (host.compare(0, 4, L"www.") == 0) host = host.substr(4);
    return host;
}

std::wstring TitleFromUrl(const std::wstring& url) {
    std::wstring u = url;
    const size_t q = u.find_first_of(L"?#");
    if (q != std::wstring::npos) u.resize(q);
    while (!u.empty() && u.back() == L'/') u.pop_back();
    const size_t slash = u.find_last_of(L'/');
    std::wstring last = slash == std::wstring::npos ? u : u.substr(slash + 1);
    const std::wstring host = HostOf(url);
    if (last.empty() || last == host || last.find(L"://") != std::wstring::npos) return host;
    // Percent-decoding keeps Cyrillic names readable.
    wchar_t buf[2048];
    DWORD n = 2048;
    if (last.size() < 2000 && SUCCEEDED(UrlUnescapeW(last.data(), buf, &n, 0))) last = buf;
    return last;
}

void CopyText(const std::wstring& text) {
    HWND hwnd = TheApp().main() ? TheApp().main()->Hwnd() : nullptr;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
    Toast(Tr(L"Link copied"), "link");
}

const wchar_t* GroupTitle(const std::wstring& g) {
    if (g == L"radio") return L"Radio";
    if (g == L"podcasts") return L"Podcasts";
    return L"Saved Streams";
}

// One way in on the Network page (open a link, Radio, Podcasts): an icon, the name and what it
// holds, as a card like the other library pages have (UX-12), not a bare text row.
class HubCard : public Widget {
public:
    HubCard(const char* icon, std::wstring title, std::function<void()> action) : icon_(icon), title_(std::move(title)) {
        interactive = true;
        focusable = true;
        cursor = IDC_HAND;
        width = Length::Grow();
        height = Length::Px(72);
        // Posted: the action opens a dialog or replaces this page while the card handles its click.
        onClick = [this, a = std::move(action)] {
            if (Host* h = GetHost()) h->Post(a);
        };
    }
    std::wstring text;  // the second line: a count or what it is for

    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = IsHot();
        p.Fill(rect, c.bgCard, 12);
        if (hot) p.Fill(rect, IsPressed() ? c.press : c.hover, 12);
        const Rect ic{rect.x + 16, rect.Cy() - 20, 40, 40};
        p.Fill(ic, c.bgBase, 10);
        p.Icon(icon_, ic.Center(24, 24), hot ? c.textPrimary : c.textSecondary, c.accentMid);
        const float tx = ic.Right() + 14, tw = std::max(0.0f, rect.Right() - 44 - tx);
        p.Text(title_, {tx, rect.Cy() - 20, tw, 20}, TextStyle::BodyMedium, c.textPrimary);
        p.Text(text, {tx, rect.Cy(), tw, 20}, TextStyle::Small, c.textTertiary);
        p.Icon("chevron_right", {rect.Right() - 36, rect.Cy() - 10, 20, 20}, hot ? c.textPrimary : c.textTertiary);
        PaintFocusRing(p, rect, 12);
        // A line cut in a narrow window shows whole on hover.
        auto& t = gfx::Text::Get();
        const bool cut = t.Measure(title_, TextStyle::BodyMedium).width > tw + 0.5f || t.Measure(text, TextStyle::Small).width > tw + 0.5f;
        tooltip = cut ? title_ + L"\n" + text : std::wstring();
    }

private:
    const char* icon_;
    std::wstring title_;
};

class NetworkPage : public Page {
public:
    NetworkPage(Route r, bool compact) : Page(r, compact, "network") {
        group_ = route_.path;
        if (!group_.empty() && group_ != L"radio" && group_ != L"podcasts" && group_ != L"saved") group_.clear();
        list_ = ListModeFor(key_, true);
        queueName_ = group_.empty() ? std::wstring(Tr(L"Network")) : std::wstring(Tr(GroupTitle(group_)));
        if (group_.empty() && !compact) {
            toolbar_->gap = 16;
            auto* open = toolbar_->Add<HubCard>("add", Tr(L"Add Network Stream"), [] { ShowNetworkStreamDialog(); });
            open->text = Tr(L"A link to a video or radio");
            radio_ = toolbar_->Add<HubCard>("streaming", Tr(L"Radio"), [] { TheApp().Navigate(Route{Section::Network, L"radio"}); });
            podcasts_ = toolbar_->Add<HubCard>("audio", Tr(L"Podcasts"),
                                               [] { TheApp().Navigate(Route{Section::Network, L"podcasts"}); });
            toolbar_->visible = true;
        }
        Refresh();
    }

    std::vector<Crumb> Breadcrumbs() const override {
        auto c = Page::Breadcrumbs();
        if (!group_.empty()) c.push_back({Tr(GroupTitle(group_)), route_});
        return c;
    }

    void OnLayout() override {
        // The cards need about 290 DIP each for their names; a narrower page stacks them.
        if (radio_) {
            const bool row = rect.w - content_->pad.H() >= 3 * 290 + 2 * 16;
            toolbar_->dir = row ? Dir::Row : Dir::Column;
            toolbar_->gap = row ? 16.0f : 8.0f;
        }
        Page::OnLayout();
    }

    std::vector<MenuItem> MoreMenu() override {
        std::vector<MenuItem> m;
        m.push_back(Item(L"Open Network Stream…", "streaming", [] { ShowNetworkStreamDialog(); }));
        if (!group_.empty()) {
            m.push_back(Item(AddLabel(), "add", [this] { PromptAdd(); }));
            if (group_ == L"radio")
                m.push_back(Item(L"Restore Default Stations", "refresh", [] {
                    TheApp().SetUiValue("network.radio", "\x01");
                    TheApp().libraryChanged.Emit();
                }));
        }
        return m;
    }

protected:
    GridStyle CardStyle() const override { return GridStyle::SquareCards; }
    unsigned RowColumns() const override { return kColSubtitle | kColDate | kColTime; }

    void Build() override {
        if (group_.empty()) {
            BuildRoot();
            return;
        }
        Entry add;
        add.kind = EntryKind::Action;
        add.title = Tr(AddLabel());
        add.icon = "add";
        AddAction(std::move(add), [this] { PromptAdd(); });
        AddStreams(WideToUtf8(group_));
    }

    void FillEmpty(EmptyState& e) override {
        if (group_.empty() && !compact_)
            e.Set("streaming", Tr(L"Your streams will appear here"),
                  Tr(L"Open a link to a video, a live stream or a radio station, for example "
                     L"https://example.com/live.m3u8. Links you open or save stay here."));
        else if (group_ == L"podcasts")
            e.Set("audio", Tr(L"No podcast episodes yet"),
                  Tr(L"Paste a link to an episode (MP3, M4A or a video URL) to keep it here."),
                  {{Tr(L"Add episode link"), ButtonKind::Primary, "add", [this] { PromptAdd(); }}});
        else if (group_ == L"radio")
            e.Set("streaming", Tr(L"No stations"), Tr(L"Add the stream address of an internet radio station."),
                  {{Tr(L"Add station"), ButtonKind::Primary, "add", [this] { PromptAdd(); }}});
        else
            e.Set("streaming", Tr(L"No saved streams"), Tr(L"Open a network stream and save it to find it here."),
                  {{Tr(L"Add stream"), ButtonKind::Primary, "add", [this] { PromptAdd(); }}});
    }

    void ExtraMenu(int index, std::vector<MenuItem>& m) override {
        const Entry* e = EntryAt(index);
        if (!e || e->kind != EntryKind::Media) return;
        const std::wstring url = e->path, title = e->title;
        const std::string group = e->key;
        // Streams have no file to show; drop the file-only commands MediaMenu added.
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Copy Link", "link", [url] { CopyText(url); }));
        if (group == "history") {
            m.push_back(Item(L"Save to Saved Streams", "save", [url, title] {
                auto list = SavedStreams("saved");
                for (const auto& s : list)
                    if (s.url == url) return;
                list.push_back({url, title});
                SetSavedStreams("saved", list);
                Toast(Tr(L"Saved"), "save");
                TheApp().libraryChanged.Emit();
            }));
            m.push_back(Item(L"Remove from History", "remove", [url] { RemoveFromHistory(url); }));
        } else {
            m.push_back(Item(L"Rename…", "edit", [url, title, group] {
                ShowTextPrompt(Tr(L"Rename"), Tr(L"Name"), title, Tr(L"Rename"), [url, group](const std::wstring& name) {
                    auto list = SavedStreams(group);
                    for (auto& s : list)
                        if (s.url == url) s.title = name;
                    SetSavedStreams(group, list);
                    TheApp().libraryChanged.Emit();
                });
            }));
            m.push_back(Item(L"Remove", "remove", [url, group] { RemoveStream(group, url); }));
        }
    }

    void Delete(int index) override {
        const Entry* e = EntryAt(index);
        if (!e || e->kind != EntryKind::Media) return;
        if (e->key == "history") {
            RemoveFromHistory(e->path);
        } else {
            // A saved station or link is gone for good (no undo): ask, as for playlists.
            const std::string group = e->key;
            const std::wstring url = e->path;
            ShowConfirm(Tr(group == "radio" ? L"Remove station" : L"Remove link"),
                        Fmt(Tr(L"“{0}” will be removed from the list."), {e->title.empty() ? url : e->title}),
                        Tr(L"Remove"), [group, url] { RemoveStream(group, url); });
        }
    }

private:
    const wchar_t* AddLabel() const {
        if (group_ == L"radio") return L"Add station…";
        if (group_ == L"podcasts") return L"Add episode link…";
        return L"Add stream…";
    }

    static void RemoveStream(const std::string& group, const std::wstring& url) {
        auto list = SavedStreams(group);
        list.erase(std::remove_if(list.begin(), list.end(), [&](const SavedStream& s) { return s.url == url; }), list.end());
        SetSavedStreams(group, list);
        TheApp().libraryChanged.Emit();
    }

    void PromptAdd() {
        const std::string group = WideToUtf8(group_.empty() ? std::wstring(L"saved") : group_);
        ShowTextPrompt(Tr(AddLabel()), Tr(L"Network URL"), L"", Tr(L"Add"), [group](const std::wstring& url) {
            if (url.find(L"://") == std::wstring::npos) {
                Toast(Tr(L"Enter a full address, like https://…"), "info");
                return;
            }
            auto list = SavedStreams(group);
            for (const auto& s : list)
                if (s.url == url) return;
            list.push_back({url, TitleFromUrl(url)});
            SetSavedStreams(group, list);
            TheApp().libraryChanged.Emit();
        }, L"https://");
    }

    void AddStream(const std::wstring& url, const std::wstring& title, const std::string& group, int64_t date) {
        if (!Matches(title + L" " + url)) return;
        Entry e;
        e.kind = EntryKind::Media;
        e.audio = true;
        e.path = url;
        e.title = title.empty() ? TitleFromUrl(url) : title;
        e.subtitle = HostOf(url);
        e.icon = group == "radio" ? "streaming" : group == "podcasts" ? "audio" : "link";
        e.key = group;
        e.date = date;
        e.favorite = Favorite(url);
        e.ref = static_cast<int>(queue_.size());
        queue_.push_back(MakeQueueItem(url, e.title));
        Push(std::move(e));
    }

    void AddStreams(const std::string& group) {
        for (const SavedStream& s : SavedStreams(group)) AddStream(s.url, s.title, group, 0);
    }

    void BuildRoot() {
        const long long stations = static_cast<long long>(SavedStreams("radio").size());
        const long long episodes = static_cast<long long>(SavedStreams("podcasts").size());
        if (radio_) {
            // The page leads with its cards; the rows below are the links themselves.
            radio_->text = stations > 0 ? CountStations(stations) : std::wstring(Tr(L"Internet radio stations"));
            podcasts_->text = episodes > 0 ? CountEpisodes(episodes) : std::wstring(Tr(L"Links to episodes you keep"));
            radio_->Invalidate();
            podcasts_->Invalidate();
        } else {
            Entry add;
            add.kind = EntryKind::Action;
            add.title = Tr(L"Add Network Stream");
            add.icon = "add";
            AddAction(std::move(add), [] { ShowNetworkStreamDialog(); });
            auto cat = [&](const wchar_t* title, const char* icon, const wchar_t* path, long long n, bool radio) {
                Entry e;
                e.kind = EntryKind::Category;
                e.title = Tr(title);
                e.icon = icon;
                e.subtitle = radio ? CountStations(n) : CountEpisodes(n);
                e.count = static_cast<int>(n);
                AddRoute(std::move(e), Route{Section::Network, path});
            };
            cat(L"Podcasts", "audio", L"podcasts", episodes, false);
            cat(L"Radio", "streaming", L"radio", stations, true);
        }
        // The group counts say what the search left, as the rows below them do.
        const auto saved = SavedStreams("saved");
        const auto shown =
            std::count_if(saved.begin(), saved.end(), [this](const SavedStream& s) { return Matches(s.title + L" " + s.url); });
        std::vector<const library::HistoryEntry*> recent;
        for (const library::HistoryEntry& h : TheApp().history().Entries())
            if (IsUrl(h.path) && Matches(h.title + L" " + h.path)) recent.push_back(&h);
        // A single group's count would repeat the page header's (the cards aren't counted there).
        const bool both = radio_ && shown > 0 && !recent.empty();
        auto count = [&](size_t n) { return both || !radio_ ? CountItems(static_cast<long long>(n)) : std::wstring(); };
        if (shown > 0) {
            AddHeader(Tr(L"Saved Streams"), count(static_cast<size_t>(shown)));
            AddStreams("saved");
        }
        if (!recent.empty()) {
            AddHeader(Tr(L"Recently Played"), count(recent.size()));
            for (const library::HistoryEntry* h : recent) AddStream(h->path, h->title, "history", h->lastPlayed);
        }
    }

    std::wstring group_;
    HubCard* radio_ = nullptr;     // the cards of the page's top level (not in the side panel)
    HubCard* podcasts_ = nullptr;
};

}  // namespace

std::unique_ptr<LibraryView> MakeNetworkPage(const Route& route, bool compact) {
    return std::make_unique<NetworkPage>(route, compact);
}

}  // namespace app::lib
