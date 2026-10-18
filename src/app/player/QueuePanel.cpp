// Queue / Bookmarks / Chapters tabs of the list popover, and the mini player queue.
#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "gfx/Text.h"

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

// Small icon button for list headers (24 DIP icons in a 30×28 hit box).
IconButton* HeaderButton(Widget* parent, const char* icon, const wchar_t* tip, std::function<void()> click, int box = 30,
                         int iconSize = 24) {
    auto* b = parent->Add<IconButton>(icon, Tr(tip), box, iconSize);
    b->width = Length::Px(static_cast<float>(box));
    b->height = Length::Px(28);
    b->onClick = std::move(click);
    return b;
}

// Summary line + buttons, 68 DIP tall in the design (40 in the mini player).
class ListHeader : public Widget {
public:
    ListHeader(std::function<std::wstring()> text, bool compact) : text_(std::move(text)), compact_(compact) {
        dir = Dir::Row;
        align = Align::Center;
        width = Length::Grow();
        height = Length::Px(compact ? 40.0f : 68.0f);
        pad = compact ? Insets{20, 0, 14, 0} : Insets{28, 0, 18, 0};
        gap = compact ? 4.0f : 6.0f;
        label_ = Add<Label>(L"", compact ? TextStyle::Small : TextStyle::Body, Tone::Tertiary);
        label_->width = Length::Grow();
        Refresh();
    }
    void Refresh() {
        label_->SetText(text_());
    }

private:
    std::function<std::wstring()> text_;
    bool compact_;
    Label* label_;
};

// --- queue ------------------------------------------------------------------------------------

class QueueList : public Widget {
public:
    explicit QueueList(bool compact) : compact_(compact) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        id = "queue-list";  // Panels.cpp gives it the keyboard when the queue opens
        pad = compact ? Insets{12, 0, 12, 12} : Insets{20, 0, 20, 20};
        queueConn_ = Connect(TheApp().queue().changed, [this] {
            // The rows changed under a drag (a removal, a new queue): its indices mean nothing now.
            dragging_ = false;
            downIndex_ = -1;
            selected_ = std::min(selected_, static_cast<int>(TheApp().queue().Items().size()) - 1);
            rows_.clear();
            InvalidateLayout();
        });
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) rows_.clear();
            if (m & (kChangeFile | kChangeMeta | kChangeState)) Invalidate();
        });
        libConn_ = Connect(TheApp().libraryChanged, [this] {
            rows_.clear();
            Invalidate();
        });
        onContextMenu = [this](const MouseEvent& e) { ShowMenuAt(e); };
    }

    float Pitch() const { return compact_ ? 31.0f : 44.0f; }
    float RowH() const { return compact_ ? 26.0f : 32.0f; }
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
        if (Playing(i) && !PS().hasVideo) return PS().artist;
        if (const library::MediaItem* m = LibraryItem(i); m && m->kind == library::MediaKind::Audio) return m->artist;
        return {};
    }

    void PaintRow(Painter& p, int i, const Rect& r, bool floating) {
        const Palette& c = P();
        const bool current = i == TheApp().queue().Current();
        const bool hot = !dragging_ && i == hover_ && IsHot();
        if (floating) {
            p.Shadow(r, 4, 10, 4, 0.35f);
            p.Fill(r, c.bgMenu, 4);
        } else if (current) {
            p.Fill(r, c.accentFaint, 4);
        }
        // The selection (what Enter plays and Del removes) shows after a mouse click too; dimmer
        // while the keyboard is elsewhere.
        if (!floating && i == selected_) p.Fill(r, IsFocused() ? c.bgSelected : c.bgSelected.Fade(0.5f), 4);
        if (!floating && hot) p.Fill(r, c.hover, 4);
        // The selected, playing and hovered fills are near-equal greys with the neutral accents:
        // an accent edge says which row Del removes and Enter plays (VF-03), dimmer unfocused.
        if (!floating && i == selected_)
            p.Fill({r.x + 1, r.y + 6, 3, r.h - 12}, IsFocused() ? c.accent : c.textTertiary, 1.5f);
        if (!floating && i == selected_ && IsFocused() && ShowFocusRing()) PaintFocusRing(p, r, 4);
        const float box = RowH();
        const bool handleHot = hot && hoverHandle_;
        p.Icon("move", Rect{r.x, r.y, box, box}.Center(compact_ ? 18.0f : 24.0f, compact_ ? 18.0f : 24.0f),
               handleHot || floating ? c.textPrimary : c.textTertiary);
        const TextStyle ts = compact_ ? TextStyle::Small : TextStyle::Body;
        const float textX = r.x + box + (compact_ ? 8.0f : 10.0f);
        const double d = DurationOf(i);
        const std::wstring dur = d > 0 ? ClockTime(d) : L"";
        const float dw = dur.empty() ? 0.0f : gfx::Text::Get().Measure(dur, ts).width + 4;
        const bool showRemove = hot && !compact_ && !removeCold_;
        const float right = r.Right() - (showRemove ? 28.0f : 0.0f) - (compact_ ? 6.0f : 10.0f);
        const Color fg = current ? c.accent : hot ? c.textPrimary : RowText();
        const float avail = std::max(0.0f, right - textX - dw - 10);
        const RowInfo& info = Info(i);
        const std::wstring& title = info.title;
        const std::wstring& artist = Playing(i) && !PS().hasVideo ? PS().artist : info.artist;
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
            const Rect x{r.Right() - 26, r.Cy() - 12, 24, 24};
            if (hoverRemove_) p.Fill(x, c.hover, 4);
            p.Icon("close", x.Center(18, 18), hoverRemove_ ? c.textPrimary : c.textTertiary);
        }
        if (!dur.empty())
            p.Text(dur, {right - dw - 4, r.y, dw + 4, r.h}, ts, current ? c.textSecondary : c.textTertiary, HAlign::Trailing);
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
        TheApp().RemoveFromQueue(i);
        hover_ = -1;
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

