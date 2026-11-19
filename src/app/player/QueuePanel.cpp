// Queue / Bookmarks / Chapters tabs of the list popover, and the mini player queue.
#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/LiveState.h"
#include "app/player/NowPlaying.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "app/player/PlayerView.h"
#include "gfx/Text.h"
#include "library/MediaTypes.h"

#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>

namespace app::panel {

using namespace ui;

std::vector<std::wstring> PickFiles(int kind);  // TracksPanel.cpp

namespace {

const PlayerState& PS() {
    return TheApp().player().State();
}

// Lengths of queue files that neither the library nor the history knows (a folder opened from
// Explorer, files dropped on the window), read once per path from the Windows property store
// on a worker thread, as the library scan does. Never destroyed: the worker may still run at exit.
class DurationProbe {
public:
    static DurationProbe& Get() {
        static DurationProbe* probe = new DurationProbe();
        return *probe;
    }
    // The length in seconds, or 0 while it is unknown (the first call queues the file).
    double Lookup(const std::wstring& path, HWND notify) {
        if (path.empty() || path.find(L"://") != std::wstring::npos) return 0;
        std::lock_guard lock(mu_);
        if (const auto it = known_.find(path); it != known_.end()) return std::max(0.0, it->second);
        known_.emplace(path, -1.0);  // queued; -1 also means "Windows doesn't know"
        todo_.push_back(path);
        notify_ = notify;
        if (!running_) {
            running_ = true;
            std::thread([this] { Run(); }).detach();
        }
        return 0;
    }

private:
    void Run() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::wstring path;
            {
                std::lock_guard lock(mu_);
                if (todo_.empty()) {
                    running_ = false;
                    break;
                }
                path = std::move(todo_.front());
                todo_.pop_front();
            }
            const double d = Read(path);
            HWND notify = nullptr;
            {
                std::lock_guard lock(mu_);
                known_[path] = d > 0 ? d : -1.0;
                // One repaint per batch: at the end, or every 16 files of a long queue.
                if (d > 0 && (todo_.empty() || ++sinceRepaint_ >= 16)) {
                    sinceRepaint_ = 0;
                    notify = notify_;
                }
            }
            if (notify) PostMessageW(notify, Host::kRepaintMessage, 0, 0);
        }
        if (SUCCEEDED(co)) CoUninitialize();
    }
    static double Read(const std::wstring& path) {
        // A cloud placeholder would be downloaded just to read its length.
        const DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN)))
            return 0;
        IPropertyStore* store = nullptr;
        if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))))
            return 0;
        double d = 0;
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(store->GetValue(PKEY_Media_Duration, &v)) && v.vt == VT_UI8)
            d = static_cast<double>(v.uhVal.QuadPart) / 1e7;  // 100 ns units
        PropVariantClear(&v);
        store->Release();
        return d;
    }
    std::mutex mu_;
    std::unordered_map<std::wstring, double> known_;
    std::deque<std::wstring> todo_;
    HWND notify_ = nullptr;
    int sinceRepaint_ = 0;
    bool running_ = false;
};

std::wstring CurrentPath() {
    const QueueItem* item = TheApp().queue().CurrentItem();
    if (item) return item->path;
    return PS().path;
}

