// Dialogs module: Keyboard Shortcuts, Open Network Stream, New Playlist and
// Media Information (design/ref: Keyboard_Shortcuts, Open_network_stream,
// new_playlist). Customize Player, Add Subtitles and the onboarding window
// live in their own files next to this one.
#include "app/Dialogs.h"
#include "ui/Host.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "gfx/Text.h"
#include "library/MediaTypes.h"
#include "net/Online.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <cwctype>
#include <set>

namespace app {

using namespace ui;
using namespace dlg;

void InitDialogs() {
    InstallSubtitleMemory();
    net::online::Init();
}

namespace {

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::iswspace(s[a])) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

Widget* Row(Widget* parent, float gap = 10) {
    auto* r = parent->Add<Widget>();
    r->dir = Dir::Row;
    r->align = Align::Center;
    r->width = Length::Grow();
    r->gap = gap;
    return r;
}

// Text block + a control at its right. The block is measured at the width it really gets, so
// a description that wraps to two lines makes the row two lines tall.
class SideRow : public Widget {
public:
    SideRow() {
        dir = Dir::Row;
        align = Align::Center;
        width = Length::Grow();
        gap = 12;
    }
    Size MeasureContent(float availW, float availH) override {
        if (Children().size() < 2) return FlexMeasure(availW, availH);
        const Size c = Children().back()->Measure(availW, availH);
        const Size t = Children().front()->Measure(std::max(1.0f, availW - pad.H() - c.w - gap), availH);
        return {availW, std::max(t.h, c.h) + pad.V()};
    }
    // The flex row would measure the block at the full width (one line) and cut its second line.
    void OnLayout() override {
        if (Children().size() < 2) return FlexLayout();
        const Rect in = rect.Inset(pad);
        Widget* control = Children().back().get();
        const Size c = control->Measure(in.w, in.h);
        const float tw = std::max(1.0f, in.w - c.w - gap);
        const Size t = Children().front()->Measure(tw, in.h);
        Children().front()->Arrange({in.x, in.y + std::round((in.h - t.h) / 2), tw, t.h});
        control->Arrange({in.Right() - c.w, in.y + std::round((in.h - c.h) / 2), c.w, c.h});
    }
};

// =============================================================================================
// Keyboard Shortcuts
// =============================================================================================

// Left inset of the key text and the room of the icon at the right; the column headers use
// them too, so the table lines up.
constexpr float kKeyInset = 16, kKeyIconRoom = 44;

// "P", "PgUp, Media Next": the slot's bindings as the Settings editor writes them.
std::wstring JoinKeys(const std::vector<std::pair<UINT, Mods>>& keys, size_t from, size_t to) {
    std::wstring s;
    for (size_t i = from; i < std::min(to, keys.size()); ++i)
        if (keys[i].first != 0) s += (s.empty() ? L"" : L", ") + KeyText(keys[i].first, keys[i].second);
    return s;
}

// Column widths of the shortcut table: the keys narrow (270 -> 120) and the gap between them
// shrinks before the action name drops below 200 DIP.
void KeyColumns(float w, float* keyW, float* gapW) {
    *gapW = std::clamp(w - 2 * 270.0f - 300.0f, 16.0f, 89.0f);
    *keyW = std::clamp(std::floor((w - 200.0f - *gapW) / 2), 120.0f, 270.0f);
}

// Action | first key | other keys, laid out with KeyColumns.
class KeyTableRow : public Widget {
public:
    explicit KeyTableRow(float h) { height = Length::Px(h); width = Length::Grow(); }
    Size MeasureContent(float availW, float) override { return {availW, height.value}; }
    void OnLayout() override {
        if (Children().size() < 3) return;
        float keyW = 0, gapW = 0;
        KeyColumns(rect.w, &keyW, &gapW);
        const float labelW = std::max(0.0f, rect.w - 2 * keyW - gapW);
        Children()[0]->Arrange({rect.x, rect.y, labelW, rect.h});
        Children()[1]->Arrange({rect.x + labelW, rect.y, keyW, rect.h});
        Children()[2]->Arrange({rect.Right() - keyW, rect.y, keyW, rect.h});
    }
};

// Column title over a KeyField, centred over the key text (not over the icon).
class KeyHeader : public Label {
public:
    explicit KeyHeader(std::wstring text) : Label(std::move(text), TextStyle::Body, Tone::Tertiary) {}
    void Paint(Painter& p) override {
        p.Text(Text(), rect.Inset(Insets{kKeyInset, 0, kKeyIconRoom, 0}), style, ToneColor(tone), halign, valign);
    }
};

// Read-only key field: dark pill, the keys centred as in the Settings editor, "—" for an empty
// slot, and an "open" arrow: a click opens the editor in Settings.
class KeyField : public Widget {
public:
    KeyField(std::wstring text, std::function<void()> edit) : text_(std::move(text)) {
        interactive = true;
        height = Length::Px(33);
        tooltip = text_.empty() ? std::wstring(Tr(L"Edit in Settings")) : text_ + L"\n" + Tr(L"Edit in Settings");
        onClick = std::move(edit);
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, IsHot() ? Color::Mix(c.bgDeep, c.bgSelected, 0.35f) : c.bgDeep, 6);
        const Rect box{rect.x + kKeyInset, rect.y, rect.w - kKeyInset - kKeyIconRoom, rect.h};
        if (text_.empty())
            p.Text(L"—", box, TextStyle::Body, c.textDisabled, HAlign::Center);
        else
            p.Text(text_, box, TextStyle::Mono, SoftText(), HAlign::Center);
        p.Icon("open", {rect.Right() - 29, rect.Cy() - 12, 24, 24}, IsHot() ? c.textPrimary : c.textTertiary);
    }
    // "Stop: S", not a bare "S": the action is in another column.
    void Describe(A11y& a) override { a.name = action + L": " + (text_.empty() ? L"—" : text_); }
    std::wstring action;

private:
    std::wstring text_;
};

class ShortcutsList : public Widget {
public:
    ShortcutsList(Frame* frame) : frame_(frame) {
        dir = Dir::Column;
        width = Length::Grow();
        pad = {0, 0, 0, 20};
    }

    // mode: 0 all fields, 1 action names, 2 keys.
    void Rebuild(const std::wstring& query, int mode) {
        ClearChildren();
        const std::wstring q = library::SearchFold(Trim(query));
        std::vector<std::wstring> groups;
        for (const ActionInfo& a : AllActions()) {
            if (a.action == Action::None || !a.group || !*a.group) continue;
            if (std::find(groups.begin(), groups.end(), a.group) == groups.end()) groups.push_back(a.group);
        }
        int shown = 0;
        for (const std::wstring& g : groups) {
            Widget* section = nullptr;
            for (const ActionInfo& a : AllActions()) {
                if (a.action == Action::None || g != a.group) continue;
                const auto& keys = Bindings(a.action);
                // Every binding: the first slot, then all the others (as Settings shows them).
                const std::wstring k1 = JoinKeys(keys, 0, 1);
                const std::wstring k2 = JoinKeys(keys, 1, keys.size());
                const std::wstring label = Tr(a.label);
                if (!q.empty()) {
                    const bool byName = library::SearchFold(label).find(q) != std::wstring::npos ||
                                        library::SearchFold(a.label).find(q) != std::wstring::npos;
                    bool byKey = false;
                    // Translated key names ("Пробел") and the English ones ("Space") both match.
                    for (const auto& [vk, mods] : keys)
                        if (library::SearchFold(KeyText(vk, mods)).find(q) != std::wstring::npos ||
                            library::SearchFold(KeyText(vk, mods, false)).find(q) != std::wstring::npos)
                            byKey = true;
                    if (!(mode == 1 ? byName : mode == 2 ? byKey : byName || byKey)) continue;
                }
                if (!section) {
                    if (shown > 0) {
                        auto* gapW = Add<Widget>();
                        gapW->height = Length::Px(24);
                        AddSeparator(this);
                    }
                    // Group title: clearly above its rows, with more air before than after
                    // (spacers: a Label centers its text inside its padding).
                    Add<Widget>()->height = Length::Px(shown > 0 ? 28.0f : 18.0f);
                    auto* h = Add<Label>(Tr(g.c_str()), TextStyle::Heading);
                    h->color = SoftText;
                    Add<Widget>()->height = Length::Px(12);
                    section = Add<Widget>();
                    section->dir = Dir::Column;
                    section->width = Length::Grow();
                    section->gap = 14;
                }
                auto* row = section->Add<KeyTableRow>(33.0f);
                auto* l = row->Add<Label>(label, TextStyle::Body);
                l->color = SoftText;
                if (gfx::Text::Get().Measure(label, TextStyle::Body).width > 200) l->tooltip = label;
                Frame* f = frame_;
                auto edit = [f] {
                    f->Close();
                    TheApp().OpenSettings("controls");
                };
                row->Add<KeyField>(k1, edit)->action = label;
                row->Add<KeyField>(k2, edit)->action = label;
                ++shown;
            }
        }
        if (shown == 0) {
            auto* l = Add<Label>(Tr(L"No shortcuts match your search."), TextStyle::Body, Tone::Tertiary);
            l->pad = {0, 24, 0, 0};
        }
        InvalidateLayout();
    }

private:
    Frame* frame_;
};

