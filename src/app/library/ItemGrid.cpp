#include "app/library/ItemGrid.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/library/LibraryCommon.h"
#include "library/MediaTypes.h"
#include "net/Online.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string_view>
#include <unordered_map>

namespace app::lib {

using namespace ui;

namespace {

// Measurements from the Figma file (Player_6261-15078, 970-4028, sidebar_848-1996).
constexpr float kVideoMinW = 316, kVideoRatio = 178.0f / 316.0f;
constexpr float kSquareMinW = 200;
// Narrow windows keep two columns of smaller cards rather than one huge card per row.
constexpr float kVideoNarrowW = 220, kSquareNarrowW = 150;
constexpr float kCardGapX = 20, kVideoGapY = 40, kSquareGapY = 32;
constexpr float kLabelGap = 20, kLineH = 19, kSubGap = 10;
constexpr float kCardRadius = 12;
constexpr float kRowH = 42, kThumb = 32;
constexpr float kHeaderCards = 64, kHeaderCardsFirst = 40, kHeaderRows = 48, kHeaderRowsFirst = 32;

bool IsCardStyle(GridStyle s) {
    return s == GridStyle::VideoCards || s == GridStyle::SquareCards;
}

// Group titles and column titles: painted, never selected.
bool IsLabel(EntryKind k) {
    return k == EntryKind::Header || k == EntryKind::Columns;
}

// One click rule (UX-20): what holds other things opens on a single click, like a link; a file
// plays from the ▶ on its card or on a double click.
bool OpensOnClick(EntryKind k) {
    return k == EntryKind::Action || k == EntryKind::Category || k == EntryKind::Folder || k == EntryKind::Playlist ||
           k == EntryKind::Album || k == EntryKind::Artist || k == EntryKind::Genre;
}

constexpr float kPlayD = 48;  // the ▶ on a hovered media card

// The track column fits "#" with the sort arrow beside it.
constexpr float kColumnsH = 32, kTrackW = 32, kDurationW = 76;
// Narrow lists keep the title readable: below these widths of the text columns the album
// column goes, then the artist (or details) column.
constexpr float kExtraMinFlex = 420, kSubMinFlex = 240;
// Cards stretch to fill the row, but past this factor of their design width one more column fits.
constexpr float kCardMaxGrow = 1.25f;

constexpr float kChipH = 20;

// "Damaged file" chip of a file the scanner found unplayable: the warning icon and the words, the
// same on cards (the badge slot) and on list rows (after the title).
float DamagedChipWidth(Painter& p) {
    return 6 + 14 + 5 + std::ceil(p.Measure(Tr(L"Damaged file"), TextStyle::Small).width) + 8;
}

void PaintDamagedChip(Painter& p, const Rect& b, Color bg) {
    const Palette& c = P();
    p.Fill(b, bg, 4);
    const float x = b.x + 6, cy = b.Cy();
    p.Icon("warning", {x - 1, cy - 8, 16, 16}, c.danger);
    p.Text(Tr(L"Damaged file"), {x + 19, b.y, b.Right() - x - 19 - 6, b.h}, TextStyle::Small, c.textPrimary, HAlign::Leading,
           VAlign::Center, 0);
}

// "New" chip of a file found since the user last looked at the library: the word on the accent.
float NewChipWidth(Painter& p) {
    return 8 + std::ceil(p.Measure(Tr(L"New"), TextStyle::Small).width) + 8;
}

void PaintNewChip(Painter& p, const Rect& b) {
    const Palette& c = P();
    p.Fill(b, c.accent, 4);
    p.Text(Tr(L"New"), {b.x + 8, b.y, b.w - 16, b.h}, TextStyle::Small, c.onAccent, HAlign::Leading, VAlign::Center, 0);
}

// When Enter opened the page, its grid lands on the first item with a focus ring.
uint64_t g_keyActivateTick = 0;

// The file or folder an entry stands for, to name its folder: albums carry their name as the
// path, so their cover track is used.
const std::wstring& LocationOf(const Entry& e) {
    return e.kind == EntryKind::Album ? e.image : e.path;
}

}  // namespace

void Disambiguate(std::vector<Entry>& v) {
    auto eligible = [](const Entry& e) {
        return (e.kind == EntryKind::Media || e.kind == EntryKind::Folder || e.kind == EntryKind::Album) && !e.title.empty();
    };
    // Titles that occur more than once; most lists have none, so this stays one hash pass.
    std::unordered_map<std::wstring_view, int> seen;
    seen.reserve(v.size());
    bool any = false;
    for (const Entry& e : v)
        if (eligible(e)) any |= ++seen[std::wstring_view(e.title)] > 1;
    if (!any) return;
    std::map<std::wstring, std::vector<size_t>> groups;  // the full visible text -> entries
    for (size_t i = 0; i < v.size(); ++i) {
        const Entry& e = v[i];
        const std::wstring& where = LocationOf(e);
        if (!eligible(e) || where.empty() || IsUrl(where) || seen[std::wstring_view(e.title)] < 2) continue;
        groups[e.title + L"\x1F" + e.subtitle + L"\x1F" + e.extra].push_back(i);
    }
    for (auto& [key, idx] : groups) {
        if (idx.size() < 2) continue;
        // The parent folder's name, else the two folders above the item, else the whole folder path.
        auto label = [&](size_t i, int depth) {
            std::wstring folder = library::ParentFolder(LocationOf(v[i]));
            if (depth >= 3) return folder;
            std::wstring out = library::FileNameOf(folder);
            if (depth == 2) {
                const std::wstring up = library::FileNameOf(library::ParentFolder(folder));
                if (!up.empty()) out = up + L" › " + out;
            }
            return out;
        };
        for (int depth = 1; depth <= 3; ++depth) {
            std::vector<std::wstring> labels;
            for (size_t i : idx) labels.push_back(label(i, depth));
            std::vector<std::wstring> sorted = labels;
            std::sort(sorted.begin(), sorted.end());
            const bool distinct = std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
            if (!distinct && depth < 3) continue;
            for (size_t k = 0; k < idx.size(); ++k) {
                Entry& e = v[idx[k]];
                if (labels[k].empty()) continue;
                e.subtitle = e.subtitle.empty() ? labels[k] : e.subtitle + L" · " + labels[k];
            }
            break;
        }
    }
}

ItemGrid::ItemGrid() {
    created_ = GetTickCount64();
    interactive = true;
    focusable = true;
    width = Length::Grow();
    onContextMenu = [this](const MouseEvent& e) {
        if (JustOpened()) return;
        const int i = IndexAt(e.x, e.y);
        if (i < 0 || !Selectable(i)) return;
        Select(i, false);
        if (GetHost()) GetHost()->SetFocus(this);
        if (onMenu) onMenu(i, {e.x, e.y});
    };
    imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeFile | kChangeState)) Invalidate();
    });
}