// "4:20" (or "1:02:03"), the same m:ss as the library lists.
std::wstring ClockTime(double s) {
    const long long t = static_cast<long long>(std::max(0.0, s));
    wchar_t buf[32];
    if (t >= 3600)
        swprintf_s(buf, L"%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    else
        swprintf_s(buf, L"%lld:%02lld", t / 60, t % 60);
    return buf;
}

// "00:00:42:023" as in the bookmarks list.
std::wstring PreciseTime(double s) {
    const long long ms = std::llround(std::max(0.0, s) * 1000);
    wchar_t buf[40];
    swprintf_s(buf, L"%02lld:%02lld:%02lld:%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    return buf;
}

std::wstring BookmarkName(const library::Bookmark& b, size_t index) {
    return b.name.empty() ? Fmt(Tr(L"Bookmark {0}"), static_cast<long long>(index + 1)) : b.name;
}

std::wstring CountBookmarks(size_t n) {
    const long long c = static_cast<long long>(n);
    if (CurrentLanguage() == Language::Russian)
        return std::to_wstring(c) + L" " + Plural(c, L"закладка", L"закладки", L"закладок");
    return std::to_wstring(c) + (c == 1 ? L" Bookmark" : L" Bookmarks");
}

// The hovered row's full text when its painted text was cut (empty otherwise), set while that row
// paints: the host shows it after the usual delay and re-arms when it changes, as in the library
// grids. Assigned only on a change, so a row repainting doesn't copy the string each time.
void RowTip(Widget& list, bool cut, const std::wstring& text) {
    if (!cut) list.tooltip.clear();
    else if (list.tooltip != text) list.tooltip = text;
}

bool TextCut(Painter& p, const std::wstring& text, TextStyle style, float w) {
    return !text.empty() && (w <= 0 || p.Measure(text, style).width > w + 0.5f);
}

ScrollView* ScrollerOf(Widget* w) {
    for (Widget* p = w->Parent(); p; p = p->Parent())
        if (auto* s = dynamic_cast<ScrollView*>(p)) return s;
    return nullptr;
}

void SaveQueueAsPlaylist();
std::vector<MenuItem> AddToQueueItems();

// Small icon button for list headers (20 DIP icons, as in the settings rows, in a 32×32 hit box).
IconButton* HeaderButton(Widget* parent, const char* icon, const wchar_t* tip, std::function<void()> click) {
    auto* b = parent->Add<IconButton>(icon, Tr(tip), 32, 20);
    b->width = Length::Px(32);
    b->height = Length::Px(32);
    b->onClick = std::move(click);
    return b;
}

// Summary line + buttons; the summary starts at the rows' text edge.
class ListHeader : public Widget {
public:
    ListHeader(std::function<std::wstring()> text, bool compact) : text_(std::move(text)) {
        dir = Dir::Row;
        align = Align::Center;
        width = Length::Grow();
        height = Length::Px(compact ? 40.0f : 52.0f);
        pad = compact ? Insets{20, 0, 14, 0} : Insets{20, 0, 12, 0};
        gap = 2.0f;
        label_ = Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        label_->width = Length::Grow();
        Refresh();
    }
    void Refresh() {
        label_->SetText(text_());
    }

private:
    std::function<std::wstring()> text_;
    Label* label_;
};

// --- queue ------------------------------------------------------------------------------------

class QueueList : public Widget {
public:
    // covers: the queue pinned beside the music view, two-line rows with the cover art.
    QueueList(bool compact, bool covers) : compact_(compact), covers_(covers) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        id = "queue-list";  // Panels.cpp gives it the keyboard when the queue opens
        pad = compact ? Insets{12, 0, 12, 12} : Insets{8, 4, 8, 8};
        queueConn_ = Connect(TheApp().queue().changed, [this] {
            // The rows changed under a drag (a removal, a new queue): its indices mean nothing now.
            dragging_ = false;
            downIndex_ = -1;
            FollowCurrent();
            selected_ = std::min(selected_, static_cast<int>(TheApp().queue().Items().size()) - 1);
            rows_.clear();
            InvalidateLayout();
        });
        shownCurrent_ = TheApp().queue().Current();
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & kChangeFile) FollowCurrent();
            if (m & (kChangeFile | kChangeMeta)) rows_.clear();
            if (m & (kChangeFile | kChangeMeta | kChangeState)) Invalidate();
        });
        libConn_ = Connect(TheApp().libraryChanged, [this] {
            rows_.clear();
            Invalidate();
        });
        onContextMenu = [this](const MouseEvent& e) { ShowMenuAt(e); };
        if (covers_) {
            pad = {12, 0, 12, 20};
            imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
        }
    }

    // A selection on the playing row moves with it (the next song, a row inserted or removed
    // above): left behind, its edge marked a second row as if it were playing.
    void FollowCurrent() {
        const int cur = TheApp().queue().Current();
        if (selected_ >= 0 && selected_ == shownCurrent_) selected_ = cur;
        shownCurrent_ = cur;
    }

    float Pitch() const { return covers_ ? 60.0f : compact_ ? 31.0f : 44.0f; }
    float RowH() const { return covers_ ? 54.0f : compact_ ? 26.0f : 40.0f; }
    int Count() const { return static_cast<int>(TheApp().queue().Items().size()); }
    Rect RowRect(int i) const {
        return {rect.x + pad.l, rect.y + pad.t + Pitch() * static_cast<float>(i), rect.w - pad.H(), RowH()};
    }
    int RowAt(float y) const {
        const int i = static_cast<int>(std::floor((y - rect.y - pad.t) / Pitch()));
        return i >= 0 && i < Count() ? i : -1;
    }

    Size MeasureContent(float availW, float) override {
        const int n = Count();
        if (n == 0) return {availW, 120};
        return {availW, pad.V() + Pitch() * static_cast<float>(n) - (Pitch() - RowH())};
    }

    void OnLayout() override {
        if (!scrolled_ && Count() > 0) {
            scrolled_ = true;
            const int cur = TheApp().queue().Current();
            if (cur >= 0)
                if (Host* h = GetHost())
                    h->Post([this, cur, alive = std::weak_ptr<bool>(alive_)] {
                        if (alive.expired()) return;
                        if (ScrollView* sv = ScrollerOf(this)) sv->ScrollIntoView(RowRect(cur));
                    });
        }
    }

    // The player's state describes this row only once mpv has this very file open (right after
    // a move on it still holds the previous one's title and duration).
    bool Playing(int i) const {
        return i == TheApp().queue().Current() && PS().fileLoaded &&
               PS().path == TheApp().queue().Items()[static_cast<size_t>(i)].path;
    }

    // Title, artist and duration of each row, looked up in the library and the history once (a
    // history lookup scans up to 500 entries) and kept until the queue, the library or the
    // playing file's tags change.
    struct RowInfo {
        bool known = false;
        std::wstring title, artist;
        double duration = 0;
    };
    const RowInfo& Info(int i) {
        const size_t n = TheApp().queue().Items().size();
        if (rows_.size() != n) rows_.assign(n, RowInfo{});
        RowInfo& r = rows_[static_cast<size_t>(i)];
        if (!r.known) {
            r.known = true;
            r.title = TitleOf(i);
            r.artist = ArtistOf(i);
            r.duration = StoredDuration(i);
        }
        return r;
    }

    double DurationOf(int i) {
        if (Playing(i) && PS().duration > 0) return PS().duration;
        if (const double d = Info(i).duration; d > 0) return d;
        return DurationProbe::Get().Lookup(TheApp().queue().Items()[static_cast<size_t>(i)].path,
                                           GetHost() ? GetHost()->Hwnd() : nullptr);
    }

    double StoredDuration(int i) const {
        const QueueItem& item = TheApp().queue().Items()[static_cast<size_t>(i)];
        if (item.duration > 0) return item.duration;
        if (const library::MediaItem* m = LibraryItem(i); m && m->duration > 0) return m->duration;
        if (const library::HistoryEntry* h = TheApp().history().Find(item.path)) return h->duration;
        return 0;
    }

    const library::MediaItem* LibraryItem(int i) const {
        return TheApp().library().Current()->FindItem(TheApp().queue().Items()[static_cast<size_t>(i)].path);
    }

    std::wstring TitleOf(int i) const {
        const QueueItem& item = TheApp().queue().Items()[static_cast<size_t>(i)];
        // Folder queues store the file name as the title: tags are better when known.
        const std::wstring file = FileNameOf(item.path);
        if (!item.title.empty() && item.title != file) return item.title;
        if (const library::MediaItem* m = LibraryItem(i); m && !m->title.empty() && m->title != file) return m->title;
        // Tags seen when it played before (the playing file's land in item.title a second in, App.cpp).
        if (const library::HistoryEntry* h = TheApp().history().Find(item.path);
            h && !h->title.empty() && h->title != file && item.path.find(L"://") == std::wstring::npos)
            return h->title;
        // Like the title bar: the file name without its extension.
        const size_t dot = file.find_last_of(L'.');
        return dot != std::wstring::npos && dot > 0 && item.path.find(L"://") == std::wstring::npos ? file.substr(0, dot)
                                                                                                  : file;
    }

    // Artist of an audio item when its tags (or the playing file) know it.
    std::wstring ArtistOf(int i) const {
        if (Playing(i) && !PS().hasVideo) return CurrentTags().artist;
        if (const library::MediaItem* m = LibraryItem(i); m && m->kind == library::MediaKind::Audio) return m->artist;
        return {};
    }

    void PaintRow(Painter& p, int i, const Rect& r, bool floating) {
        const Palette& c = P();
        const bool current = i == TheApp().queue().Current();
        const bool hot = !dragging_ && i == hover_ && IsHot();
        const float radius = compact_ ? 4.0f : kRadiusCard;
        if (floating) {
            p.Shadow(r, radius, 10, 4, 0.35f);
            p.Fill(r, c.bgMenu, radius);
        }
        // The selection (what Enter plays and Del removes) shows once the user picks a row, by
        // click or keyboard, not for the row the list starts on; dimmer while the keyboard is elsewhere.
        const bool selected = !floating && i == selected_ && (picked_ || ShowFocusRing());
        if (selected) p.Fill(r, IsFocused() ? c.bgSelected : c.bgSelected.Fade(0.5f), radius);
        if (!floating && hot) p.Fill(r, c.hover, radius);
        if (selected && IsFocused() && ShowFocusRing()) PaintFocusRing(p, r, radius);
        if (covers_) {
            PaintCoverRow(p, i, r, floating, hot, current);
            return;
        }
        // The icon column of the settings rows: the playing row's mark, the drag handle under the
        // pointer, the position otherwise.
        const float box = compact_ ? RowH() : 44.0f;
        const Rect slot = compact_ ? Rect{r.x, r.y, box, r.h} : Rect{r.x + 2, r.y, 40, r.h};
        const bool handleHot = hot && hoverHandle_;
        if (floating || handleHot)
            p.Icon("move", slot.Center(compact_ ? 18.0f : 20.0f, compact_ ? 18.0f : 20.0f), c.textPrimary);
        else if (current)
            p.Icon(PS().paused ? "pause" : "play", slot.Center(compact_ ? 14.0f : 16.0f, compact_ ? 14.0f : 16.0f),
                   c.textPrimary);
        else
            p.Text(std::to_wstring(i + 1), slot, TextStyle::MonoSmall, c.textTertiary, HAlign::Center);
        const TextStyle ts = compact_ ? TextStyle::Small : TextStyle::Body;
        const float textX = r.x + box + (compact_ ? 8.0f : 0.0f);
        const double d = DurationOf(i);
        const std::wstring dur = d > 0 ? ClockTime(d) : L"";
        // Tabular figures, as in the library's lists: the lengths line up down the column.
        const TextStyle ds = compact_ ? TextStyle::MonoSmall : TextStyle::Mono;
        const float dw = dur.empty() ? 0.0f : gfx::Text::Get().Measure(dur, ds).width + 4;
        const bool showRemove = hot && !compact_ && !removeCold_;
        const float right = r.Right() - (showRemove ? 28.0f : 0.0f) - (compact_ ? 6.0f : 12.0f);
        const Color fg = current || hot ? c.textPrimary : RowText();
        const float avail = std::max(0.0f, right - textX - dw - 10);
        const RowInfo& info = Info(i);
        const std::wstring& title = info.title;
        const std::wstring& artist = Playing(i) && !PS().hasVideo ? CurrentTags().artist : info.artist;
        const float natural = gfx::Text::Get().Measure(title, ts).width + 2;
        const float tw = std::min(avail, natural);
        p.Text(title, {textX, r.y, artist.empty() ? avail : tw, r.h}, ts, fg);
        const bool artistShown = !artist.empty() && avail - tw > 40;
        if (artistShown)
            p.Text(L" \u00b7 " + artist, {textX + tw, r.y, avail - tw, r.h}, ts, current ? c.textSecondary : c.textTertiary);
        if (hot && !floating && !hoverHandle_ && !hoverRemove_) {
            const bool cut = natural > avail + 0.5f || (!artist.empty() && !artistShown) ||
                             (artistShown && TextCut(p, L" \u00b7 " + artist, ts, avail - tw));
            RowTip(*this, cut, artist.empty() ? title : title + L"\n" + artist);
        }
        if (showRemove) {
            const Rect x{r.Right() - 32, r.Cy() - 12, 24, 24};
            if (hoverRemove_) p.Fill(x, c.hover, 4);
            p.Icon("close", x.Center(18, 18), hoverRemove_ ? c.textPrimary : c.textTertiary);
        }
        if (!dur.empty())
            p.Text(dur, {right - dw - 4, r.y, dw + 4, r.h}, ds, current ? c.textSecondary : c.textTertiary, HAlign::Trailing);
    }

    // A row of the pinned queue: the cover (the drag handle), the title over the artist, the length.
    void PaintCoverRow(Painter& p, int i, const Rect& r, bool floating, bool hot, bool current) {
        const Palette& c = P();
        const QueueItem& item = TheApp().queue().Items()[static_cast<size_t>(i)];
        const Rect art{r.x + 5, r.y + 5, r.h - 10, r.h - 10};
        const int px = GetHost() ? static_cast<int>(art.w * GetHost()->Scale()) : 0;
        ImageCache& images = TheApp().images();
        ID2D1Bitmap* bmp = GetHost() ? images.Get(item.path, px) : nullptr;
        const float appear = images.Appear(item.path, px);
        if (!bmp || appear < 1) p.Fill(art, c.bgSelected, 4);
        if (bmp) {
            p.Image(bmp, art, 4, true, appear);
        } else if (!images.Loading(item.path, px)) {  // no icon while the cover is on its way
            p.Icon(library::KindOf(item.path) == library::MediaKind::Audio ? "music" : "video", art.Center(20, 20),
                   c.textTertiary.Fade(appear));
        }
        const bool handleHot = hot && hoverHandle_;
        if (handleHot || floating) {
            p.Fill(art, Color::Hex(0x000000).Fade(0.55f), 4);
            p.Icon("move", art.Center(22, 22), Color::Hex(0xFFFFFF));
        }
        const double d = DurationOf(i);
        const std::wstring dur = d > 0 ? ClockTime(d) : L"";
        const float dw = dur.empty() ? 0.0f : gfx::Text::Get().Measure(dur, TextStyle::MonoSmall).width + 4;
        const bool showRemove = hot && !removeCold_;
        const float right = r.Right() - (showRemove ? 28.0f : 0.0f) - 8;
        const float textX = art.Right() + 12;
        const float avail = std::max(0.0f, right - textX - dw - 8);
        const RowInfo& info = Info(i);
        const std::wstring& artist = Playing(i) && !PS().hasVideo ? CurrentTags().artist : info.artist;
        const std::wstring second = artist.empty() ? std::wstring(Tr(library::KindOf(item.path) == library::MediaKind::Audio
                                                                        ? L"Song"
                                                                        : L"Video"))
                                                   : artist;
        p.Text(info.title, {textX, r.y + 8, avail, 20}, TextStyle::Body, current ? c.accent : c.textPrimary, HAlign::Leading,
               VAlign::Center, gfx::kTextEllipsis);
        p.Text(second, {textX, r.y + 28, avail, 18}, TextStyle::Small, c.textTertiary, HAlign::Leading, VAlign::Center,
               gfx::kTextEllipsis);
        if (hot && !floating && !hoverHandle_ && !hoverRemove_)
            RowTip(*this, TextCut(p, info.title, TextStyle::Body, avail) || TextCut(p, second, TextStyle::Small, avail),
                   artist.empty() ? info.title : info.title + L"\n" + artist);
        if (showRemove) {
            const Rect x{r.Right() - 30, r.Cy() - 12, 24, 24};
            if (hoverRemove_) p.Fill(x, c.hover, 4);
            p.Icon("close", x.Center(18, 18), hoverRemove_ ? c.textPrimary : c.textTertiary);
        }
        if (!dur.empty())
            p.Text(dur, {right - dw, r.y, dw, r.h}, TextStyle::MonoSmall, c.textTertiary, HAlign::Trailing);
    }

    void Paint(Painter& p) override {
        const Palette& c = P();
        const int n = Count();
        if (!dragging_ && IsHot() && GetHost()) {
            const Point m = GetHost()->MousePos();
            const int i = RowAt(m.y);
            const int h = i >= 0 && RowRect(i).Contains(m.x, m.y) ? i : -1;
            if (h != hover_) tooltip.clear();  // scrolled under the pointer: the new row sets its own
            hover_ = h;
        }
        if (n == 0) {
            p.Text(Tr(L"Queue is empty"), {rect.x, rect.y + 24, rect.w, 24}, TextStyle::Body, c.textTertiary,
                   HAlign::Center);
            p.Text(Tr(L"Add files with the + button or drop them here."), {rect.x + 24, rect.y + 52, rect.w - 48, 40},
                   TextStyle::Small, c.textTertiary, HAlign::Center, VAlign::Top, gfx::kTextWrap);
            return;
        }
        const Rect clip = p.Clip();
        const int first = std::max(0, static_cast<int>((clip.y - rect.y - pad.t) / Pitch()) - 1);
        const int last = std::min(n - 1, static_cast<int>((clip.Bottom() - rect.y - pad.t) / Pitch()) + 1);
        for (int slot = first; slot <= last; ++slot) {
            // While dragging, rows between the source and the target shift by one slot.
            int i = slot;
            if (dragging_) {
                if (slot == dropIndex_) continue;
                if (dragFrom_ < dropIndex_ && slot >= dragFrom_ && slot < dropIndex_) i = slot + 1;
                else if (dragFrom_ > dropIndex_ && slot > dropIndex_ && slot <= dragFrom_) i = slot - 1;
            }
            if (i < 0 || i >= n) continue;
            PaintRow(p, i, RowRect(slot), false);
        }
    }

    void PaintOver(Painter& p) override {
        if (!dragging_ || dragFrom_ >= Count()) return;
        Rect r = RowRect(dragFrom_);
        r.y = std::clamp(dragY_ - grabDy_, rect.y + pad.t, RowRect(Count() - 1).y);
        PaintRow(p, dragFrom_, r, true);
    }

    bool OnMouseMove(const MouseEvent& e) override {
        if (dragging_) {
            dragY_ = e.y;
            const float center = dragY_ - grabDy_ + RowH() / 2;
            dropIndex_ = std::clamp(static_cast<int>((center - rect.y - pad.t) / Pitch()), 0, Count() - 1);
            AutoScroll(e.y);
            Invalidate();
            return true;
        }
        // After a removal the next row slides under the pointer: its "×" waits for a real move,
        // so the second click of a double click doesn't remove that row too.
        if (removeCold_ && (std::abs(e.x - removedAt_.x) >= 3 || std::abs(e.y - removedAt_.y) >= 3)) {
            removeCold_ = false;
            Invalidate();
        }
        const int i = RowAt(e.y);
        const Rect r = i >= 0 ? RowRect(i) : Rect{};
        const bool inRow = i >= 0 && r.Contains(e.x, e.y);
        const bool handle = inRow && e.x < r.x + RowH() + 4;
        const bool remove = inRow && OnRemoveButton(i, e.x);
        const int h = inRow ? i : -1;
        if (h != hover_ || handle != hoverHandle_ || remove != hoverRemove_) {
            hover_ = h;
            hoverHandle_ = handle;
            hoverRemove_ = remove;
            cursor = handle ? IDC_SIZENS : IDC_ARROW;
            tooltip = remove ? Tr(L"Remove from queue") : handle ? Tr(L"Drag to reorder") : L"";
            Invalidate();
        }
        return true;
    }

    void OnMouseLeave() override {
        if (dragging_) return;
        hover_ = -1;
        hoverHandle_ = hoverRemove_ = false;
        tooltip.clear();
        Invalidate();
    }

    bool OnMouseDown(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        downIndex_ = -1;
        if (i < 0 || !RowRect(i).Contains(e.x, e.y)) return true;
        selected_ = i;
        picked_ = true;
        if (e.x < RowRect(i).x + RowH() + 4) {
            dragging_ = true;
            dragFrom_ = dropIndex_ = i;
            dragY_ = e.y;
            grabDy_ = e.y - RowRect(i).y;
        } else {
            downIndex_ = i;
        }
        Invalidate();
        return true;
    }

    bool OnMouseUp(const MouseEvent& e) override {
        if (dragging_) {
            dragging_ = false;
            // A lost capture (Alt+Tab, a dialog) cancels the drag, as in Explorer.
            if (e.cancelled) {
                selected_ = dragFrom_;
                Invalidate();
                return true;
            }
            const int from = dragFrom_, to = dropIndex_;
            selected_ = to;
            if (from != to) TheApp().queue().Move(from, to);
            Invalidate();
            return true;
        }
        // A single click only selects (OnMouseDown); the "×" acts on release.
        const int i = RowAt(e.y);
        if (!e.cancelled && i >= 0 && i == downIndex_ && OnRemoveButton(i, e.x)) {
            Remove(i);
            removeCold_ = true;
            removedAt_ = {e.x, e.y};
        }
        downIndex_ = -1;
        return true;
    }

    // A double click (or Enter) plays the row, as in VLC, MPC-HC and PotPlayer.
    bool OnDoubleClick(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        downIndex_ = -1;
        if (i < 0 || !RowRect(i).Contains(e.x, e.y)) return true;
        if (removeCold_ || OnRemoveButton(i, e.x) || e.x < RowRect(i).x + RowH() + 4) return true;
        selected_ = i;
        Play(i);
        Invalidate();
        return true;
    }

    bool OnKeyDown(const KeyEvent& e) override {
        const bool listKey = e.key == VK_UP || e.key == VK_DOWN || e.key == VK_HOME || e.key == VK_END ||
                             e.key == VK_RETURN || e.key == VK_DELETE;
        // Mid-drag the rows are in flux, and an empty list has nothing to move to; the arrows
        // still stay here instead of changing the volume.
        if (dragging_) return listKey;
        const int n = Count();
        if (n == 0) return listKey && e.key != VK_RETURN;
        if (selected_ < 0) selected_ = std::max(0, TheApp().queue().Current());
        if (listKey) picked_ = true;
        const bool move = e.mods.alt || e.mods.ctrl;
        int target = selected_;
        switch (e.key) {
            case VK_UP: target = std::max(0, selected_ - 1); break;
            case VK_DOWN: target = std::min(n - 1, selected_ + 1); break;
            case VK_HOME: target = 0; break;
            case VK_END: target = n - 1; break;
            case VK_RETURN: Play(selected_); return true;
            case VK_DELETE: Remove(selected_); return true;
            default: return false;
        }
        if (move && target != selected_) TheApp().queue().Move(selected_, target);
        selected_ = target;
        if (ScrollView* sv = ScrollerOf(this)) sv->ScrollIntoView(RowRect(selected_));
        Invalidate();
        return true;
    }

    void Remove(int i) {
        if (i < 0 || i >= Count()) return;
        const QueueItem item = TheApp().queue().Items()[static_cast<size_t>(i)];
        const std::wstring title = Info(i).title;
        TheApp().RemoveFromQueue(i);
        hover_ = -1;
        // Back where it was, without playing it again (a removed playing row stopped or moved on).
        ShowToast(TheApp().main(), Fmt(Tr(L"“{0}” removed from the queue"), {title}), "remove", 5000, Tr(L"Undo"),
                  [item, i] {
                      PlayQueue& q = TheApp().queue();
                      q.Add({item});
                      const int last = static_cast<int>(q.Items().size()) - 1;
                      q.Move(last, std::min(i, last));
                  });
    }

    // The playing row resumes if paused; another row starts playing.
    static void Play(int i) {
        if (i == TheApp().queue().Current() && PS().fileLoaded) {
            if (PS().paused) TheApp().player().SetPause(false);
        } else {
            TheApp().PlayIndex(i);
        }
    }

    // The keyboard starts on the playing row when nothing is selected yet.
    void OnFocusChanged(bool focused) override {
        if (focused && selected_ < 0 && Count() > 0) selected_ = std::max(0, TheApp().queue().Current());
        Invalidate();
    }

    int Selected() const { return selected_; }

    // For screen readers: "title, artist, 4:20", the playing row says so.
    void Describe(A11y& a) override {
        a.role = Role::List;
        a.name = Count() > 0 ? Tr(L"Queue") : Tr(L"Queue is empty");  // the painted hint, not "0 items"
        a.items = Count();
        a.item = [this](int i, A11y::Item& it) {
            const RowInfo& info = Info(i);
            it.name = info.title;
            if (!info.artist.empty()) it.name += L", " + info.artist;
            // Only what is known: probing every file for a reader would read the whole queue.
            const double d = Playing(i) && PS().duration > 0 ? PS().duration : info.duration;
            if (d > 0) it.name += L", " + ClockTime(d);
            if (i == TheApp().queue().Current()) it.name += L", " + std::wstring(Tr(L"Now playing"));
            it.rect = RowRect(i);
            it.selected = i == selected_;
        };
        a.selectItem = [this](int i) {
            selected_ = i;
            Invalidate();
        };
        a.invokeItem = [this](int i) {
            selected_ = i;
            Play(i);
            Invalidate();
        };
        a.focusedItem = selected_;
    }