// =============================================================================================
// Open Network Stream
// =============================================================================================

bool LooksLikeUrl(const std::wstring& s) {
    const size_t sep = s.find(L"://");
    if (sep == std::wstring::npos || sep == 0 || sep + 3 >= s.size()) return false;
    if (!std::iswalpha(s[0])) return false;
    for (size_t i = 1; i < sep; ++i)
        if (!std::iswalnum(s[i]) && s[i] != L'+' && s[i] != L'-' && s[i] != L'.') return false;
    for (wchar_t ch : s)
        if (std::iswspace(ch)) return false;
    return true;
}

// A page on a video site rather than a stream: mpv plays it only through yt-dlp.
bool IsSiteLink(const std::wstring& url) {
    std::wstring u = url;
    for (auto& ch : u) ch = static_cast<wchar_t>(towlower(ch));
    for (const wchar_t* h : {L"youtube.com/watch", L"youtube.com/shorts/", L"youtube.com/live/", L"youtube.com/playlist",
                             L"youtu.be/", L"twitch.tv/", L"vimeo.com/", L"soundcloud.com/", L"dailymotion.com/video",
                             L"rutube.ru/video", L"vkvideo.ru/", L"vk.com/video", L"bilibili.com/video"})
        if (u.find(h) != std::wstring::npos) return true;
    return false;
}

// yt-dlp.exe found and Settings > Network > "Open web pages with yt-dlp" on (see BuildYtdl).
bool SiteLinksPlayable() {
    return !YtDlpPath().empty() && TheApp().config().GetBool("network.ytdl");
}

// Splits the text area into links. `badLine` receives the first invalid line (1-based) or 0.
std::vector<std::wstring> ParseLinks(const std::wstring& text, int* badLine) {
    std::vector<std::wstring> out;
    *badLine = 0;
    int line = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        ++line;
        std::wstring l = Trim(text.substr(start, end - start));
        if (!l.empty()) {
            // "www.site/..." and pasted "youtube.com/watch?v=..." come without a scheme.
            if (l.rfind(L"www.", 0) == 0 || (l.find(L"://") == std::wstring::npos && IsSiteLink(l)))
                l = L"https://" + l;
            if (LooksLikeUrl(l))
                out.push_back(l);
            else if (!*badLine)
                *badLine = line;
        }
        start = end + 1;
    }
    return out;
}

// Small pill with a recent URL; clicking it fills the field.
class Chip : public Widget {
public:
    explicit Chip(std::wstring text) : text_(std::move(text)) {
        interactive = true;
        focusable = true;
        height = Length::Px(28);
        tooltip = text_;
    }
    Size MeasureContent(float availW, float) override {
        const float w = gfx::Text::Get().Measure(text_, TextStyle::Small).width + 24;
        return {std::min({w, availW, 320.0f}), 28};
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, IsPressed() ? c.bgSelected : IsHot() ? Color::Mix(c.bgRaised, c.bgSelected, 0.6f) : c.bgRaised, 6);
        p.Text(text_, rect.Inset(Insets{12, 0, 12, 0}), TextStyle::Small, IsHot() ? c.textPrimary : c.textSecondary);
        PaintFocusRing(p, rect, 6);
    }

private:
    std::wstring text_;
};

// =============================================================================================
// New Playlist
// =============================================================================================

class CoverTile : public Widget {
public:
    CoverTile() {
        interactive = true;
        focusable = true;
        width = Length::Px(112);
        height = Length::Px(112);
        tooltip = Tr(L"Choose a thumbnail");
        conn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
        onClick = [this] {
            Host* h = GetHost();
            auto files = PickFiles(h ? h->Hwnd() : nullptr, false,
                                   {{Tr(L"Images"), L"*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.gif"}});
            if (!files.empty()) {
                path = files[0];
                Invalidate();
            }
        };
    }
    std::wstring path;
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, c.bgDeep, 6);
        ID2D1Bitmap* bmp = nullptr;
        if (!path.empty()) {
            const float s = GetHost() ? GetHost()->Scale() : 1.0f;
            bmp = TheApp().images().Get(path, static_cast<int>(112 * s));
        }
        if (bmp) {
            p.Image(bmp, rect.Inset(2), 5, true);
        } else {
            const Rect plus = rect.Center(30, 30);
            p.Fill(plus, IsHot() ? c.bgSelected : c.bgChrome, 5);
            p.Icon("add", plus.Inset(3), IsHot() ? c.textPrimary : c.textSecondary);
        }
        p.Stroke(rect, IsHot() ? c.line : c.bgPanel, 6, 2);
        PaintFocusRing(p, rect, 6);
    }

private:
    ScopedConnection conn_;
};