ItemGrid::~ItemGrid() = default;

void ItemGrid::SetEntries(std::vector<Entry> entries) {
    // Keep the selection on the same item when the list is rebuilt (rescan, filter).
    std::wstring selPath;
    std::wstring selTitle;
    if (selected_ >= 0 && selected_ < static_cast<int>(entries_.size())) {
        selPath = entries_[static_cast<size_t>(selected_)].path;
        selTitle = entries_[static_cast<size_t>(selected_)].title;
    }
    entries_ = std::move(entries);
    Disambiguate(entries_);
    selected_ = -1;
    if (!selPath.empty() || !selTitle.empty())
        for (size_t i = 0; i < entries_.size(); ++i)
            if (!IsLabel(entries_[i].kind) && entries_[i].path == selPath && entries_[i].title == selTitle) {
                selected_ = static_cast<int>(i);
                break;
            }
    hover_ = -1;
    hoverHead_ = -1;
    press_ = -1;
    dragging_ = false;
    ++version_;
    ResetTip();
    InvalidateLayout();
}

void ItemGrid::DebugInfo(std::string& out) const {
    if (selected_ >= 0) out += " selected=" + std::to_string(selected_);
}

void ItemGrid::Describe(ui::A11y& a) {
    a.role = ui::Role::List;
    a.items = static_cast<int>(entries_.size());
    a.item = [this](int i, ui::A11y::Item& it) {
        const Entry& e = entries_[static_cast<size_t>(i)];
        it.role = IsLabel(e.kind) ? ui::Role::Text : ui::Role::ListItem;
        it.name = e.subtitle.empty() ? e.title : e.title + L", " + e.subtitle;
        it.rect = ItemRect(i);
        it.selected = i == selected_;
    };
    a.selectItem = [this](int i) { Select(i); };
    a.invokeItem = [this](int i) {  // as a double click (Enter) would
        if (!Selectable(i)) return;
        Select(i, false);
        if (onActivate) onActivate(i);
    };
    a.focusedItem = selected_;
}

void ItemGrid::SetStyle(GridStyle s) {
    if (style_ == s) return;
    style_ = s;
    ++version_;
    InvalidateLayout();
}

bool ItemGrid::Selectable(int i) const {
    return i >= 0 && i < static_cast<int>(entries_.size()) && !IsLabel(entries_[static_cast<size_t>(i)].kind) &&
           !slots_.empty() && static_cast<size_t>(i) < slots_.size() && slots_[static_cast<size_t>(i)].w > 0;
}

// --- layout -------------------------------------------------------------------------------

void ItemGrid::Compute(float w) {
    if (!std::isfinite(w) || w <= 0) w = 1000;
    if (w == computedW_ && version_ == computedVersion_) return;
    computedW_ = w;
    computedVersion_ = version_;
    const size_t n = entries_.size();
    slots_.assign(n, Rect{});

    const bool cards = IsCardStyle(style_);
    float cardH = kRowH, gapY = 0;
    geo_ = {};
    if (cards) {
        const float minCard = style_ == GridStyle::VideoCards ? kVideoMinW : kSquareMinW;
        int cols = std::max(1, static_cast<int>(std::floor((w + kCardGapX) / (minCard + kCardGapX))));
        const float narrowCard = style_ == GridStyle::VideoCards ? kVideoNarrowW : kSquareNarrowW;
        if (stretch && cols < 2 && w >= 2 * narrowCard + kCardGapX) cols = 2;
        auto fill = [&](int c) { return (w - kCardGapX * static_cast<float>(c - 1)) / static_cast<float>(c); };
        // Cards fill the row but stay near the design size: a wide window shows more of them,
        // not a few huge ones (a 474-DIP card at 2560 px).
        if (cols >= 2 || stretch)
            while (fill(cols) > minCard * kCardMaxGrow && fill(cols + 1) >= narrowCard) ++cols;
        geo_.cols = cols;
        // Home's one-card rows in a narrow window keep the design size.
        geo_.cardW = !stretch && cols == 1 ? minCard : std::floor(fill(cols));
        if (geo_.cardW > w) geo_.cardW = w;
        geo_.thumbH = style_ == GridStyle::VideoCards ? std::round(geo_.cardW * kVideoRatio) : std::round(geo_.cardW);
        bool subs = false;
        for (const Entry& e : entries_)
            if (e.kind != EntryKind::Header && !e.subtitle.empty()) {
                subs = true;
                break;
            }
        geo_.labelH = kLabelGap + kLineH + (subs ? kSubGap + kLineH : 0);
        cardH = geo_.thumbH + geo_.labelH;
        gapY = style_ == GridStyle::VideoCards ? kVideoGapY : kSquareGapY;
    }

    float y = 0, bottom = 0;
    int col = 0, rows = 0;
    bool limited = false;
    for (size_t i = 0; i < n; ++i) {
        const Entry& e = entries_[i];
        if (limited) {
            slots_[i] = {0, bottom, 0, 0};
            continue;
        }
        if (e.kind == EntryKind::Columns) {
            // Column titles belong to list rows only.
            if (cards || style_ == GridStyle::CompactRows || (maxRows > 0 && rows >= maxRows)) {
                slots_[i] = {0, y, 0, 0};
                continue;
            }
            slots_[i] = {0, y, w, kColumnsH};
            y += kColumnsH;
            bottom = y;
            continue;
        }
        if (e.kind == EntryKind::Header) {
            if (col > 0) {
                y += cardH + gapY;
                col = 0;
                ++rows;
            }
            if (maxRows > 0 && rows >= maxRows) {
                limited = true;
                slots_[i] = {0, bottom, 0, 0};
                continue;
            }
            const bool first = bottom <= 0;
            float h = cards ? (first ? kHeaderCardsFirst : kHeaderCards) : (first ? kHeaderRowsFirst : kHeaderRows);
            // Cards end with a row gap already; rows need a little air above a group title.
            if (cards && !first) h -= gapY - 8;
            slots_[i] = {0, y, w, h};
            y += h;
            bottom = y;
            continue;
        }
        if (maxRows > 0 && rows >= maxRows) {
            limited = true;
            slots_[i] = {0, bottom, 0, 0};
            continue;
        }
        if (cards) {
            slots_[i] = {static_cast<float>(col) * (geo_.cardW + kCardGapX), y, geo_.cardW, cardH};
            bottom = y + cardH;
            if (++col == geo_.cols) {
                col = 0;
                y += cardH + gapY;
                ++rows;
            }
        } else {
            slots_[i] = {0, y, w, kRowH};
            y += kRowH;
            bottom = y;
            ++rows;
        }
    }
    height_ = bottom;
}

