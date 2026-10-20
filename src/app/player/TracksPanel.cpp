// Track lists at the top of the settings popover's Video / Audio / Subtitles tabs: embedded
// tracks with check marks, external tracks (add / remove) and the audio output devices.
#include "ui/Host.h"
#include "Util.h"
#include "app/App.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "gfx/Gpu.h"

#include <shobjidl.h>

namespace app::panel {

using namespace ui;

// File picker for the panels. kind: 0 media, 1 audio, 2 subtitles.
std::vector<std::wstring> PickFiles(int kind) {
    std::vector<std::wstring> out;
    MainWindow* w = TheApp().main();
    gfx::ComPtr<IFileOpenDialog> dlg;
    if (!w || FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return out;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    const COMDLG_FILTERSPEC media[] = {
        {Tr(L"Video and audio"),
         L"*.mkv;*.mp4;*.m4v;*.avi;*.mov;*.webm;*.wmv;*.flv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.vob;*.ogv;*.3gp;*.rmvb;"
         L"*.mp3;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wav;*.wma;*.ac3;*.dts"},
        {Tr(L"All files"), L"*.*"},
    };
    const COMDLG_FILTERSPEC audio[] = {
        {Tr(L"Audio files"), L"*.mp3;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wav;*.wma;*.ac3;*.eac3;*.dts;*.mka;*.thd"},
        {Tr(L"All files"), L"*.*"},
    };
    const COMDLG_FILTERSPEC subs[] = {
        {Tr(L"Subtitles"), L"*.srt;*.ass;*.ssa;*.vtt;*.sub;*.sup;*.idx;*.smi"},
        {Tr(L"All files"), L"*.*"},
    };
    const COMDLG_FILTERSPEC lyrics[] = {
        {Tr(L"Lyrics"), L"*.lrc;*.txt"},
        {Tr(L"All files"), L"*.*"},
    };
    // kind 3: one .lrc for the music view (MusicView.cpp)
    if (kind == 3) dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
    dlg->SetFileTypes(2, kind == 1 ? audio : kind == 2 ? subs : kind == 3 ? lyrics : media);
    if (FAILED(dlg->Show(w->Hwnd()))) return out;
    gfx::ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return out;
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        gfx::ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return out;
}

namespace {

const PlayerState& PS() {
    return TheApp().player().State();
}

struct ChoiceRow {
    std::wstring label;
    std::wstring detail;  // right-aligned, tertiary
    bool checked = false;
    std::function<void()> select;
    std::function<void()> remove;  // shown in edit mode
};

// List of choosable rows with check marks (32 DIP rows every 44 DIP, text at x+42).
class ChoiceList : public Widget {
public:
    explicit ChoiceList(std::function<std::vector<ChoiceRow>()> rows, std::wstring empty = {})
        : rows_(std::move(rows)), empty_(std::move(empty)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        pad = {20, 0, 20, 0};
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTracks | kChangeFile)) InvalidateLayout();
        });
        liveConn_ = Connect(Live().changed, [this] { InvalidateLayout(); });
    }
    bool editing = false;
    std::wstring name;  // for screen readers when no list title stands before it
    static constexpr float kPitch = 44, kRowH = 32;
    int Count() const { return static_cast<int>(rows_().size()); }

    Size MeasureContent(float availW, float) override {
        const size_t n = rows_().size();
        if (n == 0) return {availW, empty_.empty() ? 0.0f : 40.0f};
        return {availW, kPitch * static_cast<float>(n) - (kPitch - kRowH) + 8};
    }
    Rect RowRect(int i) const { return {rect.x + pad.l, rect.y + kPitch * static_cast<float>(i), rect.w - pad.H(), kRowH}; }
    int RowAt(float y) const {
        const int i = static_cast<int>(std::floor((y - rect.y) / kPitch));
        return i >= 0 && i < static_cast<int>(rows_().size()) && y - rect.y - kPitch * static_cast<float>(i) < kRowH ? i : -1;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const auto rows = rows_();
        if (IsHot() && GetHost()) hover_ = RowAt(GetHost()->MousePos().y);
        if (rows.empty()) {
            if (!empty_.empty())
                p.Text(empty_, {rect.x + 62, rect.y, rect.w - 82, 32}, TextStyle::Body, c.textTertiary);
            return;
        }
        for (size_t k = 0; k < rows.size(); ++k) {
            const int i = static_cast<int>(k);
            const ChoiceRow& row = rows[k];
            const Rect r = RowRect(i);
            const bool hot = i == hover_ && IsHot();
            if (hot) p.Fill(r, c.hover, 4);
            if (i == focusRow_ && ShowFocusRing()) PaintFocusRing(p, r, 4);
            if (row.checked) p.Icon("check_active", Rect{r.x, r.y, 32, 32}.Center(24, 24), c.accent, c.accent);
            const bool showRemove = editing && row.remove;
            float right = r.Right();
            if (showRemove) {
                const Rect x{r.Right() - 28, r.Cy() - 12, 24, 24};
                p.Icon("close", x.Center(18, 18), hot && hoverX_ ? c.textPrimary : c.textTertiary);
                right -= 32;
            }
            float dw = 0;
            if (!row.detail.empty()) {
                dw = std::min(120.0f, gfx::Text::Get().Measure(row.detail, TextStyle::Small).width + 4);
                p.Text(row.detail, {right - dw, r.y, dw, r.h}, TextStyle::Small, c.textTertiary, HAlign::Trailing);
                dw += 8;
            }
            const Color fg = row.checked ? c.textPrimary : hot ? c.textPrimary : RowText();
            p.Text(row.label, {r.x + 42, r.y, std::max(0.0f, right - dw - r.x - 42), r.h}, TextStyle::Body, fg);
        }
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        const bool x = i >= 0 && editing && e.x >= RowRect(i).Right() - 32;
        if (i != hover_ || x != hoverX_) {
            hover_ = i;
            hoverX_ = x;
            Invalidate();
        }
        return true;
    }
    void OnMouseLeave() override {
        hover_ = -1;
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        const int i = RowAt(e.y);
        if (i < 0) return true;
        focusRow_ = i;
        Activate(i, editing && e.x >= RowRect(i).Right() - 32);
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        const int n = static_cast<int>(rows_().size());
        if (n == 0) return false;
        if (focusRow_ < 0) focusRow_ = 0;
        if (e.key == VK_UP) focusRow_ = std::max(0, focusRow_ - 1);
        else if (e.key == VK_DOWN) focusRow_ = std::min(n - 1, focusRow_ + 1);
        else if (e.key == VK_RETURN || e.key == VK_SPACE) Activate(focusRow_, false);
        else if (e.key == VK_DELETE) Activate(focusRow_, true);
        else return false;
        Invalidate();
        return true;
    }
    // One choice is on at a time: list items with radio semantics (the ticked row is selected).
    void Describe(A11y& a) override {
        a.role = Role::List;
        const auto rows = rows_();
        if (!name.empty()) a.name = name;
        if (rows.empty()) a.name = empty_;
        a.items = static_cast<int>(rows.size());
        a.item = [this, rows](int i, A11y::Item& it) {
            const ChoiceRow& row = rows[static_cast<size_t>(i)];
            it.name = row.detail.empty() ? row.label : row.label + L", " + row.detail;
            it.rect = RowRect(i);
            it.selected = row.checked;
        };
        a.selectItem = a.invokeItem = [this](int i) { Activate(i, false); };
        a.focusedItem = focusRow_;
    }