// Dashed drop zone listing the chosen files.
class FileDropZone : public Widget {
public:
    FileDropZone() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(186);
        onClick = [this] { Browse(); };
    }
    std::vector<std::wstring> files;
    std::function<void()> onChanged;

    void AddFiles(const std::vector<std::wstring>& paths) {
        std::set<std::wstring> have;
        for (const auto& f : files) have.insert(library::PathKey(f));
        for (const auto& p : paths)
            if (library::IsMediaPath(p) && have.insert(library::PathKey(p)).second) files.push_back(p);
        scroll_ = std::max(0.0f, std::min(scroll_, MaxScroll()));
        Invalidate();
        if (onChanged) onChanged();
    }
    void Browse() {
        Host* h = GetHost();
        AddFiles(PickFiles(h ? h->Hwnd() : nullptr, true,
                           {{Tr(L"Media files"),
                             L"*.mp4;*.mkv;*.avi;*.mov;*.webm;*.wmv;*.m4v;*.ts;*.flv;*.mp3;*.flac;*.m4a;*.ogg;"
                             L"*.opus;*.wav;*.aac;*.wma"},
                            {Tr(L"All files"), L"*.*"}}));
    }
    bool OnMouseMove(const MouseEvent& e) override {
        hoverRow_ = RowAt(e.x, e.y, &hoverRemove_);
        Invalidate();
        return true;
    }
    void OnMouseLeave() override {
        hoverRow_ = -1;
        Invalidate();
    }
    void OnClick() override {
        bool remove = false;
        const Point m = GetHost() ? GetHost()->MousePos() : Point{};
        const int row = RowAt(m.x, m.y, &remove);
        if (row >= 0 && remove) {
            files.erase(files.begin() + row);
            scroll_ = std::max(0.0f, std::min(scroll_, MaxScroll()));
            Invalidate();
            if (onChanged) onChanged();
            return;
        }
        Browse();
    }
    bool OnWheel(const WheelEvent& e) override {
        if (MaxScroll() <= 0) return false;
        scroll_ = std::clamp(scroll_ - e.delta * 3 * kRowH, 0.0f, MaxScroll());
        Invalidate();
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key == VK_DELETE && !files.empty()) {
            files.pop_back();
            Invalidate();
            if (onChanged) onChanged();
            return true;
        }
        return false;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, c.bgDeep, 6);
        DashedRect(p, rect, IsHot() ? c.lineStrong : c.line, 4, 4);
        PaintFocusRing(p, rect, 6);
        if (files.empty()) {
            p.Text(Tr(L"Drag files into the box or click here to select files."), {rect.x, rect.y + 52, rect.w, 24},
                   TextStyle::Body, IsHot() ? c.textTertiary : c.textDisabled, HAlign::Center);
            const Rect plus{rect.Cx() - 42, rect.y + 96, 84, 30};
            p.Fill(plus, IsHot() ? c.bgSelected : c.bgChrome, 5);
            p.Icon("add", plus.Center(24, 24), IsHot() ? c.textPrimary : c.textSecondary);
            return;
        }
        p.PushClip(rect.Inset(2));
        const Rect area = ListRect();
        const auto snap = TheApp().library().Current();  // songs by their title tag, as in the library
        for (size_t i = 0; i < files.size(); ++i) {
            const Rect r{area.x, area.y + i * kRowH - scroll_, area.w, kRowH};
            if (r.Bottom() < rect.y || r.y > rect.Bottom()) continue;
            const bool hot = static_cast<int>(i) == hoverRow_;
            if (hot) p.Fill(r, c.hover, 4);
            p.Icon(library::KindOf(files[i]) == library::MediaKind::Audio ? "music" : "video",
                   {r.x + 6, r.Cy() - 10, 20, 20}, c.textTertiary);
            const library::MediaItem* item = snap->FindItem(files[i]);
            p.Text(item ? item->DisplayTitle() : library::FileNameOf(files[i]), {r.x + 34, r.y, r.w - 74, r.h}, TextStyle::Body, SoftText());
            if (hot) p.Icon("close", {r.Right() - 28, r.Cy() - 10, 20, 20}, hoverRemove_ ? c.textPrimary : c.textTertiary);
        }
        p.PopClip();
        p.Text(Fmt(Tr(L"{0} selected · click to add more"), {std::to_wstring(files.size())}),
               {rect.x + 12, rect.Bottom() - 26, rect.w - 24, 20}, TextStyle::Small, c.textTertiary, HAlign::Trailing);
    }

private:
    static constexpr float kRowH = 30;
    Rect ListRect() const { return {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 40}; }
    float MaxScroll() const { return std::max(0.0f, files.size() * kRowH - ListRect().h); }
    int RowAt(float x, float y, bool* remove) const {
        *remove = false;
        const Rect a = ListRect();
        if (files.empty() || !a.Contains(x, y)) return -1;
        const int i = static_cast<int>((y - a.y + scroll_) / kRowH);
        if (i < 0 || i >= static_cast<int>(files.size())) return -1;
        *remove = x > a.Right() - 36;
        return i;
    }
    float scroll_ = 0;
    int hoverRow_ = -1;
    bool hoverRemove_ = false;
};

// =============================================================================================
// Media information
// =============================================================================================

struct InfoRow {
    std::wstring key, value;
};
struct InfoSection {
    std::wstring title;
    std::vector<InfoRow> rows;
};

// "Key   value" row: the value wraps (up to 3 lines) and the row grows with it; the key
// stays on the first line.
// Wrapped value capped at maxLines: the last line ends with "…" instead of running over the
// next rows (a file name with no spaces wraps at every character).
class CappedText : public Label {
public:
    using Label::Label;
    void Paint(Painter& p) override {
        p.Text(Text(), rect, style, color ? color() : ToneColor(tone), halign, valign, gfx::kTextWrap | gfx::kTextEllipsis);
    }
};

// Break opportunities after path separators, so a long path wraps at its folders first.
std::wstring BreakablePath(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t ch : s) {
        out.push_back(ch);
        if (ch == L'\\' || ch == L'/' || ch == L'_' || ch == L'.') out.push_back(L'\x200B');
    }
    return out;
}

class InfoLine : public Widget {
public:
    static constexpr float kKeyW = 210, kGap = 16;
    InfoLine(const std::wstring& key, const std::wstring& value) {
        width = Length::Grow();
        dir = Dir::None;
        clipChildren = true;
        key_ = Add<Label>(key, TextStyle::Body, Tone::Tertiary);
        key_->valign = VAlign::Top;
        value_ = Add<CappedText>(BreakablePath(value), TextStyle::Body);
        value_->color = SoftText;
        value_->wrap = true;
        value_->maxLines = 3;
        value_->valign = VAlign::Top;
        // The full value on hover: it may wrap past the three lines (paths, unbroken names).
        if (gfx::Text::Get().Measure(value, TextStyle::Body).width > 360) {
            value_->tooltip = value;
            value_->interactive = true;
        }
    }
    Size MeasureContent(float availW, float) override {
        const float lh = gfx::Text::Get().LineHeight(TextStyle::Body);
        return {availW, std::max(lh, value_->Measure(std::max(1.0f, availW - kKeyW - kGap), 1e6f).h)};
    }
    void OnLayout() override {
        const float lh = gfx::Text::Get().LineHeight(TextStyle::Body);
        key_->Arrange({rect.x, rect.y, kKeyW, lh});
        value_->Arrange({rect.x + kKeyW + kGap, rect.y, std::max(1.0f, rect.w - kKeyW - kGap), rect.h});
    }

private:
    Label* key_ = nullptr;
    Label* value_ = nullptr;
};

std::wstring Prop(const char* name) {
    return Utf8ToWide(TheApp().player().GetPropertyString(name));
}

// Decimal comma in Russian, as in the library sizes.
std::wstring Decimal(std::wstring s) {
    if (CurrentLanguage() == Language::Russian) std::replace(s.begin(), s.end(), L'.', L',');
    return s;
}

std::wstring Kbps(const std::wstring& bitsPerSecond) {
    if (bitsPerSecond.empty()) return {};
    const double v = _wtof(bitsPerSecond.c_str());
    if (v <= 0) return {};
    wchar_t buf[32];
    if (v >= 1e6)
        swprintf(buf, 32, L"%.1f %s", v / 1e6, Tr(L"Mbit/s"));
    else
        swprintf(buf, 32, L"%.0f %s", v / 1e3, Tr(L"kbit/s"));
    return Decimal(buf);
}