Size ItemGrid::MeasureContent(float availW, float) {
    Compute(availW);
    return {std::isfinite(availW) ? availW : computedW_, height_};
}

void ItemGrid::OnLayout() {
    Compute(rect.w);
    if (keyFocusPending_ && !slots_.empty()) {
        keyFocusPending_ = false;
        Host* h = GetHost();
        if (h && IsFocused() && selected_ < 0)
            for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
                if (Selectable(i)) {
                    Select(i, false);
                    h->SetFocus(this, true);  // same widget: only turns the focus ring on
                    break;
                }
    }
}

bool ItemGrid::JustOpened() const {
    return GetTickCount64() - created_ < static_cast<uint64_t>(GetDoubleClickTime());
}

Rect ItemGrid::ItemRect(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= slots_.size()) return {};
    return slots_[static_cast<size_t>(index)].Offset(rect.x, rect.y);
}

int ItemGrid::IndexAt(float x, float y) const {
    const float rx = x - rect.x, ry = y - rect.y;
    if (rx < 0 || ry < 0 || ry >= height_) return -1;
    // First slot whose bottom is below the point; scan the row from there.
    auto it = std::lower_bound(slots_.begin(), slots_.end(), ry, [](const Rect& r, float v) { return r.Bottom() <= v; });
    for (; it != slots_.end() && it->y <= ry; ++it)
        if (it->w > 0 && it->Contains(rx, ry)) return static_cast<int>(it - slots_.begin());
    return -1;
}

ScrollView* ItemGrid::Scroller() const {
    for (Widget* w = Parent(); w; w = w->Parent())
        if (auto* s = dynamic_cast<ScrollView*>(w)) return s;
    return nullptr;
}

void ItemGrid::Select(int index, bool scrollIntoView) {
    if (index >= 0 && !Selectable(index)) return;
    const bool changed = index != selected_;
    selected_ = index;
    if (index >= 0 && scrollIntoView)
        if (ScrollView* s = Scroller()) {
            Rect r = ItemRect(index);
            // Keep a group header visible above the first item of the group.
            if (index > 0 && entries_[static_cast<size_t>(index - 1)].kind == EntryKind::Header)
                r = {r.x, ItemRect(index - 1).y, r.w, r.Bottom() - ItemRect(index - 1).y};
            s->ScrollIntoView(r);
        }
    if (changed && onSelectionChanged) onSelectionChanged(index);
    if (changed && IsFocused()) ui::a11y::FocusChanged(GetHost());
    Invalidate();
}

// --- painting ------------------------------------------------------------------------------

void ItemGrid::PaintThumb(Painter& p, const Entry& e, const Rect& t, float radius, int iconSize, float opacity) {
    const Palette& c = P();
    ID2D1Bitmap* bmp = nullptr;
    if (!e.image.empty()) {
        Host* host = GetHost();
        const float scale = host ? host->Scale() : 1.0f;
        // A video's own thumbnail gives way to its TMDB still or backdrop once downloaded
        // (Settings › Privacy; a frame where playback stopped stays).
        std::wstring image = e.image;
        // A damaged file is not the film its name says: no lookup, no artwork (V7-02).
        if (e.kind == EntryKind::Media && !e.audio && !e.damaged && e.image == e.path)
            if (std::wstring online = net::online::CardImage(e.path); !online.empty()) image = std::move(online);
        bmp = TheApp().images().Get(image, static_cast<int>(std::ceil(std::max(t.w, t.h) * scale)));
    }
    const bool iconOnly = e.kind == EntryKind::Folder || e.kind == EntryKind::Playlist || e.kind == EntryKind::Category ||
                          e.kind == EntryKind::Artist || e.kind == EntryKind::Genre || e.kind == EntryKind::Stream;
    if (bmp) {
        if (roundImages && e.kind == EntryKind::Artist) {
            p.PushRoundClip(t, t.w / 2);
            p.Image(bmp, t, 0, true, opacity);
            p.PopClip();
        } else {
            p.Fill(t, c.bgDeep, radius);
            p.Image(bmp, t, radius, true, opacity);
        }
        return;
    }
    const float r = roundImages && e.kind == EntryKind::Artist ? t.w / 2 : radius;
    if (e.kind == EntryKind::Action) {
        p.Stroke(t, c.lineStrong, r);
    } else {
        p.Fill(t, c.bgCard, r);
    }
    std::string icon = e.icon;
    if (icon.empty()) icon = e.audio ? "music" : "video";
    const Color ic = iconOnly || e.kind == EntryKind::Action ? c.textSecondary : c.textTertiary;
    p.Icon(icon, t.Center(static_cast<float>(iconSize), static_cast<float>(iconSize)), ic.Fade(opacity), c.accentMid.Fade(opacity));
}