private:
    bool OnRemoveButton(int i, float x) const {
        return !compact_ && !removeCold_ && x >= RowRect(i).Right() - (covers_ ? 32 : 36);
    }

    void AutoScroll(float y) {
        ScrollView* sv = ScrollerOf(this);
        if (!sv) return;
        if (y < sv->rect.y + 24) sv->ScrollTo(sv->Offset() - 12, false);
        else if (y > sv->rect.Bottom() - 24) sv->ScrollTo(sv->Offset() + 12, false);
    }

    // The Menu key: the selected (or playing) row's menu; the whole queue's when it is empty.
    bool MenuPoint(Point& at) const override {
        const int i = selected_ >= 0 ? selected_ : TheApp().queue().Current();
        if (i < 0 || i >= Count()) return Widget::MenuPoint(at);
        const Rect r = RowRect(i);
        at = {r.x + std::min(r.w / 2, 120.0f), r.Cy()};
        return true;
    }

    void ShowMenuAt(const MouseEvent& e) {
        const int i = RowAt(e.y);
        if (i < 0) {
            // The empty part of the list: what concerns the whole queue.
            std::vector<MenuItem> items;
            items.push_back({Tr(L"Save as playlist…"), L"", "save", false, Count() > 0, false, false,
                             [] { SaveQueueAsPlaylist(); }});
            items.push_back(MenuItem::Separator());
            for (MenuItem& m : AddToQueueItems()) items.push_back(std::move(m));
            ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
            return;
        }
        selected_ = i;
        std::vector<MenuItem> items;
        items.push_back({Tr(L"Play"), L"", "play", false, true, false, false, [i] { TheApp().PlayIndex(i); }});
        items.push_back({Tr(L"Move up"), L"", "arrow_up", false, i > 0, false, false,
                         [i] { TheApp().queue().Move(i, i - 1); }});
        items.push_back({Tr(L"Move down"), L"", "arrow_down", false, i + 1 < Count(), false, false,
                         [i] { TheApp().queue().Move(i, i + 1); }});
        items.push_back(MenuItem::Separator());
        items.push_back({Tr(L"Remove from queue"), L"Del", "remove", false, true, false, false,
                         [this, i] { Remove(i); }});
        ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
    }

    bool compact_;
    bool covers_ = false;
    ScopedConnection imagesConn_;
    int hover_ = -1, selected_ = -1, downIndex_ = -1;
    int shownCurrent_ = -1;  // the playing row when the selection last looked (FollowCurrent)
    bool hoverHandle_ = false, hoverRemove_ = false;
    bool removeCold_ = false;  // the "×" stays off until the pointer leaves where it removed a row
    Point removedAt_{};
    std::vector<RowInfo> rows_;
    bool dragging_ = false;
    int dragFrom_ = 0, dropIndex_ = 0;
    float dragY_ = 0, grabDy_ = 0;
    bool scrolled_ = false;
    bool picked_ = false;  // the user chose a row: the selection shows
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    ScopedConnection queueConn_, playerConn_, libConn_;
};