std::vector<InfoSection> CollectMediaInfo() {
    const PlayerState& s = TheApp().player().State();
    std::vector<InfoSection> out;
    auto add = [](InfoSection& sec, const wchar_t* key, std::wstring value) {
        if (!value.empty()) sec.rows.push_back({Tr(key), std::move(value)});
    };
    InfoSection general{Tr(L"General"), {}};
    add(general, L"Title", s.title);
    add(general, L"Path", s.path.empty() ? Prop("path") : s.path);
    const std::wstring size = Prop("file-size");
    if (!size.empty()) add(general, L"Size", FormatBytes(_wtof(size.c_str())));
    if (s.duration > 0) add(general, L"Duration", FormatTime(s.duration, s.duration >= 3600));
    add(general, L"Container", Prop("file-format"));
    if (!size.empty() && s.duration > 0)
        add(general, L"Overall bitrate", Kbps(std::to_wstring(_wtof(size.c_str()) * 8 / s.duration)));
    add(general, L"Artist", s.artist);
    add(general, L"Album", s.album);
    out.push_back(std::move(general));

    if (s.hasVideo || s.videoW > 0) {
        InfoSection video{Tr(L"Video"), {}};
        std::wstring codec = Prop("video-format");
        const std::wstring longCodec = Prop("video-codec");
        if (!longCodec.empty()) codec = longCodec;
        add(video, L"Codec", codec);
        if (s.videoW > 0) add(video, L"Resolution", std::to_wstring(s.videoW) + L" × " + std::to_wstring(s.videoH));
        add(video, L"Aspect ratio", Prop("video-params/aspect-name"));
        std::wstring fps = Prop("container-fps");
        if (fps.empty()) fps = Prop("estimated-vf-fps");
        if (!fps.empty()) {
            wchar_t buf[32];
            swprintf(buf, 32, L"%.3g", _wtof(fps.c_str()));
            add(video, L"Frame rate", Decimal(buf) + L" " + Tr(L"fps"));
        }
        add(video, L"Bitrate", Kbps(Prop("video-bitrate")));
        add(video, L"Pixel format", Prop("video-params/pixelformat"));
        const std::wstring prim = Prop("video-params/primaries"), gamma = Prop("video-params/gamma");
        if (!prim.empty() || !gamma.empty()) add(video, L"Color", prim + (gamma.empty() ? L"" : L" / " + gamma));
        add(video, L"HDR", s.hdr ? Tr(L"Yes") : Tr(L"No"));
        add(video, L"Hardware decoding", s.hwdec.empty() || s.hwdec == L"no" ? Tr(L"Off") : s.hwdec);
        out.push_back(std::move(video));
    }

    InfoSection audio{Tr(L"Audio"), {}};
    std::wstring acodec = Prop("audio-codec");
    if (acodec.empty()) acodec = Prop("audio-codec-name");
    add(audio, L"Codec", acodec);
    const std::wstring rate = Prop("audio-params/samplerate");
    if (!rate.empty()) add(audio, L"Sample rate", rate + L" " + Tr(L"Hz"));
    std::wstring ch = Prop("audio-params/hr-channels");
    if (ch.empty()) ch = Prop("audio-params/channel-count");
    add(audio, L"Channels", ch);
    add(audio, L"Bitrate", Kbps(Prop("audio-bitrate")));
    if (!audio.rows.empty()) out.push_back(std::move(audio));

    InfoSection tracks{Tr(L"Tracks"), {}};
    for (const Track& t : s.tracks) {
        const wchar_t* kind = t.type == L"video" ? L"Video" : t.type == L"audio" ? L"Audio" : L"Subtitles";
        std::wstring v = t.Label();
        if (!t.codec.empty() && v.find(t.codec) == std::wstring::npos) v += L" · " + t.codec;
        if (t.external) v += L" · " + std::wstring(Tr(L"external"));
        if (t.selected) v += L" · " + std::wstring(Tr(L"active"));
        tracks.rows.push_back({std::wstring(Tr(kind)) + L" #" + std::to_wstring(t.id), v});
    }
    if (!tracks.rows.empty()) out.push_back(std::move(tracks));
    return out;
}

// --- TMDB block of the info dialog ---------------------------------------------------------

// A poster from the image cache (2:3), or the placeholder until it is downloaded.
class PosterView : public Widget {
public:
    explicit PosterView(std::wstring video) : video_(std::move(video)) {
        width = Length::Px(104);
        height = Length::Px(156);
        conn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const std::wstring file = net::online::PosterImage(video_);
        ID2D1Bitmap* bmp = nullptr;
        if (!file.empty() && GetHost()) bmp = TheApp().images().Get(file, static_cast<int>(rect.h * GetHost()->Scale()));
        p.Fill(rect, c.bgCard, 6);
        if (bmp) p.Image(bmp, rect, 6, true);
        else p.Icon("video", rect.Center(32, 32), c.textTertiary, c.accentMid);
    }

private:
    std::wstring video_;
    ScopedConnection conn_;
};

// Poster, title, year, rating and overview from TMDB with "Find another match" and "Reset";
// rebuilt whenever the online records change (the lookup may finish while the dialog is open).
class MetaBlock : public Widget {
public:
    explicit MetaBlock(std::wstring video) : video_(std::move(video)) {
        dir = Dir::Column;
        width = Length::Grow();
        gap = 6;
        conn_ = Connect(net::online::Changed(), [this] {
            if (GetHost()) GetHost()->Post([this, alive = alive_] {
                if (*alive) Rebuild();
            });
        });
        Rebuild();
    }
    ~MetaBlock() override { *alive_ = false; }

private:
    void Rebuild() {
        ClearChildren();
        using net::online::MediaMeta;
        const MediaMeta* m = net::online::Meta(video_);
        auto actions = [this](Widget* parent, bool matched) {
            auto* row = parent->Add<Widget>();
            row->dir = Dir::Row;
            row->gap = 18;
            row->pad = {0, 4, 0, 0};
            const std::wstring video = video_;
            auto* other = row->Add<LinkButton>(matched ? Tr(L"Find another match") : Tr(L"Find a match"));
            other->id = "meta-rematch";
            other->trailingIcon = "search";
            other->onClick = [video] {
                if (MainWindow* w = TheApp().main())
                    w->Post([video] {
                        CloseTransientPopups(TheApp().main());
                        ShowMetadataMatchDialog(video);
                    });
            };
            if (matched) {
                auto* reset = row->Add<LinkButton>(Tr(L"Reset"));
                reset->id = "meta-reset";
                reset->tooltip = Tr(L"Forget this match and show the file's own thumbnail");
                reset->onClick = [video] { net::online::ResetMeta(video); };
            }
        };
        if (!net::online::MetadataToggle()) {
            visible = false;
            InvalidateLayout();
            return;
        }
        visible = true;
        if (!net::online::MetadataOn()) {
            auto* l = Add<Label>(net::online::MetadataKeyRejected()
                                     ? Tr(L"TMDB rejected the API key. Check it in Settings › Privacy › Online services.")
                                     : Tr(L"Paste your TMDB API key in Settings › Privacy › Online services to see posters and descriptions."),
                                 TextStyle::Body, Tone::Tertiary);
            l->wrap = true;
            l->width = Length::Grow();
            InvalidateLayout();
            return;
        }
        if (!m || m->state != MediaMeta::State::Matched) {
            const bool waiting = m && m->state == MediaMeta::State::Queued;
            auto* l = Add<Label>(waiting ? Tr(L"Looking this video up on TMDB…")
                                 : m && m->state == MediaMeta::State::Failed
                                     ? Tr(L"TMDB couldn't be reached. Lumen tries again next time.")
                                 : m && m->manual ? Tr(L"Match reset: the file shows its own thumbnail.")
                                                  : Tr(L"TMDB has nothing for this file name."),
                                 TextStyle::Body, Tone::Tertiary);
            l->wrap = true;
            l->width = Length::Grow();
            if (!waiting) actions(this, false);
            InvalidateLayout();
            return;
        }
        auto* row = Add<Widget>();
        row->dir = Dir::Row;
        row->width = Length::Grow();
        row->gap = 16;
        row->align = Align::Start;
        row->Add<PosterView>(video_);
        auto* col = row->Add<Widget>();
        col->dir = Dir::Column;
        col->width = Length::Grow();
        col->gap = 4;
        auto* title = col->Add<Label>(m->title, TextStyle::Large);
        title->color = SoftText;
        title->width = Length::Grow();
        title->tooltip = m->title;
        std::wstring line = net::online::MetaSummary(*m);
        if (m->tv) {
            wchar_t code[24];
            swprintf_s(code, L"S%02dE%02d", m->season, m->episode);
            line = std::wstring(code) + (m->episodeTitle.empty() ? L"" : L" · " + m->episodeTitle) +
                   (line.empty() ? L"" : L" · " + line);
        }
        if (!line.empty()) {
            auto* l = col->Add<Label>(line, TextStyle::Body, Tone::Tertiary);
            l->width = Length::Grow();
        }
        if (!m->overview.empty()) {
            auto* o = col->Add<CappedText>(m->overview, TextStyle::Body);
            o->color = SoftText;
            o->wrap = true;
            o->maxLines = 5;
            o->width = Length::Grow();
            o->tooltip = m->overview;
            o->interactive = true;
        }
        actions(col, true);
        auto* credit = col->Add<Label>(Tr(L"Data from TMDB"), TextStyle::Small, Tone::Tertiary);
        credit->pad = {0, 2, 0, 0};
        InvalidateLayout();
    }