void ItemGrid::PaintCard(Painter& p, int i, const Rect& r, bool hot, bool now) {
    const Palette& c = P();
    const Entry& e = entries_[static_cast<size_t>(i)];
    Rect t{r.x, r.y, r.w, geo_.thumbH};
    if (e.kind == EntryKind::Action && geo_.cols == 1) {
        // "Add…" tiles have the size of the cards beside them; in one column they keep the
        // design's card size instead of filling a narrow page alone.
        const bool video = style_ == GridStyle::VideoCards;
        const float tileW = video ? kVideoMinW : kSquareMinW;
        if (t.w > tileW) t = {t.x, t.y, tileW, video ? std::round(tileW * kVideoRatio) : tileW};
    }
    const bool sel = i == selected_;
    const bool pressed = hot && IsPressed();
    const float dim = e.damaged ? 0.4f : e.watched ? 0.55f : 1.0f;

    PaintThumb(p, e, t, kCardRadius, 48, dim);

    // Progress line along the bottom edge, clipped by the rounded corners. On artwork, like the
    // "now playing" chip: white on a dark track in both themes, so a light frame can't hide it.
    if (e.progress > 0) {
        p.PushRoundClip(t, kCardRadius);
        p.Fill({t.x, t.Bottom() - 4, t.w, 4}, Color::Hex(0x000000, 0.55f));
        p.Fill({t.x, t.Bottom() - 4, std::round(t.w * static_cast<float>(std::min(1.0, e.progress))), 4}, Color::Hex(0xFFFFFF));
        p.PopClip();
    }

    // Badges: duration for media, item count for folders, the warning chip for damaged files.
    std::wstring badge;
    if (e.kind == EntryKind::Media && !e.audio && e.duration > 0) badge = FormatDuration(e.duration);
    if ((e.kind == EntryKind::Folder || e.kind == EntryKind::Playlist) && e.count > 0 && style_ == GridStyle::VideoCards)
        badge = std::to_wstring(e.count);
    if (e.damaged) {
        const float cw = std::min(DamagedChipWidth(p), t.w - 16);
        PaintDamagedChip(p, {t.Right() - 8 - cw, t.Bottom() - 12 - kChipH, cw, kChipH}, c.bgBase.Fade(0.82f));
    } else if (!badge.empty()) {
        const float tw = std::ceil(p.Measure(badge, TextStyle::MonoSmall).width);
        // One height for every card, clear of the 4-DIP progress line, so a row's badges line up.
        const float bh = 20, by = t.Bottom() - 12 - bh;
        const Rect b{t.Right() - 8 - tw - 12, by, tw + 12, bh};
        p.Fill(b, c.bgBase.Fade(0.82f), 4);
        p.Text(badge, b, TextStyle::MonoSmall, c.textPrimary, HAlign::Center, VAlign::Center, 0);
    }

    // Year and rating from TMDB, top right (the card's text lines keep their layout).
    if (e.kind == EntryKind::Media && !e.audio && !e.damaged && style_ == GridStyle::VideoCards)
        if (const net::online::MediaMeta* m = net::online::Meta(e.path)) {
            const std::wstring info = net::online::MetaSummary(*m);
            if (!info.empty() && t.w > 160) {
                const float tw = std::ceil(p.Measure(info, TextStyle::Small).width);
                const Rect b{t.Right() - 8 - tw - 12, t.y + 8, tw + 12, 20};
                p.Fill(b, c.bgBase.Fade(0.82f), 4);
                p.Text(info, b, TextStyle::Small, c.textPrimary, HAlign::Center, VAlign::Center, 0);
            }
        }

    if (e.favorite) {
        const Rect b{t.x + 8, t.y + 8, 24, 24};
        p.Fill(b, c.bgBase.Fade(0.82f), 12);
        p.Icon("favorite_active", b.Center(14, 14), c.textPrimary);
    }
    // Top left, after the heart; a damaged file shows only its warning.
    if (e.isNew && !e.damaged) {
        const float x = t.x + 8 + (e.favorite ? 24 + 6 : 0);
        const float cw = std::min(NewChipWidth(p), t.Right() - 8 - x);
        if (cw > 16) PaintNewChip(p, {x, t.y + 10, cw, kChipH});
    }

    if (now) {
        // On artwork: a fixed dark chip with a white glyph, the same in both themes.
        const Color white = Color::Hex(0xFFFFFF);
        p.FillCircle({t.Cx(), t.Cy()}, 26, Color::Hex(0x000000, 0.62f));
        const bool paused = TheApp().player().State().paused;
        p.Icon(paused ? "pause" : "waveform_twotone", t.Center(paused ? 24.0f : 30.0f, paused ? 24.0f : 30.0f), white,
               white.Fade(0.6f));
    }
    if (hot) {
        p.Fill(t, pressed ? c.press : c.hover, kCardRadius);
        if ((e.kind == EntryKind::Media || (e.kind == EntryKind::Album && onPlay)) && !now) {
            // On artwork, like the "now playing" chip: the same in both themes (the light theme's
            // black accent vanished on a dark cover), with a shadow that keeps it on a white one.
            const float rad = kPlayD / 2 + (hoverPlay_ ? 2.0f : 0.0f);
            p.Shadow(t.Center(rad * 2, rad * 2), rad, 10, 2, 0.35f);
            p.FillCircle({t.Cx(), t.Cy()}, rad, Color::Hex(0xFFFFFF, hoverPlay_ ? 1.0f : 0.94f));
            p.Icon("play", t.Center(24, 24), Color::Hex(0x000000));
        }
    }
    if (sel) {
        const bool focused = IsFocused();
        p.Stroke(t.Inset(-3), focused ? c.accentMid : c.lineStrong, kCardRadius + 3, focused ? 2.0f : 1.5f);
    }

    const Color fg = now ? c.accent : (hot || sel) ? c.textPrimary : (e.watched || e.damaged) ? c.textTertiary : c.textSecondary;
    const float ly = t.Bottom() + kLabelGap;
    ItemText(p, i, e.title, {r.x, ly - 2, r.w, kLineH + 4}, TextStyle::Large, fg);
    if (!e.subtitle.empty())
        ItemText(p, i, e.subtitle, {r.x, ly + kLineH + kSubGap - 2, r.w, kLineH + 4}, TextStyle::Large, c.textTertiary);
}

Rect ItemGrid::MoreButtonRect(const Rect& slot) const {
    return {slot.Right() - 28, slot.Cy() - 12, 24, 24};
}

bool ItemGrid::OnPlayButton(int i, float x, float y) const {
    if (!IsCardStyle(style_) || !Selectable(i)) return false;
    const EntryKind kind = entries_[static_cast<size_t>(i)].kind;
    if (kind != EntryKind::Media && !(kind == EntryKind::Album && onPlay)) return false;
    // The playing card shows its state there, not a ▶.
    if (SamePath(entries_[static_cast<size_t>(i)].path, NowPlayingPath())) return false;
    const Rect r = ItemRect(i);
    const float dx = x - r.Cx(), dy = y - (r.y + geo_.thumbH / 2), reach = kPlayD / 2 + 4;
    return dx * dx + dy * dy <= reach * reach;
}