private:
    bool OnRemoveButton(int i, float x) const { return !compact_ && !removeCold_ && x >= RowRect(i).Right() - 28; }

    void AutoScroll(float y) {
        ScrollView* sv = ScrollerOf(this);
        if (!sv) return;
        if (y < sv->rect.y + 24) sv->ScrollTo(sv->Offset() - 12, false);
        else if (y > sv->rect.Bottom() - 24) sv->ScrollTo(sv->Offset() + 12, false);
    }

    void ShowMenuAt(const MouseEvent& e) {
        const int i = RowAt(e.y);
        if (i < 0) return;
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
    int hover_ = -1, selected_ = -1, downIndex_ = -1;
    bool hoverHandle_ = false, hoverRemove_ = false;
    bool removeCold_ = false;  // the "×" stays off until the pointer leaves where it removed a row
    Point removedAt_{};
    std::vector<RowInfo> rows_;
    bool dragging_ = false;
    int dragFrom_ = 0, dropIndex_ = 0;
    float dragY_ = 0, grabDy_ = 0;
    bool scrolled_ = false;
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
}

void AddFilesToQueue() {
    std::vector<QueueItem> add;
    for (const std::wstring& f : PickFiles(0)) add.push_back({f, FileNameOf(f), 0});
    if (add.empty()) return;
    const bool wasEmpty = TheApp().queue().Items().empty();
    TheApp().queue().Add(std::move(add));
    if (wasEmpty || !PS().fileLoaded) TheApp().PlayIndex(TheApp().queue().Current() < 0 ? 0 : TheApp().queue().Current());
}

// Repeat button with a small "1" for repeat-one (the queue header and the control bar).
class RepeatButton : public IconButton {
public:
    explicit RepeatButton(bool header) : IconButton("repeat_right", Tr(L"Repeat")) {
        if (header) {
            width = Length::Px(30);
            height = Length::Px(28);
        }
        onClick = [] { Run(Action::ToggleRepeat); };
        conn_ = Connect(TheApp().queue().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        const Repeat r = TheApp().queue().RepeatMode();
        active = r != Repeat::Off;
        tooltip = r == Repeat::Off ? Tr(L"Repeat off") : r == Repeat::All ? Tr(L"Repeat all") : Tr(L"Repeat one");
        IconButton::Paint(p);
        if (r == Repeat::One) {
            const Rect b{rect.Right() - 11, rect.y + 2, 10, 10};
            p.Fill(b, P().accent, 5);
            p.Text(L"1", b, TextStyle::Micro, P().onAccent, HAlign::Center);
        }
    }

private:
    ScopedConnection conn_;
};

class ShuffleButton : public IconButton {
public:
    ShuffleButton() : IconButton("shuffle_right", Tr(L"Shuffle")) {
        width = Length::Px(30);
        height = Length::Px(28);
        onClick = [] { Run(Action::ToggleShuffle); };
        conn_ = Connect(TheApp().queue().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        active = TheApp().queue().Shuffle();
        IconButton::Paint(p);
    }

private:
    ScopedConnection conn_;
};

// Column: header + scrolling list. Keeps the header text in sync.
class QueueTab : public Widget {
public:
    explicit QueueTab(bool compact) {
        dir = Dir::Column;
        width = Length::Grow();
        height = Length::Grow();
        header_ = Add<ListHeader>([] { return QueueSummary(); }, compact);
        sort_ = HeaderButton(header_, "sort", L"Sort", [this] {
            std::vector<MenuItem> items;
            items.push_back({Tr(L"Sort by name"), L"", "", false, true, false, false, [] { SortQueue(0); }});
            items.push_back({Tr(L"Sort by name, descending"), L"", "", false, true, false, false, [] { SortQueue(1); }});
            items.push_back({Tr(L"Reverse order"), L"", "", false, true, false, false, [] { SortQueue(2); }});
            ShowMenu(GetHost(), sort_->rect, std::move(items), Place::Below, 220);
        });
        if (!compact) {
            header_->Add<RepeatButton>(true);
            header_->Add<ShuffleButton>();
        }
        HeaderButton(header_, "remove", L"Remove selected", [this] {
            const int sel = list_->Selected();
            if (sel >= 0) {
                list_->Remove(sel);
                return;
            }
            // Nothing selected: clear everything except what is playing, in one change (one
            // Remove per row was O(n²) and froze a 5000-item queue for seconds).
            PlayQueue& q = TheApp().queue();
            if (const QueueItem* cur = q.CurrentItem()) q.Set({*cur}, 0, q.Name());
            else q.Clear();
        });
        HeaderButton(header_, "add", L"Add files", [] { AddFilesToQueue(); });
        auto* sv = Add<ScrollView>();
        sv->width = Length::Grow();
        sv->height = Length::Grow();
        list_ = sv->SetContent<QueueList>(compact);
        conn_ = Connect(TheApp().queue().changed, [this] { header_->Refresh(); });
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) header_->Refresh();
        });
        libConn_ = Connect(TheApp().libraryChanged, [this] { header_->Refresh(); });
    }