    std::wstring video_;
    ScopedConnection conn_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

std::wstring InfoText(const std::vector<InfoSection>& sections) {
    std::wstring t;
    for (const auto& s : sections) {
        if (!t.empty()) t += L"\r\n";
        t += s.title + L"\r\n";
        for (const auto& r : s.rows) t += L"  " + r.key + L": " + r.value + L"\r\n";
    }
    return t;
}

}  // namespace

// =============================================================================================

void ShowKeyboardShortcutsDialog() {
    MainWindow* w = TheApp().main();
    if (!w) return;
    // A modal over the whole window: at the 720x440 minimum the list keeps most of the height.
    const bool shortWindow = w->ClientSize().h < 600;
    auto frame = std::make_unique<Frame>(Tr(L"Keyboard Shortcuts"), FitWidth(w, 1048),
                                         std::min(800.0f, std::floor(w->ClientSize().h - 24)));
    Frame* f = frame.get();
    auto* search = f->Toolbar()->Add<TextBox>(Tr(L"Search Keyboard Shortcuts..."), "search");
    search->width = Length::Grow();
    search->raised = true;
    auto* mode = f->Toolbar()->Add<FieldDropdown>(
        std::vector<Choice>{{Tr(L"All Fields"), "all"}, {Tr(L"Action"), "action"}, {Tr(L"Hotkey"), "key"}}, 0);
    mode->width = Length::Px(200);
    mode->height = Length::Px(34);
    mode->a11yName = Tr(L"Search in");

    Widget* body = f->Body();
    body->pad = {20, shortWindow ? 10.0f : 20.0f, 20, 0};
    body->gap = 0;
    // Same column titles and alignment as Settings > Controls ("Shortcut", centred keys).
    auto* head = body->Add<KeyTableRow>(25.0f);
    head->Add<Label>(Tr(L"Action"), TextStyle::Body, Tone::Tertiary);
    for (const wchar_t* t : {L"Shortcut", L"Alternative"}) {
        auto* h = head->Add<KeyHeader>(Tr(t));
        h->halign = HAlign::Center;
    }
    auto* gapW = body->Add<Widget>();
    gapW->height = Length::Px(shortWindow ? 8.0f : 20.0f);
    AddSeparator(body);

    auto* scroll = body->Add<ScrollView>();
    scroll->width = Length::Grow();
    scroll->height = Length::Grow();
    auto* list = scroll->SetContent<ShortcutsList>(f);
    list->Rebuild({}, 0);
    auto refresh = [list, search, mode, scroll] {
        list->Rebuild(search->Text(), mode->Selected());
        scroll->ScrollTo(0, false);
    };
    search->onChange = [refresh](const std::wstring&) { refresh(); };
    search->onEscape = [f, search] {
        if (search->Text().empty()) f->Close();
        else search->SetText({}, true);
    };
    mode->onChange = [refresh](int) { refresh(); };

    auto* edit = f->Footer()->Add<DButton>(Tr(L"Edit in Settings"), BtnStyle::Neutral);
    edit->onClick = [f] {
        f->Close();
        TheApp().OpenSettings("controls");
    };
    f->Footer()->Add<Spacer>();
    f->Footer()->Add<DButton>(Tr(L"Done"), BtnStyle::Accent)->onClick = [f] { f->Close(); };
    ShowFrame(w, std::move(frame), search);
}

namespace {

// A link worth offering from the clipboard: a stream protocol, a media/playlist file, or a
// known video site. Plain web pages are left alone.
bool LooksLikeMediaUrl(const std::wstring& url) {
    std::wstring u = url;
    for (auto& ch : u) ch = static_cast<wchar_t>(towlower(ch));
    for (const wchar_t* p : {L"rtsp://", L"rtmp://", L"mms://", L"udp://", L"srt://", L"rtp://"})
        if (u.rfind(p, 0) == 0) return true;
    for (const wchar_t* e : {L".m3u8", L".m3u", L".mp4", L".mkv", L".webm", L".mp3", L".aac", L".flac", L".ogg",
                             L".opus", L".ts", L".mpd", L".pls", L".mov", L".avi"})
        if (u.find(e) != std::wstring::npos) return true;
    return IsSiteLink(u);
}

}  // namespace

