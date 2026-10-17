// Dialogs module: Keyboard Shortcuts, Open Network Stream, New Playlist and
// Media Information (design/ref: Keyboard_Shortcuts, Open_network_stream,
// new_playlist). Customize Player, Add Subtitles and the onboarding window
// live in their own files next to this one.
#include "app/Dialogs.h"

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

// =============================================================================================
// Keyboard Shortcuts
// =============================================================================================

// Read-only key field from the design: dark pill, centered key text, pencil at the right.
class KeyField : public Widget {
public:
    KeyField(std::wstring text, std::function<void()> edit) : text_(std::move(text)) {
        interactive = true;
        width = Length::Px(270);
        height = Length::Px(33);
        tooltip = Tr(L"Edit in Settings");
        onClick = std::move(edit);
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, IsHot() ? Color::Mix(c.bgDeep, c.bgSelected, 0.35f) : c.bgDeep, 6);
        p.Text(text_, {rect.x + 20, rect.y, rect.w - 64, rect.h}, TextStyle::Mono, SoftText(), HAlign::Center);
        p.Icon("edit", {rect.Right() - 29, rect.Cy() - 12, 24, 24}, IsHot() ? c.textPrimary : c.textTertiary);
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
                std::wstring k1 = keys.size() > 0 ? KeyText(keys[0].first, keys[0].second) : L"";
                std::wstring k2 = keys.size() > 1 ? KeyText(keys[1].first, keys[1].second) : L"";
                const std::wstring label = Tr(a.label);
                if (!q.empty()) {
                    const bool byName = library::SearchFold(label).find(q) != std::wstring::npos ||
                                        library::SearchFold(a.label).find(q) != std::wstring::npos;
                    bool byKey = false;
                    for (const auto& [vk, mods] : keys)
                        if (library::SearchFold(KeyText(vk, mods)).find(q) != std::wstring::npos) byKey = true;
                    if (!(mode == 1 ? byName : mode == 2 ? byKey : byName || byKey)) continue;
                }
                if (!section) {
                    if (shown > 0) {
                        auto* gapW = Add<Widget>();
                        gapW->height = Length::Px(20);
                        AddSeparator(this);
                    }
                    auto* h = Add<Label>(Tr(g.c_str()), TextStyle::Large, Tone::Secondary);
                    h->pad = {0, 20, 0, 20};
                    section = Add<Widget>();
                    section->dir = Dir::Column;
                    section->width = Length::Grow();
                    section->gap = 14;
                }
                auto* row = Row(section, 0);
                row->height = Length::Px(33);
                auto* l = row->Add<Label>(label, TextStyle::Body);
                l->color = SoftText;
                l->width = Length::Grow();
                Frame* f = frame_;
                auto edit = [f] {
                    f->Close();
                    TheApp().OpenSettings("controls");
                };
                row->Add<KeyField>(k1, edit);
                row->Add<Widget>()->width = Length::Px(89);
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
            if (l.rfind(L"www.", 0) == 0) l = L"https://" + l;
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

std::wstring Prop(const char* name) {
    return Utf8ToWide(TheApp().player().GetPropertyString(name));
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
    return buf;
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
            add(video, L"Frame rate", std::wstring(buf) + L" " + Tr(L"fps"));
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
    auto frame = std::make_unique<Frame>(Tr(L"Keyboard Shortcuts"), FitWidth(w, 1048), FitHeight(w, 800));
    Frame* f = frame.get();
    auto* search = f->Toolbar()->Add<TextBox>(Tr(L"Search Keyboard Shortcuts..."), "search");
    search->width = Length::Grow();
    search->background = P().bgRaised;
    auto* mode = f->Toolbar()->Add<FieldDropdown>(
        std::vector<Choice>{{Tr(L"All Fields"), "all"}, {Tr(L"Action"), "action"}, {Tr(L"Hotkey"), "key"}}, 0);
    mode->width = Length::Px(200);
    mode->height = Length::Px(34);

    Widget* body = f->Body();
    body->pad = {20, 20, 20, 0};
    body->gap = 0;
    auto* head = Row(body, 0);
    head->height = Length::Px(25);
    head->Add<Label>(Tr(L"Action"), TextStyle::Body, Tone::Tertiary)->width = Length::Grow();
    auto* h1 = head->Add<Label>(Tr(L"Hotkey"), TextStyle::Body, Tone::Tertiary);
    h1->width = Length::Px(270);
    h1->halign = HAlign::Center;
    head->Add<Widget>()->width = Length::Px(89);
    auto* h2 = head->Add<Label>(Tr(L"Alternative"), TextStyle::Body, Tone::Tertiary);
    h2->width = Length::Px(270);
    h2->halign = HAlign::Center;
    auto* gapW = body->Add<Widget>();
    gapW->height = Length::Px(20);
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
    auto* hint = body->Add<Label>(Tr(L"Enter opens the stream, Shift+Enter starts a new line."), TextStyle::Small,
                                  Tone::Tertiary);
    hint->pad = {0, 0, 0, 0};

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

    // Buffering (Settings > Network) right here: streams are where it matters.
    auto* buf = Row(body, 12);
    auto* bl = buf->Add<Widget>();
    bl->dir = Dir::Column;
    bl->width = Length::Grow();
    bl->gap = 2;
    bl->Add<Label>(Tr(L"Buffering"), TextStyle::Body)->color = SoftText;
    bl->Add<Label>(Tr(L"More buffering survives a slow connection but starts later. Applies to all streams."),
                   TextStyle::Small, Tone::Tertiary);
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

    auto validate = [urls, hint, open] {
        int bad = 0;
        const auto links = ParseLinks(urls->Text(), &bad);
        open->enabled = bad == 0 && !links.empty();
        if (bad)
            hint->SetText(Fmt(Tr(L"Line {0} is not a link. Links look like https://, rtsp:// or udp://"),
                              {std::to_wstring(bad)}));
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
    const std::wstring clip = Trim(ClipboardText(w->Hwnd()));
    if (LooksLikeUrl(clip) && clip.size() < 2048) urls->SetText(clip);
    ShowFrame(w, std::move(frame), urls);
    validate();
}

void ShowNewPlaylistDialog(std::function<void(const std::string&)> done) {
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
    Checkbox* addQueue = nullptr;
    const size_t queued = TheApp().queue().Items().size();
    if (queued > 0)
        addQueue = body->Add<Checkbox>(
            Fmt(Tr(L"Also add the current queue ({0})"), {CountItems(static_cast<long long>(queued))}));

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
    auto submit = [f, name, cover, zone, addQueue, result] {
        const std::wstring n = Trim(name->Text());
        if (n.empty()) return;
        auto& pl = TheApp().playlists();
        const std::string id = pl.Create(n);
        std::vector<library::PlaylistItem> items;
        if (addQueue && addQueue->Value())
            for (const QueueItem& q : TheApp().queue().Items()) items.push_back({q.path, q.title, q.duration});
        for (const auto& p : zone->files) items.push_back({p, {}, 0});
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
                auto* row = Row(content, 16);
                row->align = Align::Start;
                auto* k = row->Add<Label>(r.key, TextStyle::Body, Tone::Tertiary);
                k->width = Length::Px(210);
                auto* v = row->Add<Label>(r.value, TextStyle::Body);
                v->color = SoftText;
                v->width = Length::Grow();
                v->wrap = true;
                v->maxLines = 3;
                est += 26 + (r.value.size() > 60 ? 20.0f : 0.0f);
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

}  // namespace app
