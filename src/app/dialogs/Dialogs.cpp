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
                row->Add<KeyField>(k1, edit);
                row->Add<KeyField>(k2, edit);
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
        for (size_t i = 0; i < files.size(); ++i) {
            const Rect r{area.x, area.y + i * kRowH - scroll_, area.w, kRowH};
            if (r.Bottom() < rect.y || r.y > rect.Bottom()) continue;
            const bool hot = static_cast<int>(i) == hoverRow_;
            if (hot) p.Fill(r, c.hover, 4);
            p.Icon(library::KindOf(files[i]) == library::MediaKind::Audio ? "music" : "video",
                   {r.x + 6, r.Cy() - 10, 20, 20}, c.textTertiary);
            p.Text(library::FileNameOf(files[i]), {r.x + 34, r.y, r.w - 74, r.h}, TextStyle::Body, SoftText());
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

    AddSeparator(body);
    AddFieldHeader(body, Tr(L"Playlist name"), Tr(L"The playlist will be saved and displayed with this title."),
                   Tr(L"(optional)"));
    auto* name = body->Add<TextBox>(Tr(L"e.g., Favorite TV Channels"));
    name->width = Length::Grow();
    name->height = Length::Px(37);

    // Buffering (Settings > Network) right here: streams are where it matters. Same
    // heading + description scale as the two fields above.
    AddSeparator(body);
    auto* buf = body->Add<SideRow>();
    Widget* bl = AddFieldHeader(
        buf, Tr(L"Buffering"),
        Tr(L"More buffering survives a slow connection but starts later. Applies to all streams."));
    bl->width = Length::Grow();
    std::vector<Choice> modes = {{Tr(L"Low"), "low"}, {Tr(L"Normal"), "normal"}, {Tr(L"High"), "high"}};
    const std::string current = TheApp().config().GetString("network.buffering");
    if (current == "custom") modes.push_back({Tr(L"Custom"), "custom"});
    int sel = 1;
    for (size_t i = 0; i < modes.size(); ++i)
        if (modes[i].value == current) sel = static_cast<int>(i);
    auto* dd = buf->Add<FieldDropdown>(modes, sel);
    dd->width = Length::Px(200);
    dd->height = Length::Px(34);
    dd->onChange = [dd](int i) {
        if (i >= 0) TheApp().config().SetString("network.buffering", dd->Choices()[static_cast<size_t>(i)].value);
    };

    f->Footer()->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [f] { f->Close(); };
    f->Footer()->Add<Spacer>();
    auto* open = f->Footer()->Add<DButton>(Tr(L"Open"), BtnStyle::Accent);
    open->enabled = false;

    auto validate = [urls, hint, hintIcon, open] {
        int bad = 0;
        const std::wstring text = urls->Text();
        const auto links = ParseLinks(text, &bad);
        open->enabled = bad == 0 && !links.empty();
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
            hint->SetText(Tr(L"Video site links need yt-dlp.exe next to Lumen.exe (Settings > Network)."));
        else if (site && links.size() == 1)
            hint->SetText(Tr(L"The video page opens through yt-dlp."));
        else if (links.size() > 1)
            hint->SetText(Fmt(Tr(L"{0} links will be opened as one queue."), {std::to_wstring(links.size())}));
        else
            hint->SetText(Tr(L"Enter opens the stream, Shift+Enter starts a new line."));
        open->Invalidate();
    };
    urls->onChange = [validate](const std::wstring&) { validate(); };
    auto submit = [f, urls, name] {
        int bad = 0;
        const auto links = ParseLinks(urls->Text(), &bad);
        if (bad || links.empty()) return;
        const std::wstring listName = Trim(name->Text());
        f->Close();
        if (!listName.empty()) {
            const std::string id = TheApp().playlists().Create(listName);
            TheApp().playlists().AddPaths(id, links);
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
    AddFieldHeader(left, Tr(L"Playlist Details"),
                   Tr(L"The playlist will be saved and displayed with this title and thumbnail."));
    auto* name = left->Add<TextBox>(Tr(L"e.g., Favorite Videos"));
    name->width = Length::Grow();
    name->height = Length::Px(37);
    auto* cover = top->Add<CoverTile>();

    AddSeparator(body);
    AddFieldHeader(body, Tr(L"Select Files"), Tr(L"These files will be added to the playlist."));
    auto* zone = body->Add<FileDropZone>();
    // The files the dialog was opened for are listed (and can be removed) instead of being
    // added silently after Create; their titles and lengths come along.
    auto known = std::make_shared<std::map<std::wstring, NewPlaylistFile>>();
    for (NewPlaylistFile& file : files) {
        if (!known->emplace(library::PathKey(file.path), file).second) continue;
        zone->files.push_back(file.path);
    }
    Checkbox* addQueue = nullptr;
    const size_t queued = TheApp().queue().Items().size();
    if (queued > 0)
        addQueue = body->Add<Checkbox>(
            Fmt(Tr(L"Also add the current queue ({0})"), {std::to_wstring(queued)}));

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
        if (!result->empty()) ShowToast(w, Tr(L"Playlist created"), "playlist");
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

    f->Footer()->Add<Spacer>();
    auto* close = f->Footer()->Add<DButton>(Tr(L"Close"), BtnStyle::Accent);
    close->onClick = [f] { f->Close(); };
    f->onSubmit = [f] { f->Close(); };
    // On "Close", not the first licence link: Enter closes, as in the other dialogs (VF-04).
    ShowFrame(host, std::move(frame), close);
}

}  // namespace app