std::wstring QueueSummary() {
    const PlayQueue& q = TheApp().queue();
    const auto& items = q.Items();
    double total = 0;
    bool complete = true;  // a partial total would be misleading
    auto lib = TheApp().library().Current();
    for (size_t i = 0; i < items.size(); ++i) {
        double d = items[i].duration;
        if (static_cast<int>(i) == q.Current() && PS().duration > 0) d = PS().duration;
        if (d <= 0)
            if (const library::MediaItem* m = lib->FindItem(items[i].path)) d = m->duration;
        if (d <= 0) complete = false;
        total += d;
    }
    std::wstring s = CountItems(static_cast<long long>(items.size()));
    if (complete && total > 0) s += L" \u00b7 " + ClockTime(total);
    return s;
}

void SortQueue(int mode) {
    PlayQueue& q = TheApp().queue();
    std::vector<QueueItem> items = q.Items();
    if (items.size() < 2) return;
    const std::wstring current = q.CurrentItem() ? q.CurrentItem()->path : L"";
    std::vector<size_t> order(items.size());
    std::iota(order.begin(), order.end(), size_t{0});
    auto name = [&](size_t i) { return items[i].title.empty() ? FileNameOf(items[i].path) : items[i].title; };
    if (mode == 2) {
        std::reverse(order.begin(), order.end());
    } else if (mode >= 3) {
        // 3: newest first, 4: oldest first, 5: largest first. URLs and missing files go last.
        std::vector<uint64_t> v(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            WIN32_FILE_ATTRIBUTE_DATA d;
            if (!GetFileAttributesExW(items[i].path.c_str(), GetFileExInfoStandard, &d)) continue;
            const FILETIME t = d.ftLastWriteTime;
            v[i] = mode == 5 ? (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow
                             : (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if ((v[a] == 0) != (v[b] == 0)) return v[b] == 0;
            return mode == 4 ? v[a] < v[b] : v[a] > v[b];
        });
    } else {
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const int c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, SORT_DIGITSASNUMBERS | NORM_IGNORECASE,
                                          name(a).c_str(), -1, name(b).c_str(), -1, nullptr, nullptr, 0);
            return mode == 0 ? c == CSTR_LESS_THAN : c == CSTR_GREATER_THAN;
        });
    }
    std::vector<QueueItem> sorted;
    int cur = -1;
    for (size_t i : order) {
        if (items[i].path == current && cur < 0) cur = static_cast<int>(sorted.size());
        sorted.push_back(items[i]);
    }
    q.Set(std::move(sorted), cur, q.Name());
    q.reordered.Emit();
}