private:
    ListHeader* header_;
    IconButton* sort_ = nullptr;
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
        pad = {20, 0, 20, 20};
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
    static constexpr float kPitch = 44, kRowH = 32;
    Rect RowRect(int i) const { return {rect.x + pad.l, rect.y + kPitch * static_cast<float>(i), rect.w - pad.H(), kRowH}; }
    int RowAt(float y) const {
        const int i = static_cast<int>(std::floor((y - rect.y) / kPitch));
        return i >= 0 && i < static_cast<int>(Marks().size()) ? i : -1;
    }
    Size MeasureContent(float availW, float) override {
        const size_t n = Marks().size();
        return {availW, n == 0 ? 120.0f : kPitch * static_cast<float>(n) + pad.b};
    }
    void OnLayout() override {
        if (editor_) editor_->Arrange(RowRect(editIndex_).Inset({-4, 0, 140, 0}));
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
            if (i == selected_) p.Fill(r, c.bgSelected, 4);
            if (hot) p.Fill(r, c.hover, 4);
            if (i == selected_ && ShowFocusRing()) PaintFocusRing(p, r, 4);
            const std::wstring time = PreciseTime(marks[k].time);
            if (!(editor_ && editIndex_ == i)) {
                const std::wstring name = BookmarkName(marks[k], k);
                p.Text(name, {r.x, r.y, r.w - 130, r.h}, TextStyle::Body, hot ? c.textPrimary : RowText());
                if (hot) RowTip(*this, TextCut(p, name, TextStyle::Body, r.w - 130), name);
            } else if (hot) {
                tooltip.clear();
            }
            p.Text(time, {r.Right() - 130, r.y, 126, r.h}, TextStyle::Mono, c.textTertiary, HAlign::Trailing);
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
        pad = {20, 20, 20, 20};
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
    static constexpr float kPitch = 44, kRowH = 32;
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
            if (isCur) p.Fill(r, c.accentFaint, 4);
            if (hot) p.Fill(r, c.hover, 4);
            if (i == selected_ && ShowFocusRing()) PaintFocusRing(p, r, 4);
            p.Text(std::to_wstring(i + 1), {r.x + 8, r.y, 32, r.h}, TextStyle::Body, isCur ? c.accent : c.accentMid);
            const std::wstring title = ch.title.empty() ? Fmt(Tr(L"Chapter {0}"), static_cast<long long>(i + 1)) : ch.title;
            p.Text(title, {r.x + 48, r.y, r.w - 48 - 90, r.h}, TextStyle::Body,
                   isCur ? c.accent : hot ? c.textPrimary : RowText());
            if (hot) RowTip(*this, TextCut(p, title, TextStyle::Body, r.w - 48 - 90), title);
            p.Text(FormatTime(ch.time, PS().duration >= 3600), {r.Right() - 90, r.y, 86, r.h}, TextStyle::Mono,
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

private:
    int hover_ = -1, selected_ = -1;
    int paintedCurrent_ = -2;  // the chapter highlighted by the last paint
    ScopedConnection conn_;
};

}  // namespace

std::unique_ptr<Widget> BuildQueueTab(bool compact) {
    return std::make_unique<QueueTab>(compact);
}

std::unique_ptr<IconButton> MakeRepeatButton() {
    return std::make_unique<RepeatButton>(false);
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