void ItemGrid::PaintRow(Painter& p, int i, const Rect& r, bool hot, bool now) {
    const Palette& c = P();
    const Entry& e = entries_[static_cast<size_t>(i)];
    const bool sel = i == selected_;
    const bool compact = style_ == GridStyle::CompactRows;
    const Rect bg{r.x - 8, r.y + 1, r.w + 16, r.h - 2};
    // Selection keeps the full selected tone even without focus, so it never looks like hover.
    if (sel)
        p.Fill(bg, c.bgSelected, 6);
    else if (hot)
        p.Fill(bg, IsPressed() ? c.press : c.hover, 6);
    if (sel && ShowFocusRing()) p.Stroke(bg, c.accentMid, 6, 1.5f);

    const bool dragSource = dragging_ && i == press_;
    if (dragSource) p.PushOpacity(0.35f);

    const bool compactRow = style_ == GridStyle::CompactRows;
    const RowCells cells = MediaCells(r);
    const Rect t = compactRow ? Rect{r.x, r.Cy() - kThumb / 2, kThumb, kThumb} : cells.thumb;
    // The playing track shows the compact list's ▶ in place of its number.
    if (!compactRow && (columns & kColTrack) && now && t.w <= 0)
        p.Icon("play", {cells.track.Right() - 18, r.Cy() - 9, 18, 18}, c.accent);
    else if (!compactRow && (columns & kColTrack) && e.track > 0)
        p.Text(std::to_wstring(e.track), cells.track, TextStyle::Mono, now ? c.accent : c.textTertiary, HAlign::Trailing);
    const bool iconRow = e.kind != EntryKind::Media && e.kind != EntryKind::Album && e.image.empty();
    const float dim = e.damaged ? 0.4f : e.watched ? 0.55f : 1.0f;
    if (t.w <= 0) {
        // No artwork column: the track number (accent while playing) carries the row.
    } else if (iconRow) {
        std::string icon = e.icon.empty() ? "folder" : e.icon;
        const Color ic = hot || sel ? c.textPrimary : c.textSecondary;
        p.Icon(icon, t.Center(24, 24), ic, c.accentMid);
    } else {
        PaintThumb(p, e, t, 2, 18, dim);
        if (e.progress > 0)
            p.Fill({t.x, t.Bottom() - 2, std::round(t.w * static_cast<float>(std::min(1.0, e.progress))), 2}, c.accent);
        if (now) {
            p.Fill(t, c.scrim, 2);
            p.Icon("play", t.Center(18, 18), c.accent);
        }
    }

    const Color fg = now ? c.accent
                     : e.kind == EntryKind::Action ? (hot ? c.textPrimary : c.textTertiary)
                     : (hot || sel)                ? c.textPrimary
                     : (e.watched || e.damaged)    ? c.textTertiary
                                                   : c.textSecondary;
    float x = compactRow ? t.Right() + 10 : cells.title.x;
    float right = r.Right();
    const bool menu = e.kind != EntryKind::Action && e.kind != EntryKind::Category && onMenu;
    if (menu && (hot || sel)) {
        const Rect mb = MoreButtonRect(r);
        if (hot && hoverMore_) p.Fill(mb.Inset(-4), c.hover, 4);
        p.Icon("more", mb, hot && hoverMore_ ? c.textPrimary : c.textSecondary);
    }
    // Every row of a list with menus keeps the "⋯" slot, so counts and columns end at one edge.
    if (onMenu) right -= 36;
    // List rows always keep the heart slot: the columns don't jump when the first favourite is
    // added, and counts and columns end at the same edge on every page (folders, streams, songs).
    if (!compact || e.favorite) {
        if (e.favorite) p.Icon("favorite_active", {right - 16, r.Cy() - 8, 16, 16}, c.textSecondary);
        right -= 28;
    }

    const TextStyle body = TextStyle::Body;
    auto cell = [&](const std::wstring& text, float w, TextStyle st, HAlign h) {
        if (w <= 0) return;
        ItemText(p, i, text, {right - w, r.y, w, r.h}, st, c.textTertiary, h);
        right -= w + 16;
    };

    if (compact) {
        if ((columns & kColDuration) && e.duration > 0 && e.kind == EntryKind::Media)
            cell(FormatDuration(e.duration), 56, TextStyle::MonoSmall, HAlign::Trailing);
        if (e.count >= 0 && e.kind != EntryKind::Media) cell(std::to_wstring(e.count), 40, TextStyle::Small, HAlign::Trailing);
        const float avail = right - x;
        const float natural = p.Measure(e.title, body).width + 1;
        const float tw = std::min(avail, natural);
        ItemText(p, i, e.title, {x, r.y, tw, r.h}, body, fg);
        if (!e.subtitle.empty() && !(e.kind != EntryKind::Media && e.count >= 0)) {
            const std::wstring sub = L" · " + e.subtitle;
            if (avail - tw > 40)
                ItemText(p, i, sub, {x + tw, r.y, avail - tw, r.h}, body, c.textTertiary);
            else if (collectTip_ && i == hover_)
                tipCut_ = true;  // no room for the subtitle at all
        }
        if (dragSource) p.PopOpacity();
        return;
    }

    const float w = r.w;
    if (e.kind != EntryKind::Media) {
        // Folders, playlists, albums: one count at the right edge; the title column lines up with media rows.
        const float reserved = ((columns & kColDate) && w > 720 ? 112.0f : 0.0f) +
                               ((columns & kColSize) && w > 600 ? 96.0f : 0.0f) + ((columns & kColDuration) ? 80.0f : 0.0f);
        // Categories carry their own noun ("8 songs", "3 artists").
        const bool songs = e.kind == EntryKind::Album || e.kind == EntryKind::Artist || e.kind == EntryKind::Genre;
        const std::wstring count = e.kind == EntryKind::Category && !e.subtitle.empty() ? e.subtitle
                                   : songs                                              ? CountSongs(e.count)
                                                                                        : CountItems(e.count);
        if (e.count >= 0) p.Text(count, {right - 160, r.y, 160, r.h}, TextStyle::Body, c.textTertiary, HAlign::Trailing);
        right -= std::max(reserved, e.count >= 0 ? 176.0f : 0.0f);
    } else {
        if (columns & kColDate) {
            std::wstring d;
            if (e.date > 0) d = (columns & kColTime) && DayBucket(e.date) <= 1 ? FormatClock(e.date) : FormatDate(e.date);
            cell(d, w > 720 ? 96.0f : 0.0f, TextStyle::Mono, HAlign::Trailing);  // tabular digits
        }
        if (columns & kColSize)
            cell(e.size > 0 ? FormatSize(e.size) : std::wstring(), w > 600 ? 80.0f : 0.0f, TextStyle::Body, HAlign::Trailing);
        if (columns & kColDuration)
            cell(e.duration > 0 ? FormatDuration(e.duration) : std::wstring(), kDurationW, TextStyle::Mono, HAlign::Trailing);
    }

    const float flex = right - x;
    const unsigned shown = RowColumnsFor(flex);
    // Counted rows (folders, categories) already show their count on the right.
    const bool sub = (shown & kColSubtitle) != 0 && !(e.kind != EntryKind::Media && e.count >= 0);
    const bool extra = (shown & kColExtra) != 0;
    if (collectTip_ && i == hover_ && (((columns & kColSubtitle) && !sub && !e.subtitle.empty() && e.count < 0) ||
                                       ((columns & kColExtra) && !extra && !e.extra.empty())))
        tipCut_ = true;  // a dropped column hid this row's artist or album
    float titleW = flex;
    if (sub && extra) titleW = flex * 0.44f;
    else if (sub || extra) titleW = flex * 0.6f;
    if (e.damaged || e.isNew) {
        // The chip follows the title (shortened if needed), in the title's own column.
        const float cw = e.damaged ? DamagedChipWidth(p) : NewChipWidth(p);
        const float textW = std::min(std::ceil(p.Measure(e.title, body).width) + 1, std::max(0.0f, titleW - 16 - cw - 8));
        ItemText(p, i, e.title, {x, r.y, textW, r.h}, body, fg);
        const Rect chip{x + textW + 8, r.Cy() - kChipH / 2, cw, kChipH};
        if (titleW - 16 >= cw) e.damaged ? PaintDamagedChip(p, chip, c.bgCard) : PaintNewChip(p, chip);
    } else {
        ItemText(p, i, e.title, {x, r.y, titleW - 16, r.h}, body, fg);
    }
    float cx = x + titleW;
    const float rest = flex - titleW;
    if (sub) {
        const float sw = extra ? rest / 2 : rest;
        ItemText(p, i, e.subtitle, {cx, r.y, sw - 16, r.h}, body, c.textTertiary);
        cx += sw;
    }
    if (extra) ItemText(p, i, e.extra, {cx, r.y, flex - (cx - x) - 16, r.h}, body, c.textTertiary);
    if (dragSource) p.PopOpacity();
}