void AddFilesToQueue();

// "+" and the empty list's menu: the library (pick there, "Add to queue") or files from disk.
std::vector<MenuItem> AddToQueueItems() {
    std::vector<MenuItem> items;
    items.push_back({Tr(L"From the library…"), L"", "songs", false, true, false, false, [] {
        const bool song = library::KindOf(CurrentPath()) == library::MediaKind::Audio;
        TheApp().ShowPlayer(false);
        TheApp().Navigate(song ? Route{Section::Music, L"songs"} : Route{Section::Videos, {}});
        ShowToast(TheApp().main(), Tr(L"Right-click what to add › Add to Queue"), "queue", 5000);
    }});
    items.push_back({Tr(L"Files…"), L"", "file", false, true, false, false, [] { AddFilesToQueue(); }});
    return items;
}

void SaveQueueAsPlaylist() {
    // The dialog starts with the queue ticked; the new list shows on the Playlists page.
    ShowNewPlaylistDialog([](const std::string& id) {
        if (!id.empty()) TheApp().libraryChanged.Emit();
    });
}

void AddFilesToQueue() {
    std::vector<QueueItem> add;
    for (const std::wstring& f : PickFiles(0)) add.push_back({f, FileNameOf(f), 0});
    if (add.empty()) return;
    const bool wasEmpty = TheApp().queue().Items().empty();
    TheApp().queue().Add(std::move(add));
    if (wasEmpty || !PS().fileLoaded) TheApp().PlayIndex(TheApp().queue().Current() < 0 ? 0 : TheApp().queue().Current());
}