void ShowNetworkStreamDialog() {
    MainWindow* w = TheApp().main();
    if (!w) return;
    auto frame = std::make_unique<Frame>(Tr(L"Open Network Stream"), FitWidth(w, 1048));
    Frame* f = frame.get();
    Widget* body = f->Body();

    AddFieldHeader(body, Tr(L"Network URL"), Tr(L"You can enter multiple links to load, one per line."));
    auto* urls = body->Add<TextArea>(Tr(L"e.g., https://www.example.com/stream.avi"));
    urls->width = Length::Grow();
    urls->height = Length::Px(std::clamp(w->ClientSize().h - 600, 96.0f, 194.0f));
    // Neutral hint, or the validation error (info icon in the danger color, primary text).
    auto* hintRow = Row(body, 6);
    auto* hintIcon = hintRow->Add<IconView>("info", 16);
    hintIcon->color = [] { return P().danger; };
    hintIcon->width = Length::Px(16);
    hintIcon->height = Length::Px(16);
    hintIcon->visible = false;
    auto* hint = hintRow->Add<Label>(Tr(L"Enter opens the stream, Shift+Enter starts a new line."), TextStyle::Small,
                                     Tone::Tertiary);
    hint->width = Length::Grow();

    // Recent links from the history.
    std::vector<std::wstring> recent;
    for (const auto& e : TheApp().history().Entries()) {
        if (e.path.find(L"://") == std::wstring::npos) continue;
        if (std::find(recent.begin(), recent.end(), e.path) == recent.end()) recent.push_back(e.path);
        if (recent.size() >= 4) break;
    }
    if (!recent.empty()) {
        auto* row = Row(body, 8);
        row->Add<Label>(Tr(L"Recent"), TextStyle::Body, Tone::Tertiary)->pad = {0, 0, 6, 0};
        for (const auto& r : recent) {
            auto* chip = row->Add<Chip>(r);
            chip->onClick = [urls, r] { urls->SetText(r, true); };
        }
    }

    // One link just plays; several can be kept as a playlist, named only when that is asked for.
    auto* save = body->Add<Checkbox>(Tr(L"Save as playlist"));
    save->visible = false;
    auto* name = body->Add<TextBox>(Tr(L"Playlist name"));
    name->width = Length::Grow();
    name->height = Length::Px(37);
    name->visible = false;

    // Buffering applies to every stream, so it is a setting, not a choice in this dialog.
    auto* buffering = body->Add<LinkButton>(Tr(L"Buffering settings…"), TextStyle::Small);
    buffering->onClick = [] { TheApp().OpenSettings("network"); };

    f->Footer()->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [f] { f->Close(); };
    f->Footer()->Add<Spacer>();
    auto* open = f->Footer()->Add<DButton>(Tr(L"Open"), BtnStyle::Accent);
    open->enabled = false;

    auto validate = [body, urls, hint, hintIcon, open, save, name] {
        int bad = 0;
        const std::wstring text = urls->Text();
        const auto links = ParseLinks(text, &bad);
        const bool many = links.size() > 1;
        if (save->visible != many || name->visible != (many && save->Value())) {
            save->visible = many;
            name->visible = many && save->Value();
            body->InvalidateLayout();
        }
        open->enabled = bad == 0 && !links.empty() && !(name->visible && Trim(name->Text()).empty());
        // The line being typed isn't an error yet ("h", "http", "rtsp:/"): only once it is long
        // enough to have a scheme, or another line follows it.
        if (bad != 0) {
            const size_t lineStart = text.find_last_of(L'\n');
            const std::wstring last = Trim(lineStart == std::wstring::npos ? text : text.substr(lineStart + 1));
            const int lines = 1 + static_cast<int>(std::count(text.begin(), text.end(), L'\n'));
            if (bad == lines && last.find(L"://") == std::wstring::npos && last.size() < 10) bad = 0;
        }
        const bool site = std::any_of(links.begin(), links.end(), IsSiteLink);
        // Site links stay allowed without yt-dlp (some serve media directly), but say why they may fail.
        const bool warn = bad != 0 || (site && !SiteLinksPlayable());
        hintIcon->visible = warn;
        hint->tone = warn ? Tone::Primary : Tone::Tertiary;
        hint->InvalidateLayout();
        if (bad)
            hint->SetText(Fmt(Tr(L"Line {0} is not a link. Links look like https://, rtsp:// or udp://"),
                              {std::to_wstring(bad)}));
        else if (site && !SiteLinksPlayable())
            hint->SetText(Tr(L"Video site links need yt-dlp.exe next to Lumen.exe (Settings › Network)."));
        else if (site && links.size() == 1)
            hint->SetText(Tr(L"The video page opens through yt-dlp."));
        else if (links.size() > 1)
            hint->SetText(Fmt(Tr(L"{0} links will be opened as one queue."), {std::to_wstring(links.size())}));
        else
            hint->SetText(Tr(L"Enter opens the stream, Shift+Enter starts a new line."));
        open->Invalidate();
    };
    urls->onChange = [validate](const std::wstring&) { validate(); };
    name->onChange = [validate](const std::wstring&) { validate(); };
    save->onChange = [validate, name](bool on) {
        validate();
        if (Host* h = name->GetHost(); on && h) h->SetFocus(name);
    };
    auto submit = [f, urls, name] {
        int bad = 0;
        const auto links = ParseLinks(urls->Text(), &bad);
        if (bad || links.empty()) return;
        const std::wstring listName = name->visible ? Trim(name->Text()) : std::wstring();
        if (name->visible && listName.empty()) return;  // asked to save: a playlist needs a name
        f->Close();
        if (!listName.empty()) {
            const std::string id = TheApp().playlists().Create(listName);
            TheApp().playlists().AddPaths(id, links);
            TheApp().libraryChanged.Emit();  // the Playlists page and Home show the new list
            std::vector<QueueItem> items;
            for (const auto& l : links) items.push_back({l, {}, 0});
            TheApp().PlayQueueItems(std::move(items), 0, listName);
        } else {
            TheApp().Open(links);
        }
    };
    open->onClick = submit;
    urls->onSubmit = submit;
    name->onSubmit = [submit](const std::wstring&) { submit(); };
    f->onSubmit = submit;
    // Prefill a link from the clipboard.
    if (!ui::Host::Automation()) {
        const std::wstring clip = Trim(ClipboardText(w->Hwnd()));
        if (LooksLikeUrl(clip) && clip.size() < 2048 && LooksLikeMediaUrl(clip)) {
            urls->SetText(clip);
        }
    }
    ShowFrame(w, std::move(frame), urls);
    validate();
}

void ShowNewPlaylistDialog(std::function<void(const std::string&)> done, std::vector<NewPlaylistFile> files) {
    MainWindow* w = TheApp().main();
    if (!w) {
        if (done) done({});
        return;
    }
    auto frame = std::make_unique<Frame>(Tr(L"New Playlist"), FitWidth(w, 713));
    Frame* f = frame.get();
    Widget* body = f->Body();

    auto* top = Row(body, 20);
    top->align = Align::End;
    auto* left = top->Add<Widget>();
    left->dir = Dir::Column;
    left->width = Length::Grow();
    left->gap = 20;
    AddFieldHeader(left, Tr(L"Name"),
                   Tr(L"The playlist will be saved and displayed with this title and thumbnail."));
    auto* name = left->Add<TextBox>(Tr(L"e.g., Evening"));
    name->width = Length::Grow();
    name->height = Length::Px(37);
    auto* cover = top->Add<CoverTile>();

    AddSeparator(body);
    AddFieldHeader(body, Tr(L"Select Files"), Tr(L"These files will be added to the playlist."));
    // Above the drop zone, ticked unless the dialog came with files of its own: a new list is
    // most often what plays now, and missing the box made an empty playlist.
    Checkbox* addQueue = nullptr;
    const size_t queued = TheApp().queue().Items().size();
    if (queued > 0)
        addQueue = body->Add<Checkbox>(Fmt(Tr(L"Also add the current queue ({0})"), {std::to_wstring(queued)}),
                                       files.empty());
    auto* zone = body->Add<FileDropZone>();
    // The files the dialog was opened for are listed (and can be removed) instead of being
    // added silently after Create; their titles and lengths come along.
    auto known = std::make_shared<std::map<std::wstring, NewPlaylistFile>>();
    for (NewPlaylistFile& file : files) {
        if (!known->emplace(library::PathKey(file.path), file).second) continue;
        zone->files.push_back(file.path);
    }

    f->Footer()->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [f] { f->Close(); };
    f->Footer()->Add<Spacer>();
    auto* create = f->Footer()->Add<DButton>(Tr(L"Create"), BtnStyle::Accent);
    create->enabled = false;
    name->onChange = [create](const std::wstring& t) {
        create->enabled = !Trim(t).empty();
        create->Invalidate();
    };

    // `done` runs exactly once: with the id on create, empty when cancelled.
    auto result = std::make_shared<std::string>();
    auto submit = [f, name, cover, zone, addQueue, result, known] {
        const std::wstring n = Trim(name->Text());
        if (n.empty()) return;
        auto& pl = TheApp().playlists();
        const std::string id = pl.Create(n);
        std::vector<library::PlaylistItem> items;
        if (addQueue && addQueue->Value())
            for (const QueueItem& q : TheApp().queue().Items()) items.push_back({q.path, q.title, q.duration});
        for (const auto& p : zone->files) {
            auto it = known->find(library::PathKey(p));
            if (it != known->end())
                items.push_back({p, it->second.title, it->second.duration});
            else
                items.push_back({p, {}, 0});
        }
        if (!items.empty()) pl.Add(id, items);
        if (!cover->path.empty()) TheApp().SetUiValue("playlist.cover." + id, WideToUtf8(cover->path));
        *result = id;
        f->Close();
    };
    create->onClick = submit;
    name->onSubmit = [submit](const std::wstring&) { submit(); };
    f->onSubmit = submit;

    // Files dropped on the window go into the list while the dialog is open.
    auto previousDrop = std::make_shared<std::function<void(std::vector<std::wstring>)>>(w->onDropFiles);
    w->onDropFiles = [zone](std::vector<std::wstring> files) { zone->AddFiles(files); };
    f->OnClosed([w, previousDrop, result, done] {
        w->onDropFiles = *previousDrop;
        if (done) done(*result);
        if (result->empty()) return;
        const library::Playlist* p = TheApp().playlists().Find(*result);
        const std::wstring route = Utf8ToWide(*result);
        ShowToast(w, Fmt(Tr(L"Playlist “{0}” created"), {p ? p->name : std::wstring()}), "playlist", 5000, Tr(L"Open"),
                  [w, route] {
                      // Posted: leaving the player closes the popups, this toast among them, mid-click.
                      w->Post([route] {
                          TheApp().ShowPlayer(false);
                          TheApp().Navigate(Route{Section::Playlists, route});
                      });
                  });
    });
    ShowFrame(w, std::move(frame), name);
}