// Cells of a media row (and of the column titles above the rows). Mirrors PaintRow.
ItemGrid::RowCells ItemGrid::MediaCells(const Rect& r) const {
    RowCells cells;
    float x = r.x;
    if (columns & kColTrack) {
        cells.track = {x, r.y, kTrackW, r.h};
        x += kTrackW + 12;
    }
    if (thumbs) {
        cells.thumb = {x, r.Cy() - kThumb / 2, kThumb, kThumb};
        x += kThumb + 10;
    } else {
        cells.thumb = {x, r.Cy(), 0, 0};
    }
    float right = r.Right();
    if (onMenu) right -= 36;
    cells.heart = {right - 16, r.Cy() - 8, 16, 16};  // always reserved, see PaintRow
    right -= 28;
    const float w = r.w;
    auto take = [&](Rect& out, float cw) {
        if (cw <= 0) return;
        out = {right - cw, r.y, cw, r.h};
        right -= cw + 16;
    };
    if (columns & kColDate) take(cells.date, w > 720 ? 96.0f : 0.0f);
    if (columns & kColSize) take(cells.size, w > 600 ? 80.0f : 0.0f);
    if (columns & kColDuration) take(cells.duration, kDurationW);
    const float flex = right - x;
    const unsigned shown = RowColumnsFor(flex);
    const bool sub = (shown & kColSubtitle) != 0, extra = (shown & kColExtra) != 0;
    float titleW = flex;
    if (sub && extra) titleW = flex * 0.44f;
    else if (sub || extra) titleW = flex * 0.6f;
    cells.title = {x, r.y, titleW - 16, r.h};
    float cx = x + titleW;
    const float rest = flex - titleW;
    if (sub) {
        const float sw = extra ? rest / 2 : rest;
        cells.sub = {cx, r.y, sw - 16, r.h};
        cx += sw;
    }
    if (extra) cells.extra = {cx, r.y, flex - (cx - x) - 16, r.h};
    return cells;
}

unsigned ItemGrid::RowColumnsFor(float flex) const {
    unsigned c = columns;
    if (flex < kExtraMinFlex) c &= ~static_cast<unsigned>(kColExtra);
    if (flex < kSubMinFlex) c &= ~static_cast<unsigned>(kColSubtitle);
    return c;
}

int ItemGrid::HeadAt(int index, float x) const {
    if (index < 0 || entries_[static_cast<size_t>(index)].kind != EntryKind::Columns) return -1;
    const RowCells cells = MediaCells(ItemRect(index));
    const Rect* rects[kHeadCount] = {&cells.track, &cells.title, &cells.sub, &cells.extra,
                                     &cells.duration, &cells.size, &cells.date};
    for (int h = 0; h < kHeadCount; ++h) {
        const Rect& cr = *rects[h];
        if (cr.w <= 0 || heads[h].label.empty() || heads[h].sort.empty()) continue;
        // The title column also owns the thumbnail space before it.
        const float left = h == kHeadTitle ? cells.thumb.x : cr.x;
        if (x >= left - 4 && x < cr.Right() + 8) return h;
    }
    return -1;
}

void ItemGrid::PaintColumns(Painter& p, const Rect& r) {
    const Palette& c = P();
    const RowCells cells = MediaCells(r);
    const Rect* rects[kHeadCount] = {&cells.track, &cells.title, &cells.sub, &cells.extra,
                                     &cells.duration, &cells.size, &cells.date};
    const size_t colon = sort.find(':');
    const std::string base = colon == std::string::npos ? sort : sort.substr(0, colon);
    const bool desc = colon != std::string::npos;
    for (int h = 0; h < kHeadCount; ++h) {
        const Rect& cr = *rects[h];
        const ColumnTitle& head = heads[h];
        if (cr.w <= 0 || head.label.empty()) continue;
        const bool active = !head.sort.empty() && head.sort == base;
        const bool hot = h == hoverHead_ && IsHot();
        const Color col = hot ? c.textPrimary : active ? c.textSecondary : c.textTertiary;
        const bool trailing = h == kHeadTrack || h == kHeadDuration || h == kHeadSize || h == kHeadDate;
        const float tw = std::min(cr.w, std::ceil(p.Measure(head.label, TextStyle::SmallMedium).width));
        Rect text = cr;
        if (active && trailing) text.w -= 16;  // room for the arrow on the left of the right edge
        p.Text(head.label, text, TextStyle::SmallMedium, col, trailing ? HAlign::Trailing : HAlign::Leading);
        if (active) {
            const float ax = trailing ? text.Right() + 2 : cr.x + tw + 4;
            p.Icon(desc ? "arrow_down" : "arrow_up", {ax, r.Cy() - 7, 14, 14}, col);
        }
    }
    p.Fill({r.x, r.Bottom() - 1, r.w, 1}, c.line);  // the content's edges, like the page header
}

void ItemGrid::PaintHeader(Painter& p, int i, const Rect& r) {
    const Entry& e = entries_[static_cast<size_t>(i)];
    if (IsCardStyle(style_)) {
        p.Text(e.title, {r.x, r.Bottom() - 24 - 16, r.w, 24}, TextStyle::Section, P().textSecondary);
    } else {
        const float h = 22;
        p.Text(e.title, {r.x, r.Bottom() - h - 8, r.w, h}, TextStyle::BodyMedium, P().textSecondary);
        // The count ends where the rows' last column ends (before the "⋯" and heart slots).
        float right = r.Right();
        if (onMenu) right -= 36;
        if (style_ != GridStyle::CompactRows) right -= 28;
        if (!e.subtitle.empty())
            p.Text(e.subtitle, {r.x, r.Bottom() - h - 8, right - r.x, h}, TextStyle::Small, P().textTertiary, HAlign::Trailing);
    }
}