// Repeat button with a small "1" for repeat-one (the control bar).
class RepeatButton : public IconButton {
public:
    RepeatButton() : IconButton("repeat_right", Tr(L"Repeat")) {
        onClick = [] { Run(Action::ToggleRepeat); };
        conn_ = Connect(TheApp().queue().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        const Repeat r = TheApp().queue().RepeatMode();
        active = r != Repeat::Off;
        tooltip = WithKey(r == Repeat::Off ? Tr(L"Repeat off") : r == Repeat::All ? Tr(L"Repeat all") : Tr(L"Repeat one"),
                          Action::ToggleRepeat);
        IconButton::Paint(p);
        if (r == Repeat::One) {
            const Rect b{rect.Right() - 11, rect.y + 2, 10, 10};
            p.Fill(b, P().accent, 5);
            p.Text(L"1", b, TextStyle::Micro, P().onAccent, HAlign::Center);
        }
    }
    void Describe(A11y& a) override { a.toggle = TheApp().queue().RepeatMode() != Repeat::Off ? 1 : 0; }

private:
    ScopedConnection conn_;
};

// Everything except what is playing, in one change (one Remove per row was O(n²) and froze a
// 5000-item queue for seconds), with an Undo.
void ClearQueue() {
    PlayQueue& q = TheApp().queue();
    if (q.Items().size() <= (q.CurrentItem() ? 1u : 0u)) return;  // nothing to clear
    const std::vector<QueueItem> before = q.Items();
    const int cur = q.Current();
    const std::wstring name = q.Name();
    if (const QueueItem* item = q.CurrentItem()) q.Set({*item}, 0, name);
    else q.Clear();
    ShowToast(TheApp().main(), Tr(L"Queue cleared"), "remove", 5000, Tr(L"Undo"), [before, cur, name] {
        // Only onto what the clear left: the playing item alone, or nothing.
        PlayQueue& now = TheApp().queue();
        const auto& left = now.Items();
        const bool same =
            cur >= 0 ? left.size() == 1 && left[0].path == before[static_cast<size_t>(cur)].path : left.empty();
        if (same) now.Set(before, std::max(cur, 0), name);
    });
}

// Column: header + scrolling list. Keeps the header text in sync.
class QueueTab : public Widget {
public:
    QueueTab(bool compact, bool covers) {
        dir = Dir::Column;
        width = Length::Grow();
        height = Length::Grow();
        header_ = Add<ListHeader>([] { return QueueSummary(); }, compact);
        // Two buttons, as the settings panels keep few choices in view: Add, and the rest in a menu
        // (Repeat and Shuffle live in the Playback panel; Del or the row's x removes one row).
        add_ = HeaderButton(header_, "add", L"Add", [this] {
            ShowMenu(GetHost(), add_->rect, AddToQueueItems(), Place::Below, 200);
        });
        more_ = HeaderButton(header_, "more", L"More", [this, compact, covers] {
            auto item = [](const wchar_t* label, std::function<void()> fn, const char* icon = "") {
                return MenuItem{Tr(label), L"", icon, false, true, false, false, std::move(fn)};
            };
            MenuItem sort = item(L"Sort", {}, "sort");
            sort.enabled = TheApp().queue().Items().size() > 1;
            sort.submenu = {item(L"Sort by name", [] { SortQueue(0); }),
                            item(L"Sort by name, descending", [] { SortQueue(1); }),
                            item(L"Newest first", [] { SortQueue(3); }),
                            item(L"Oldest first", [] { SortQueue(4); }),
                            item(L"Largest first", [] { SortQueue(5); }),
                            item(L"Reverse order", [] { SortQueue(2); })};
            std::vector<MenuItem> items{sort, item(L"Save as playlist…", [] { SaveQueueAsPlaylist(); }, "save")};
            if (!compact && !covers && !PS().hasVideo)
                items.push_back(item(L"Pin beside the music", [] { SetQueueDockSide("right"); }, "sidebar_right"));
            items.push_back(MenuItem::Separator());
            items.push_back(item(L"Clear queue", [] { ClearQueue(); }, "remove"));
            ShowMenu(GetHost(), more_->rect, std::move(items), Place::Below, 240);
        });
        // Music: the queue can stay beside the cover (PlayerView's dock) instead of this popover.
        if (covers) HeaderButton(header_, "close", L"Unpin the queue", [] { SetQueueDockSide(""); });
        auto* sv = Add<ScrollView>();
        sv->width = Length::Grow();
        sv->height = Length::Grow();
        list_ = sv->SetContent<QueueList>(compact, covers);
        conn_ = Connect(TheApp().queue().changed, [this] { header_->Refresh(); });
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) header_->Refresh();
        });
        libConn_ = Connect(TheApp().libraryChanged, [this] { header_->Refresh(); });
    }

private:
    ListHeader* header_;
    IconButton* add_ = nullptr;
    IconButton* more_ = nullptr;
    QueueList* list_;
    ScopedConnection conn_, playerConn_, libConn_;
};

// --- bookmarks ----------------------------------------------------------------------------------