private:
    void Activate(int i, bool remove) {
        auto rows = rows_();
        if (i < 0 || i >= static_cast<int>(rows.size())) return;
        const ChoiceRow& row = rows[static_cast<size_t>(i)];
        if (remove && row.remove) row.remove();
        else if (row.select) row.select();
        Invalidate();
    }
    std::function<std::vector<ChoiceRow>()> rows_;
    std::wstring empty_;
    int hover_ = -1, focusRow_ = -1;
    bool hoverX_ = false;
    ScopedConnection playerConn_, liveConn_;
};

// Shows the ✕ on each added track; off and dimmed while nothing is added.
class RemoveModeButton : public IconButton {
public:
    explicit RemoveModeButton(ChoiceList* list) : IconButton("remove", Tr(L"Remove external tracks"), 30, 24), list_(list) {
        onClick = [this] {
            list_->editing = !list_->editing;
            list_->Invalidate();
            Invalidate();
        };
    }
    void Paint(Painter& p) override {
        enabled = list_->Count() > 0;
        if (!enabled) list_->editing = false;
        active = list_->editing;
        IconButton::Paint(p);
    }
    void Describe(A11y& a) override { a.toggle = list_->editing ? 1 : 0; }

private:
    ChoiceList* list_;
};

// "Embedded Tracks" row (60 DIP) with optional icon buttons on the right.
Widget* AddListTitle(Widget* page, const std::wstring& text) {
    auto* row = page->Add<Widget>();
    row->dir = Dir::Row;
    row->align = Align::Center;
    row->width = Length::Grow();
    row->height = Length::Px(60);
    row->pad = {28, 0, 24, 0};
    row->gap = 14;
    auto* l = row->Add<Label>(text, TextStyle::Body, Tone::Tertiary);
    l->width = Length::Grow();
    return row;
}

IconButton* AddTitleButton(Widget* row, const char* icon, const wchar_t* tip, std::function<void()> click) {
    auto* b = row->Add<IconButton>(icon, Tr(tip), 30, 24);
    b->onClick = std::move(click);
    return b;
}