void ItemGrid::Paint(Painter& p) {
    Compute(rect.w);
    const Rect clip = p.Clip();  // copy: children / clips push onto the painter's stack
    const float top = clip.y - rect.y, bottom = clip.Bottom() - rect.y;
    if (IsHot() && GetHost() && !dragging_) {
        const Point m = GetHost()->MousePos();
        UpdateHover(m.x, m.y);
    }
    const std::wstring now = NowPlayingPath();
    const bool hotItem = IsHot() && hover_ >= 0 && !dragging_;
    collectTip_ = hotItem && (hover_ != tipIndex_ || version_ != tipVersion_ || rect.w != tipW_);
    tipCut_ = false;
    auto it = std::lower_bound(slots_.begin(), slots_.end(), top, [](const Rect& r, float v) { return r.Bottom() <= v; });
    for (; it != slots_.end() && it->y < bottom; ++it) {
        if (it->w <= 0) continue;
        const int i = static_cast<int>(it - slots_.begin());
        const Rect r = it->Offset(rect.x, rect.y);
        const Entry& e = entries_[static_cast<size_t>(i)];
        if (e.kind == EntryKind::Header) {
            PaintHeader(p, i, r);
            continue;
        }
        if (e.kind == EntryKind::Columns) {
            PaintColumns(p, r);
            continue;
        }
        const bool hot = i == hover_ && IsHot() && !dragging_;
        const bool playing = !now.empty() && e.kind == EntryKind::Media && SamePath(e.path, now);
        if (IsCardStyle(style_))
            PaintCard(p, i, r, hot, playing);
        else
            PaintRow(p, i, r, hot, playing);
    }
    if (dragging_ && dropAt_ >= 0) {
        float y;
        if (dropAt_ < static_cast<int>(slots_.size()))
            y = slots_[static_cast<size_t>(dropAt_)].y;
        else
            y = height_;
        p.Fill({rect.x - 8, rect.y + y - 1, rect.w + 16, 2}, P().accent, 1);
    }
    if (collectTip_) {
        // The full title, subtitle and album of an item whose text was cut, shown by the host's
        // tooltip after the usual hover delay.
        collectTip_ = false;
        tipIndex_ = hover_;
        tipVersion_ = version_;
        tipW_ = rect.w;
        std::wstring tip;
        if (tipCut_) {
            const Entry& e = entries_[static_cast<size_t>(hover_)];
            tip = e.title;
            for (const std::wstring* part : {&e.subtitle, &e.extra})
                if (!part->empty() && *part != e.title) tip += L"\n" + *part;
        }
        tooltip = std::move(tip);
    } else if (!hotItem && !tooltip.empty()) {
        ResetTip();
    }
}

void ItemGrid::ItemText(Painter& p, int i, const std::wstring& text, const Rect& box, TextStyle style, Color c, HAlign h) {
    p.Text(text, box, style, c, h);
    if (collectTip_ && i == hover_ && !text.empty() && (box.w <= 0 || p.Measure(text, style).width > box.w + 0.5f))
        tipCut_ = true;
}

void ItemGrid::ResetTip() {
    tooltip.clear();
    tipIndex_ = -2;
}

// --- input -----------------------------------------------------------------------------------

void ItemGrid::UpdateHover(float x, float y) {
    const int i = IndexAt(x, y);
    const int head = HeadAt(i, x);
    if (head != hoverHead_) {
        hoverHead_ = head;
        Invalidate();
    }
    if (head >= 0) {
        cursor = IDC_HAND;
        if (hover_ != -1) {
            hover_ = -1;
            ResetTip();
            Invalidate();
        }
        return;
    }
    const int h = Selectable(i) ? i : -1;
    bool more = false;
    if (h >= 0 && !IsCardStyle(style_)) more = MoreButtonRect(ItemRect(h)).Inset(-4).Contains(x, y);
    const bool play = OnPlayButton(h, x, y);
    if (h != hover_ || more != hoverMore_ || play != hoverPlay_) {
        if (h != hover_) ResetTip();  // worked out again when the new item is painted
        hover_ = h;
        hoverMore_ = more;
        hoverPlay_ = play;
        // Whatever acts on a single click gets the hand, like a link.
        const bool link = h >= 0 && (OpensOnClick(entries_[static_cast<size_t>(h)].kind) || play);
        cursor = link ? IDC_HAND : IDC_ARROW;
        Invalidate();
    }
}

bool ItemGrid::OnMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    const int i = IndexAt(e.x, e.y);
    press_ = -1;
    dragging_ = false;
    if (!JustOpened() && onSort) {
        const int head = HeadAt(i, e.x);
        if (head >= 0) {
            const std::string& key = heads[head].sort;
            onSort(sort == key ? key + ":desc" : key);  // rebuilds the entries, not this grid
            return true;
        }
    }
    if (JustOpened() || !Selectable(i)) return true;
    Select(i, false);
    press_ = i;
    pressAt_ = {e.x, e.y};
    pressPlay_ = OnPlayButton(i, e.x, e.y);
    pressMods_ = e.mods.ctrl || e.mods.shift;
    const Entry& en = entries_[static_cast<size_t>(i)];
    if (!IsCardStyle(style_) && onMenu && en.kind != EntryKind::Action && en.kind != EntryKind::Category &&
        MoreButtonRect(ItemRect(i)).Inset(-4).Contains(e.x, e.y)) {
        const Rect mb = MoreButtonRect(ItemRect(i));
        press_ = -1;
        onMenu(i, {mb.x, mb.Bottom()});
    }
    return true;
}

int ItemGrid::DropIndexAt(float y) const {
    const float ry = y - rect.y;
    for (size_t i = 0; i < slots_.size(); ++i) {
        const Rect& s = slots_[i];
        if (s.w <= 0) continue;
        if (ry < s.Cy()) return static_cast<int>(i);
    }
    return static_cast<int>(slots_.size());
}

bool ItemGrid::OnMouseMove(const MouseEvent& e) {
    if (press_ >= 0 && reorderable && !IsCardStyle(style_) && onMove) {
        const float dx = e.x - pressAt_.x, dy = e.y - pressAt_.y;
        if (!dragging_ && dx * dx + dy * dy > 36) dragging_ = true;
        if (dragging_) {
            dropAt_ = DropIndexAt(e.y);
            if (ScrollView* s = Scroller()) {
                if (e.y < s->rect.y + 24) s->ScrollTo(s->Offset() - 24, false);
                else if (e.y > s->rect.Bottom() - 24) s->ScrollTo(s->Offset() + 24, false);
            }
            Invalidate();
            return true;
        }
    }
    UpdateHover(e.x, e.y);
    return true;
}