void ShowMediaInfoDialog() {
    MainWindow* w = TheApp().main();
    if (!w) return;
    const bool loaded = TheApp().player().State().fileLoaded;
    const auto sections = loaded ? CollectMediaInfo() : std::vector<InfoSection>{};
    auto frame = std::make_unique<Frame>(Tr(L"Media Information"), FitWidth(w, 720));
    Frame* f = frame.get();
    Widget* body = f->Body();
    if (!loaded) {
        body->Add<Label>(Tr(L"Nothing is playing. Open a file to see its properties."), TextStyle::Body, Tone::Tertiary);
    } else {
        auto* scroll = body->Add<ScrollView>();
        scroll->width = Length::Grow();
        auto* content = scroll->SetContent<Widget>();
        content->dir = Dir::Column;
        content->width = Length::Grow();
        content->gap = 6;
        // Frame width minus the body padding (20 + 20).
        const float valueW = FitWidth(w, 720) - 40 - InfoLine::kKeyW - InfoLine::kGap;
        float est = 0;
        // Poster and overview from TMDB for a video, when the user turned that on.
        const std::wstring path = TheApp().player().State().path;
        if (net::online::MetadataToggle() && library::KindOf(path) == library::MediaKind::Video) {
            content->Add<MetaBlock>(path);
            auto* g = content->Add<Widget>();
            g->height = Length::Px(10);
            AddSeparator(content);
            est += 200;
        }
        for (size_t i = 0; i < sections.size(); ++i) {
            if (i > 0) {
                auto* g = content->Add<Widget>();
                g->height = Length::Px(10);
                AddSeparator(content);
                est += 17;
            }
            auto* h = content->Add<Label>(sections[i].title, TextStyle::Large);
            h->color = SoftText;
            h->pad = {0, 6, 0, 4};
            est += 36;
            for (const auto& r : sections[i].rows) {
                auto* row = content->Add<InfoLine>(r.key, r.value);
                est += row->Measure(valueW + InfoLine::kKeyW + InfoLine::kGap, 1e6f).h + 6;
            }
        }
        scroll->height = Length::Px(std::min(est + 8, std::max(160.0f, w->ClientSize().h - 220)));
    }
    if (loaded) {
        auto* copy = f->Footer()->Add<DButton>(Tr(L"Copy"), BtnStyle::Neutral);
        const std::wstring text = InfoText(sections);
        copy->onClick = [w, text] {
            SetClipboardText(w->Hwnd(), text);
            ShowToast(w, Tr(L"Copied to clipboard"), "check_active");
        };
    }
    f->Footer()->Add<Spacer>();
    f->Footer()->Add<DButton>(Tr(L"Close"), BtnStyle::Accent)->onClick = [f] { f->Close(); };
    f->onSubmit = [f] { f->Close(); };
    ShowFrame(w, std::move(frame));
}

// =============================================================================================
// About
// =============================================================================================

namespace {

// "0.1.0" from the VERSIONINFO resource (res/app.rc), without version.lib.
std::wstring AppVersion() {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));  // VS_VERSION_INFO, RT_VERSION
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const auto* data = mem ? static_cast<const BYTE*>(LockResource(mem)) : nullptr;
    const DWORD size = res ? SizeofResource(nullptr, res) : 0;
    for (DWORD i = 0; data && i + 16 <= size; i += 4) {
        DWORD sig = 0;
        memcpy(&sig, data + i, 4);
        if (sig != 0xFEEF04BD) continue;  // VS_FIXEDFILEINFO
        DWORD ms = 0, ls = 0;
        memcpy(&ms, data + i + 8, 4);
        memcpy(&ls, data + i + 12, 4);
        return std::to_wstring(HIWORD(ms)) + L"." + std::to_wstring(LOWORD(ms)) + L"." + std::to_wstring(HIWORD(ls));
    }
    return L"?";
}

class AboutLink : public LinkButton {
public:
    AboutLink(std::wstring text, std::wstring url) : LinkButton(std::move(text)), url_(std::move(url)) {
        trailingIcon = "open";
        tooltip = url_;
        onClick = [this] { Host::ShellOpen(nullptr, L"open", url_.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    }

private:
    std::wstring url_;
};

}  // namespace

void ShowAboutDialog(Host* host) {
    if (!host) host = TheApp().main();
    if (!host) return;
    auto frame = std::make_unique<Frame>(Tr(L"About Lumen"), FitWidth(host, 560));
    Frame* f = frame.get();
    Widget* body = f->Body();
    body->gap = 16;

    auto* top = Row(body, 14);
    auto* logo = top->Add<IconView>("lumen", 40);
    logo->width = Length::Px(48);
    logo->height = Length::Px(48);
    auto* names = top->Add<Widget>();
    names->dir = Dir::Column;
    names->gap = 2;
    auto* title = names->Add<Label>(L"Lumen", TextStyle::Large);
    title->color = SoftText;
    names->Add<Label>(Fmt(Tr(L"Version {0}"), {AppVersion()}), TextStyle::Body, Tone::Tertiary);

    // mpv starts with the first file; before that its versions aren't known.
    Player& player = TheApp().player();
    const std::wstring later = Tr(L"known once a file has been opened");
    std::wstring mpv = player.Ready() ? Utf8ToWide(player.GetPropertyString("mpv-version")) : L"";
    std::wstring ffmpeg = player.Ready() ? Utf8ToWide(player.GetPropertyString("ffmpeg-version")) : L"";
    body->Add<InfoLine>(L"libmpv", mpv.empty() ? later : mpv);
    body->Add<InfoLine>(L"FFmpeg", ffmpeg.empty() ? later : ffmpeg);

    AddSeparator(body);
    body->Add<Label>(Tr(L"Licences"), TextStyle::Body, Tone::Tertiary);
    auto* links = body->Add<Widget>();
    links->dir = Dir::Column;
    links->gap = 6;
    links->width = Length::Grow();
    links->Add<AboutLink>(Tr(L"Lumen: GNU GPL 3.0 or later"), L"https://www.gnu.org/licenses/gpl-3.0.html");
    links->Add<AboutLink>(Tr(L"mpv and libmpv: GPL 2.0 or later"), L"https://github.com/mpv-player/mpv/blob/master/Copyright");
    links->Add<AboutLink>(Tr(L"FFmpeg: LGPL 2.1 or later"), L"https://ffmpeg.org/legal.html");
    links->Add<AboutLink>(Tr(L"Inter typeface: SIL Open Font License 1.1"), L"https://openfontlicense.org");

    // The online services (each off until turned on in Settings › Privacy). TMDB's terms ask for
    // this notice word for word.
    AddSeparator(body);
    body->Add<Label>(Tr(L"Online services"), TextStyle::Body, Tone::Tertiary);
    auto* services = body->Add<Widget>();
    services->dir = Dir::Column;
    services->gap = 6;
    services->width = Length::Grow();
    services->Add<AboutLink>(Tr(L"Song lyrics: LRCLIB"), L"https://lrclib.net");
    services->Add<AboutLink>(Tr(L"Subtitles: OpenSubtitles.com"), L"https://www.opensubtitles.com");
    services->Add<AboutLink>(Tr(L"Posters and descriptions: TMDB"), L"https://www.themoviedb.org");
    auto* tmdb = services->Add<Label>(L"This product uses the TMDB API but is not endorsed or certified by TMDB.",
                                      TextStyle::Small, Tone::Tertiary);
    tmdb->wrap = true;
    tmdb->width = Length::Grow();

    f->Footer()->Add<Spacer>();
    auto* close = f->Footer()->Add<DButton>(Tr(L"Close"), BtnStyle::Accent);
    close->onClick = [f] { f->Close(); };
    f->onSubmit = [f] { f->Close(); };
    // On "Close", not the first licence link: Enter closes, as in the other dialogs (VF-04).
    ShowFrame(host, std::move(frame), close);
}

// =============================================================================================
// TMDB: find another match
// =============================================================================================

namespace {

class MatchRow : public Widget {
public:
    MatchRow(net::MetaMatch m, std::function<void(const net::MetaMatch&)> pick) : m_(std::move(m)), pick_(std::move(pick)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(64);
        tooltip = m_.overview.empty() ? m_.title : m_.title + L"\n\n" + m_.overview;
        onClick = [this] {
            if (pick_) pick_(m_);
        };
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = IsHot() || IsPressed();
        if (hot) p.Fill(rect, IsPressed() ? c.press : c.hover, 6);
        PaintFocusRing(p, rect, 6);
        const Color fg = hot ? c.textPrimary : c.textSecondary;
        p.Icon(m_.tv ? "tv" : "video", {rect.x + 10, rect.y + 10, 24, 24}, fg, c.accentMid);
        std::wstring title = m_.title;
        if (m_.year) title += L" (" + std::to_wstring(m_.year) + L")";
        p.Text(title, {rect.x + 46, rect.y + 8, rect.w - 58, 22}, TextStyle::Body, fg);
        const std::wstring overview = m_.overview.empty() ? std::wstring(Tr(L"No description")) : m_.overview;
        p.Text(overview, {rect.x + 46, rect.y + 32, rect.w - 58, 22}, TextStyle::Small, c.textTertiary);
    }

private:
    net::MetaMatch m_;
    std::function<void(const net::MetaMatch&)> pick_;
};

class MatchDialog : public std::enable_shared_from_this<MatchDialog> {
public:
    MatchDialog(MainWindow* w, std::wstring video) : window_(w), video_(std::move(video)) {}