class BookmarkList : public Widget {
public:
    BookmarkList() {
        interactive = true;
        focusable = true;
        dir = Dir::None;
        width = Length::Grow();
        pad = {8, 4, 8, 8};
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & kChangeFile) {
                CancelEdit();
                InvalidateLayout();
            }
        });
        onContextMenu = [this](const MouseEvent& e) {
            const int i = RowAt(e.y);
            if (i < 0) return;
            selected_ = i;
            std::vector<MenuItem> items;
            items.push_back({Tr(L"Go to bookmark"), L"", "play", false, true, false, false, [this, i] { Seek(i); }});
            items.push_back({Tr(L"Rename"), L"F2", "edit", false, true, false, false, [this, i] { BeginEdit(i); }});
            items.push_back({Tr(L"Delete"), L"Del", "remove", false, true, false, false, [this, i] { Delete(i); }});
            ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
        };
    }
    std::vector<library::Bookmark> Marks() const { return TheApp().bookmarks().For(CurrentPath()); }
    bool MenuPoint(Point& at) const override {
        if (selected_ < 0 || selected_ >= static_cast<int>(Marks().size())) return false;
        const Rect r = RowRect(selected_);
        at = {r.x + std::min(r.w / 2, 120.0f), r.Cy()};
        return true;
    }
    static constexpr float kPitch = 44, kRowH = 40;
    Rect RowRect(int i) const {
        return {rect.x + pad.l, rect.y + pad.t + kPitch * static_cast<float>(i), rect.w - pad.H(), kRowH};
    }
    int RowAt(float y) const {
        const int i = static_cast<int>(std::floor((y - rect.y - pad.t) / kPitch));
        return i >= 0 && i < static_cast<int>(Marks().size()) ? i : -1;
    }
    Size MeasureContent(float availW, float) override {
        const size_t n = Marks().size();
        return {availW, n == 0 ? 120.0f : pad.V() + kPitch * static_cast<float>(n) - (kPitch - kRowH)};
    }
    void OnLayout() override {
        if (editor_) editor_->Arrange(RowRect(editIndex_).Inset({8, 4, 146, 4}));
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const auto marks = Marks();
        if (IsHot() && GetHost()) hover_ = RowAt(GetHost()->MousePos().y);
        if (hover_ < 0) tooltip.clear();
        if (marks.empty()) {
            const bool loaded = PS().fileLoaded;
            p.Text(loaded ? Tr(L"No bookmarks yet") : Tr(L"Nothing is playing"), {rect.x, rect.y + 24, rect.w, 24},
                   TextStyle::Body, c.textTertiary, HAlign::Center);
            if (loaded)
                p.Text(Tr(L"Press + to bookmark the current moment."), {rect.x + 24, rect.y + 52, rect.w - 48, 40},
                       TextStyle::Small, c.textTertiary, HAlign::Center, VAlign::Top, gfx::kTextWrap);
            return;
        }
        for (size_t k = 0; k < marks.size(); ++k) {
            const int i = static_cast<int>(k);
            const Rect r = RowRect(i);
            const bool hot = i == hover_ && IsHot();
            if (i == selected_) p.Fill(r, c.bgSelected, kRadiusCard);
            if (hot) p.Fill(r, c.hover, kRadiusCard);
            if (i == selected_ && ShowFocusRing()) PaintFocusRing(p, r, kRadiusCard);
            const std::wstring time = PreciseTime(marks[k].time);
            if (!(editor_ && editIndex_ == i)) {
                const std::wstring name = BookmarkName(marks[k], k);
                const float w = r.w - 12 - 142;
                p.Text(name, {r.x + 12, r.y, w, r.h}, TextStyle::Body, hot ? c.textPrimary : RowText());
                if (hot) RowTip(*this, TextCut(p, name, TextStyle::Body, w), name);
            } else if (hot) {
                tooltip.clear();
            }
            p.Text(time, {r.Right() - 142, r.y, 130, r.h}, TextStyle::Mono, c.textTertiary, HAlign::Trailing);
        }
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i != hover_) {
            hover_ = i;
            tooltip.clear();
            Invalidate();
        }
        return true;
    }
    void OnMouseLeave() override {
        hover_ = -1;
        tooltip.clear();
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i >= 0) {
            selected_ = i;
            Seek(i);
        }
        return true;
    }
    bool OnDoubleClick(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i >= 0) BeginEdit(i);
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        const int n = static_cast<int>(Marks().size());
        if (n == 0) return false;
        if (e.key == VK_UP) selected_ = std::max(0, selected_ - 1);
        else if (e.key == VK_DOWN) selected_ = std::min(n - 1, selected_ + 1);
        else if (e.key == VK_RETURN && selected_ >= 0) Seek(selected_);
        else if (e.key == VK_F2 && selected_ >= 0) BeginEdit(selected_);
        else if (e.key == VK_DELETE && selected_ >= 0) Delete(selected_);
        else return false;
        Invalidate();
        return true;
    }

    void Seek(int i) {
        const auto marks = Marks();
        if (i < 0 || i >= static_cast<int>(marks.size()) || !PS().fileLoaded) return;
        TheApp().player().SeekAbsolute(marks[static_cast<size_t>(i)].time, true);
    }
    void Delete(int i) {
        CancelEdit();
        TheApp().bookmarks().Remove(CurrentPath(), static_cast<size_t>(i));
        selected_ = std::min(selected_, static_cast<int>(Marks().size()) - 1);
        Changed();
    }
    void AddCurrent() {
        if (!PS().fileLoaded) return;
        const size_t i = TheApp().bookmarks().Add(CurrentPath(), PS().position);
        selected_ = static_cast<int>(i);
        Changed();
        BeginEdit(selected_);
    }
    int Selected() const { return selected_; }
    std::function<void()> onChanged;

    void Describe(A11y& a) override {
        a.role = Role::List;
        const auto marks = Marks();
        a.name = !marks.empty() ? Tr(L"Bookmarks") : PS().fileLoaded ? Tr(L"No bookmarks yet") : Tr(L"Nothing is playing");
        a.items = static_cast<int>(marks.size());
        a.item = [this, marks](int i, A11y::Item& it) {
            const auto k = static_cast<size_t>(i);
            it.name = BookmarkName(marks[k], k) + L", " + FormatTime(marks[k].time, true);
            it.rect = RowRect(i);
            it.selected = i == selected_;
        };
        a.selectItem = [this](int i) {
            selected_ = i;
            Invalidate();
        };
        a.invokeItem = [this](int i) {
            selected_ = i;
            Seek(i);
            Invalidate();
        };
        a.focusedItem = selected_;
    }

private:
    void Changed() {
        InvalidateLayout();
        if (onChanged) onChanged();
    }
    void BeginEdit(int i) {
        CancelEdit();
        const auto marks = Marks();
        if (i < 0 || i >= static_cast<int>(marks.size())) return;
        editIndex_ = i;
        auto box = std::make_unique<EditBox>();
        box->SetText(BookmarkName(marks[static_cast<size_t>(i)], static_cast<size_t>(i)));
        box->onSubmit = [this](const std::wstring& t) { FinishEdit(t); };
        box->onEscape = [this] { Post([this] { CancelEdit(); }); };
        editor_ = static_cast<TextBox*>(AddChild(std::move(box)));
        InvalidateLayout();
        if (Host* h = GetHost()) {
            h->SetFocus(editor_, true);
            editor_->SelectAll();
        }
    }
    void FinishEdit(const std::wstring& text) {
        const int i = editIndex_;
        std::wstring name = text;
        while (!name.empty() && iswspace(name.back())) name.pop_back();
        Post([this, i, name] {
            TheApp().bookmarks().Rename(CurrentPath(), static_cast<size_t>(i), name);
            CancelEdit();
            if (Host* h = GetHost()) h->SetFocus(this);
        });
    }
    void CancelEdit() {
        if (!editor_) return;
        Widget* e = editor_;
        editor_ = nullptr;
        if (Host* h = GetHost(); h && h->Focus() == e) h->SetFocus(this);
        RemoveChild(e);
        Invalidate();
    }
    void Post(std::function<void()> fn) {
        // Removing the editor from inside its own key handler would free it mid-call.
        if (Host* h = GetHost())
            h->Post([fn = std::move(fn), alive = std::weak_ptr<bool>(alive_)] {
                if (!alive.expired()) fn();
            });
    }

    int hover_ = -1, selected_ = -1;
    TextBox* editor_ = nullptr;
    int editIndex_ = -1;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    ScopedConnection conn_;
};

class BookmarksTab : public Widget {
public:
    BookmarksTab() {
        dir = Dir::Column;
        width = Length::Grow();
        height = Length::Grow();
        header_ = Add<ListHeader>([] { return CountBookmarks(TheApp().bookmarks().For(CurrentPath()).size()); }, false);
        HeaderButton(header_, "remove", L"Delete bookmark", [this] {
            if (list_->Selected() >= 0) list_->Delete(list_->Selected());
        });
        HeaderButton(header_, "add", L"Add bookmark", [this] { list_->AddCurrent(); });
        auto* sv = Add<ScrollView>();
        sv->width = Length::Grow();
        sv->height = Length::Grow();
        list_ = sv->SetContent<BookmarkList>();
        list_->onChanged = [this] { header_->Refresh(); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & kChangeFile) header_->Refresh();
        });
    }