bool ItemGrid::OnMouseUp(const MouseEvent&) {
    if (dragging_) {
        const int from = press_;
        int to = dropAt_ > from ? dropAt_ - 1 : dropAt_;
        dragging_ = false;
        dropAt_ = -1;
        press_ = -1;
        if (from >= 0 && to >= 0 && to != from && onMove) {
            const int last = static_cast<int>(entries_.size()) - 1;
            to = std::min(to, last);
            while (to < last && !Selectable(to)) ++to;  // above the column titles = before the first item
            if (to != from) onMove(from, to);
        }
        Invalidate();
        return true;
    }
    return true;
}

void ItemGrid::OnClick() {
    const int i = press_;
    press_ = -1;
    if (JustOpened() || !Selectable(i)) return;
    const bool play = pressPlay_;
    pressPlay_ = false;
    if (pressMods_) return;  // Ctrl/Shift+click only selects: it must not open or play
    if (play && onPlay && entries_[static_cast<size_t>(i)].kind != EntryKind::Media) onPlay(i);  // may destroy this grid
    else if ((OpensOnClick(entries_[static_cast<size_t>(i)].kind) || play) && onActivate) onActivate(i);  // may destroy this grid
}

bool ItemGrid::OnDoubleClick(const MouseEvent& e) {
    const int i = IndexAt(e.x, e.y);
    if (HeadAt(i, e.x) >= 0) return OnMouseDown(e);  // a quick second click on a column title reverses again
    if (JustOpened() || !Selectable(i)) return true;
    Select(i, false);
    // Already ran on the first click (a container, or the ▶ of a card).
    if (OpensOnClick(entries_[static_cast<size_t>(i)].kind) || OnPlayButton(i, e.x, e.y)) return true;
    if (onActivate) onActivate(i);
    return true;
}

void ItemGrid::OnMouseLeave() {
    hover_ = -1;
    ResetTip();
    hoverHead_ = -1;
    hoverMore_ = false;
    hoverPlay_ = false;
    Invalidate();
}

void ItemGrid::OnFocusChanged(bool focused) {
    // The page was opened with Enter: its slots aren't laid out yet, select after layout.
    if (focused && selected_ < 0 && GetTickCount64() - g_keyActivateTick < 1000) {
        g_keyActivateTick = 0;
        keyFocusPending_ = true;
        InvalidateLayout();
    }
    if (focused && selected_ < 0) {
        // Keyboard users land on the first item.
        Host* h = GetHost();
        if (h && h->FocusVisible())
            for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
                if (Selectable(i)) {
                    Select(i, true);
                    break;
                }
    }
    Invalidate();
}

int ItemGrid::Navigate(int from, int dx, int dy) const {
    const int n = static_cast<int>(entries_.size());
    if (from < 0 || !Selectable(from)) {
        for (int i = 0; i < n; ++i)
            if (Selectable(i)) return i;
        return -1;
    }
    if (dx != 0) {
        for (int i = from + dx; i >= 0 && i < n; i += dx)
            if (Selectable(i)) return i;
        return from;
    }
    const Rect cur = slots_[static_cast<size_t>(from)];
    int best = from;
    float bestDy = kInf, bestDx = kInf;
    for (int i = 0; i < n; ++i) {
        if (!Selectable(i)) continue;
        const Rect& s = slots_[static_cast<size_t>(i)];
        const float d = s.y - cur.y;
        if (dy > 0 ? d <= 1 : d >= -1) continue;
        const float ady = std::fabs(d), adx = std::fabs(s.Cx() - cur.Cx());
        if (ady < bestDy - 0.5f || (std::fabs(ady - bestDy) <= 0.5f && adx < bestDx)) {
            best = i;
            bestDy = ady;
            bestDx = adx;
        }
    }
    return best;
}

bool ItemGrid::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_BACK) {
        // Library back, like a file browser. Posted: going back destroys this page.
        if (e.mods.ctrl || e.mods.alt || e.mods.shift || !TheApp().CanGoBack()) return false;
        if (Host* h = GetHost()) h->Post([] { TheApp().GoBack(); });
        return true;
    }
    const int n = static_cast<int>(entries_.size());
    if (n == 0) return false;
    const bool cards = IsCardStyle(style_);
    int target = -2;
    switch (e.key) {
        case VK_UP: target = Navigate(selected_, 0, -1); break;
        case VK_DOWN: target = Navigate(selected_, 0, 1); break;
        case VK_LEFT:
            if (!cards) return false;
            target = Navigate(selected_, -1, 0);
            break;
        case VK_RIGHT:
            if (!cards) return false;
            target = Navigate(selected_, 1, 0);
            break;
        case VK_HOME:
            for (int i = 0; i < n; ++i)
                if (Selectable(i)) {
                    target = i;
                    break;
                }
            break;
        case VK_END:
            for (int i = n - 1; i >= 0; --i)
                if (Selectable(i)) {
                    target = i;
                    break;
                }
            break;
        case VK_PRIOR:
        case VK_NEXT: {
            ScrollView* s = Scroller();
            const float page = s ? std::max(60.0f, s->rect.h - 60) : 400.0f;
            int cur = selected_ >= 0 ? selected_ : Navigate(-1, 0, 0);
            if (cur < 0) return true;
            const float goal = slots_[static_cast<size_t>(cur)].y + (e.key == VK_NEXT ? page : -page);
            target = cur;
            for (int k = 0; k < 10000; ++k) {
                const int nx = Navigate(target, 0, e.key == VK_NEXT ? 1 : -1);
                if (nx == target) break;
                if (e.key == VK_NEXT ? slots_[static_cast<size_t>(nx)].y > goal : slots_[static_cast<size_t>(nx)].y < goal) break;
                target = nx;
            }
            if (target == cur) target = Navigate(cur, 0, e.key == VK_NEXT ? 1 : -1);
            break;
        }
        case VK_RETURN:
            if (JustOpened() || e.repeat) return true;  // the Enter that opened this page, or a held key
            if (selected_ < 0) return false;  // the page may have a default (Home resumes Continue Watching)
            if (onActivate) {
                g_keyActivateTick = GetTickCount64();
                onActivate(selected_);  // may destroy this grid (navigation)
            }
            return true;
        case VK_DELETE:
            if (selected_ >= 0 && onDelete) {
                onDelete(selected_);
                return true;
            }
            return false;
        case VK_APPS:
        case VK_F10:
            if (e.key == VK_F10 && !e.mods.shift) return false;
            if (selected_ >= 0 && onMenu) {
                const Rect r = ItemRect(selected_);
                onMenu(selected_, {r.x + std::min(r.w / 2, 120.0f), cards ? r.y + geo_.thumbH / 2 : r.Bottom()});
            }
            return true;
        default: return false;
    }
    if (target >= 0) Select(target, true);
    return true;
}

}  // namespace app::lib