    std::unique_ptr<Frame> Build() {
        auto frame = std::make_unique<Frame>(Tr(L"Find Another Match"), FitWidth(window_, 780), FitHeight(window_, 640));
        frame_ = frame.get();
        const net::VideoGuess g = net::online::Guess(video_);
        Widget* bar = frame_->Toolbar();
        query_ = bar->Add<TextBox>(Tr(L"Movie or series title"), "search");
        query_->id = "meta-query";
        query_->raised = true;
        query_->width = Length::Grow();
        query_->height = Length::Px(34);
        query_->SetText(g.title.empty() ? library::StemOf(video_) : g.title);
        query_->onSubmit = [this](const std::wstring&) { Search(); };
        year_ = bar->Add<TextBox>(Tr(L"Year"));
        year_->raised = true;
        year_->width = Length::Px(84);
        year_->height = Length::Px(34);
        if (g.year) year_->SetText(std::to_wstring(g.year));
        year_->onSubmit = [this](const std::wstring&) { Search(); };
        kind_ = bar->Add<FieldDropdown>(std::vector<Choice>{{Tr(L"Movie"), "movie"}, {Tr(L"Series"), "tv"}},
                                        g.kind == net::VideoGuess::Episode ? 1 : 0);
        kind_->width = Length::Px(150);
        kind_->height = Length::Px(34);
        kind_->onChange = [this](int) { Search(); };
        bar->Add<DButton>(Tr(L"Find"), BtnStyle::Accent)->onClick = [this] { Search(); };

        Widget* body = frame_->Body();
        body->pad = {};
        body->gap = 0;
        scroll_ = body->Add<ScrollView>();
        scroll_->width = Length::Grow();
        scroll_->height = Length::Grow();

        frame_->Footer()->Add<DButton>(Tr(L"Close"), BtnStyle::Neutral)->onClick = [this] { frame_->Close(); };
        status_ = frame_->Footer()->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
        status_->pad = {8, 0, 0, 0};
        status_->width = Length::Grow();
        auto* reset = frame_->Footer()->Add<DButton>(Tr(L"Reset"), BtnStyle::Neutral);
        reset->tooltip = Tr(L"Forget this match and show the file's own thumbnail");
        reset->onClick = [this] {
            net::online::ResetMeta(video_);
            ShowToast(window_, Tr(L"Match reset"), "refresh");
            frame_->Close();
        };
        frame_->onSubmit = [this] { Search(); };
        return frame;
    }
    Widget* FirstFocus() const { return query_; }
    void Closed() { closed_ = true; }

    void Search() {
        const bool tv = kind_->Selected() == 1;
        Widget* content = Message(Tr(L"Searching…"));
        (void)content;
        auto self = weak_from_this();
        // Only the latest search shows: an older answer that arrives later is dropped.
        const unsigned serial = ++searches_;
        net::online::SearchMetadata(query_->Text(), std::max(0, _wtoi(year_->Text().c_str())), tv,
                                    [self, serial](const std::vector<net::MetaMatch>& results, const std::wstring& error) {
                                        if (auto d = self.lock(); d && serial == d->searches_) d->Show(results, error);
                                    });
    }

private:
    Widget* Content() {
        Widget* content = scroll_->SetContent<Widget>();
        content->dir = Dir::Column;
        content->width = Length::Grow();
        content->pad = {16, 12, 16, 12};
        content->gap = 2;
        scroll_->ScrollTo(0, false);
        scroll_->InvalidateLayout();
        return content;
    }
    Widget* Message(const std::wstring& text) {
        Widget* content = Content();
        content->align = Align::Center;
        content->pad = {40, 70, 40, 30};
        auto* l = content->Add<Label>(text, TextStyle::Body, Tone::Tertiary);
        l->wrap = true;
        l->halign = HAlign::Center;
        l->maxW = 560;
        return content;
    }
    void Show(const std::vector<net::MetaMatch>& results, const std::wstring& error) {
        if (closed_) return;
        if (!error.empty()) {
            Message(error);
            status_->SetText({});
            return;
        }
        if (results.empty()) {
            Message(Tr(L"Nothing found. Try another title, the year or the other kind."));
            status_->SetText({});
            return;
        }
        Widget* content = Content();
        auto self = weak_from_this();
        for (const net::MetaMatch& m : results)
            content->Add<MatchRow>(m, [self](const net::MetaMatch& pick) {
                if (auto d = self.lock()) d->Pick(pick);
            });
        status_->SetText(Fmt(Tr(L"Found: {0}"), {std::to_wstring(results.size())}));
        status_->InvalidateLayout();
    }
    void Pick(const net::MetaMatch& m) {
        net::online::SetMatch(video_, m);
        ShowToast(window_, Fmt(Tr(L"Matched with {0}"), {m.title}), "check_selected");
        frame_->Close();
    }

    MainWindow* window_;
    std::wstring video_;
    Frame* frame_ = nullptr;
    TextBox* query_ = nullptr;
    TextBox* year_ = nullptr;
    FieldDropdown* kind_ = nullptr;
    ScrollView* scroll_ = nullptr;
    Label* status_ = nullptr;
    bool closed_ = false;
    unsigned searches_ = 0;  // serial of the latest Search()
};

}  // namespace

void ShowMetadataMatchDialog(const std::wstring& videoPath) {
    MainWindow* w = TheApp().main();
    if (!w || videoPath.empty()) return;
    auto dialog = std::make_shared<MatchDialog>(w, videoPath);
    auto frame = dialog->Build();
    Widget* focus = dialog->FirstFocus();
    frame->OnClosed([dialog] { dialog->Closed(); });
    ShowFrame(w, std::move(frame), focus);
    dialog->Search();
}

}  // namespace app