private:
    ListHeader* header_;
    BookmarkList* list_;
    ScopedConnection conn_;
};

// --- chapters ---------------------------------------------------------------------------------

class ChapterList : public Widget {
public:
    ChapterList() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        pad = {8, 4, 8, 8};
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeChapters | kChangeFile)) {
                InvalidateLayout();
            } else if (m & (kChangeTime | kChangeState)) {
                // Time ticks only matter when they move the highlight to another chapter.
                const int cur = CurrentChapter();
                if (cur != paintedCurrent_) Invalidate();
            }
        });
    }
    static constexpr float kPitch = 44, kRowH = 40;
    int Count() const { return static_cast<int>(PS().chapters.size()); }
    Rect RowRect(int i) const {
        return {rect.x + pad.l, rect.y + pad.t + kPitch * static_cast<float>(i), rect.w - pad.H(), kRowH};
    }
    int RowAt(float y) const {
        const int i = static_cast<int>(std::floor((y - rect.y - pad.t) / kPitch));
        return i >= 0 && i < Count() ? i : -1;
    }
    int CurrentChapter() const {
        const PlayerState& s = PS();
        if (s.chapter >= 0) return static_cast<int>(s.chapter);
        int cur = -1;
        for (int i = 0; i < Count(); ++i)
            if (s.chapters[static_cast<size_t>(i)].time <= s.position + 0.05) cur = i;
        return cur;
    }
    Size MeasureContent(float availW, float) override {
        const int n = Count();
        return {availW, n == 0 ? 120.0f : pad.V() + kPitch * static_cast<float>(n) - (kPitch - kRowH)};
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const int n = Count();
        if (IsHot() && GetHost()) {
            const int h = RowAt(GetHost()->MousePos().y);
            if (h != hover_) tooltip.clear();
            hover_ = h;
        }
        if (n == 0) {
            p.Text(Tr(L"This file has no chapters"), {rect.x, rect.y + 24, rect.w, 24}, TextStyle::Body, c.textTertiary,
                   HAlign::Center);
            return;
        }
        const int cur = CurrentChapter();
        paintedCurrent_ = cur;
        // Only the rows in view (an audiobook has hundreds of chapters).
        const Rect clip = p.Clip();
        const int first = std::max(0, static_cast<int>((clip.y - rect.y - pad.t) / kPitch) - 1);
        const int last = std::min(n - 1, static_cast<int>((clip.Bottom() - rect.y - pad.t) / kPitch) + 1);
        for (int i = first; i <= last; ++i) {
            const Chapter& ch = PS().chapters[static_cast<size_t>(i)];
            const Rect r = RowRect(i);
            const bool hot = i == hover_ && IsHot();
            const bool isCur = i == cur;
            if (hot) p.Fill(r, c.hover, kRadiusCard);
            if (i == selected_ && ShowFocusRing()) PaintFocusRing(p, r, kRadiusCard);
            // As the queue: the chapter playing now has the play mark in the number column.
            const Rect slot{r.x + 2, r.y, 40, r.h};
            if (isCur) p.Icon("play", slot.Center(16, 16), c.textPrimary);
            else p.Text(std::to_wstring(i + 1), slot, TextStyle::MonoSmall, c.textTertiary, HAlign::Center);
            const std::wstring title = ch.title.empty() ? Fmt(Tr(L"Chapter {0}"), static_cast<long long>(i + 1)) : ch.title;
            const float w = r.w - 44 - 102;
            p.Text(title, {r.x + 44, r.y, w, r.h}, TextStyle::Body, isCur || hot ? c.textPrimary : RowText());
            if (hot) RowTip(*this, TextCut(p, title, TextStyle::Body, w), title);
            p.Text(FormatTime(ch.time, PS().duration >= 3600), {r.Right() - 102, r.y, 90, r.h}, TextStyle::Mono,
                   isCur ? c.textSecondary : c.textTertiary, HAlign::Trailing);
        }
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i != hover_) {
            hover_ = i;
            tooltip.clear();
            Invalidate();
        }
        return true;
    }
    void OnMouseLeave() override {
        hover_ = -1;
        tooltip.clear();
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i >= 0) {
            selected_ = i;
            Seek(i);
        }
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        const int n = Count();
        if (n == 0) return false;
        if (selected_ < 0) selected_ = std::max(0, CurrentChapter());
        if (e.key == VK_UP) selected_ = std::max(0, selected_ - 1);
        else if (e.key == VK_DOWN) selected_ = std::min(n - 1, selected_ + 1);
        else if (e.key == VK_RETURN) Seek(selected_);
        else return false;
        if (ScrollView* sv = ScrollerOf(this)) sv->ScrollIntoView(RowRect(selected_));
        Invalidate();
        return true;
    }
    void Seek(int i) {
        if (i < 0 || i >= Count()) return;
        TheApp().player().SeekAbsolute(PS().chapters[static_cast<size_t>(i)].time, true);
    }

    void Describe(A11y& a) override {
        a.role = Role::List;
        a.name = Count() > 0 ? Tr(L"Chapters") : Tr(L"This file has no chapters");
        a.items = Count();
        a.item = [this, cur = CurrentChapter()](int i, A11y::Item& it) {
            const Chapter& ch = PS().chapters[static_cast<size_t>(i)];
            it.name = ch.title.empty() ? Fmt(Tr(L"Chapter {0}"), static_cast<long long>(i + 1)) : ch.title;
            it.name += L", " + FormatTime(ch.time, PS().duration >= 3600);
            if (i == cur) it.name += L", " + std::wstring(Tr(L"Now playing"));
            it.rect = RowRect(i);
            it.selected = i == selected_;
        };
        a.selectItem = [this](int i) {
            selected_ = i;
            Invalidate();
        };
        a.invokeItem = [this](int i) {
            selected_ = i;
            Seek(i);
            Invalidate();
        };
        a.focusedItem = selected_;
    }

private:
    int hover_ = -1, selected_ = -1;
    int paintedCurrent_ = -2;  // the chapter highlighted by the last paint
    ScopedConnection conn_;
};

}  // namespace

std::unique_ptr<Widget> BuildQueueTab(bool compact, bool covers) {
    return std::make_unique<QueueTab>(compact, covers);
}

std::unique_ptr<IconButton> MakeRepeatButton() {
    return std::make_unique<RepeatButton>();
}

std::unique_ptr<Widget> BuildBookmarksTab() {
    return std::make_unique<BookmarksTab>();
}

std::unique_ptr<Widget> BuildChaptersTab() {
    auto sv = std::make_unique<ScrollView>();
    sv->width = Length::Grow();
    sv->height = Length::Grow();
    sv->SetContent<ChapterList>();
    return sv;
}

float CompactQueueHeight() {
    const float rows = static_cast<float>(std::max<size_t>(1, TheApp().queue().Items().size()));
    return kHeaderH + 40 + std::min(rows, 8.0f) * 31 + 16;
}

}  // namespace app::panel