const char* TrackProp(const wchar_t* type) {
    if (std::wstring_view(type) == L"video") return "vid";
    if (std::wstring_view(type) == L"audio") return "aid";
    return "sid";
}

// Embedded or external tracks of one type.
std::vector<ChoiceRow> TrackRows(const wchar_t* type, bool external) {
    std::vector<ChoiceRow> rows;
    const char* prop = TrackProp(type);
    int ordinal = 0;
    for (const Track& t : PS().tracks) {
        if (t.type != type) continue;
        ++ordinal;
        if (t.external != external) continue;
        ChoiceRow r;
        r.label = external && t.title.empty() && !t.TitleFromFileName() && t.lang.empty()
                      ? Fmt(Tr(L"Track {0}"), static_cast<long long>(ordinal))
                      : TrackName(t, ordinal);
        // The codec ("SubRip", "E-AC-3") is jargon: the row says stereo or 5.1 and nothing more (a
        // tooltip under the pointer covered the next row; Media Information names the format).
        r.detail = TrackDetail(t, false);
        r.checked = t.selected;
        const int64_t id = t.id;
        r.select = [prop, id] { TheApp().player().SetTrack(prop, id); };
        if (external) {
            const std::string cmd = std::string(prop) == "sid" ? "sub-remove" : prop[0] == 'a' ? "audio-remove" : "video-remove";
            r.remove = [cmd, id] { TheApp().player().Command({cmd, std::to_string(id)}); };
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

// The "off" choice sits above the track lists on its own, so it never reads as a track.
std::vector<ChoiceRow> OffRows(const wchar_t* type) {
    std::vector<ChoiceRow> rows;
    if (!PS().fileLoaded) return rows;
    const std::wstring_view t(type);
    const char* prop = TrackProp(type);
    bool any = false;
    for (const Track& track : PS().tracks) any |= track.type == type && track.selected;
    ChoiceRow off;
    off.label = t == L"video" ? Tr(L"No video") : t == L"audio" ? Tr(L"No audio") : Tr(L"Subtitles off");
    off.checked = !any;
    off.select = [prop] { TheApp().player().SetTrack(prop, 0); };
    rows.push_back(std::move(off));
    return rows;
}

std::vector<ChoiceRow> DeviceRows() {
    std::vector<ChoiceRow> rows;
    const LiveValues& v = Live().V();
    for (const AudioDevice& d : v.devices) {
        ChoiceRow r;
        r.label = d.name == "auto" ? Tr(L"System default") : d.description.empty() ? Utf8ToWide(d.name) : d.description;
        r.checked = d.name == v.audioDevice;
        const std::string name = d.name;
        r.select = [name] { TheApp().player().SetProperty("audio-device", name); };
        rows.push_back(std::move(r));
    }
    return rows;
}

void AddTracksSection(Widget* page, const wchar_t* type) {
    page->Add<Widget>()->height = Length::Px(16);
    const std::wstring_view t(type);
    page->Add<ChoiceList>([type] { return OffRows(type); })->name =
        Tr(t == L"video" ? L"Video" : t == L"audio" ? L"Audio" : L"Subtitles");
    AddSectionLine(page);
    AddListTitle(page, Tr(L"Embedded Tracks"));
    page->Add<ChoiceList>([type] { return TrackRows(type, false); }, Tr(L"No tracks"));
    if (std::wstring_view(type) == L"video") return;
    AddSectionLine(page);
    Widget* title = AddListTitle(page, Tr(L"External Tracks"));
    auto* list = page->Add<ChoiceList>([type] { return TrackRows(type, true); }, Tr(L"None added"));
    title->Add<RemoveModeButton>(list);
    const bool subs = std::wstring_view(type) == L"sub";
    if (subs)
        AddTitleButton(title, "search", L"Find subtitles online", [] {
            if (!PS().fileLoaded) return;
            // The dialog replaces the popover; close it after the click returns.
            if (MainWindow* w = TheApp().main())
                w->Post([] {
                    ClosePlayerPanels();
                    ShowOnlineSubtitlesDialog();
                });
        });
    AddTitleButton(title, "add", subs ? L"Add subtitle file…" : L"Add audio file…", [subs] {
        if (!PS().fileLoaded) return;
        for (const std::wstring& f : PickFiles(subs ? 2 : 1)) {
            if (subs) TheApp().player().AddSubtitle(f);
            else TheApp().player().AddAudio(f);
        }
    });
}

}  // namespace

void AddTrackLists(Widget* page, const wchar_t* type) {
    AddTracksSection(page, type);
    if (std::wstring_view(type) != L"audio") return;
    AddSectionLine(page);
    Widget* title = AddListTitle(page, Tr(L"Output Device"));
    AddTitleButton(title, "refresh", L"Refresh devices", [] { Live().RefreshDevices(); });
    page->Add<ChoiceList>([] { return DeviceRows(); }, Tr(L"Default device"));
}

}  // namespace app::panel
