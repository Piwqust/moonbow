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

// One choice per row: a radio on the left, the name and, under it, a line of detail ("Dubbed ·
// 5.1"). Rows hover inside an 8 DIP inset, their radio at the rows' 20 DIP edge. A row that can
// be removed (an added track) shows an × on hover.
class ChoiceList : public Widget {
public:
    explicit ChoiceList(std::function<std::vector<ChoiceRow>()> rows, std::wstring empty = {})
        : rows_(std::move(rows)), empty_(std::move(empty)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        pad = {8, 0, 8, 0};
        playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTracks | kChangeFile)) InvalidateLayout();
        });
        liveConn_ = Connect(Live().changed, [this] { InvalidateLayout(); });
    }
    std::wstring name;  // for screen readers

    Size MeasureContent(float availW, float) override {
        const auto rows = rows_();
        if (rows.empty()) return {availW, empty_.empty() ? 0.0f : 40.0f};
        return {availW, RowH(rows) * static_cast<float>(rows.size())};
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const auto rows = rows_();
        if (IsHot() && GetHost()) hover_ = RowAt(GetHost()->MousePos().y, rows);
        if (rows.empty()) {
            if (!empty_.empty()) p.Text(empty_, {rect.x + 20, rect.y, rect.w - 40, 40}, TextStyle::Body, c.textTertiary);
            return;
        }
        for (size_t k = 0; k < rows.size(); ++k) {
            const int i = static_cast<int>(k);
            const ChoiceRow& row = rows[k];
            const Rect r = RowRect(i, rows);
            const bool hot = i == hover_ && IsHot();
            if (hot) p.Fill(r, c.hover, kRadiusCard);
            if (i == focusRow_ && ShowFocusRing()) PaintFocusRing(p, r, kRadiusCard);
            // The radio: a ring, filled with a dot when chosen.
            const Point dot{r.x + 12 + 9, r.Cy()};
            p.StrokeCircle(dot, 8.25f, row.checked ? c.textPrimary : c.textTertiary, 1.5f);
            if (row.checked) p.FillCircle(dot, 4, c.textPrimary);
            float right = r.Right() - 12;
            if (row.remove && hot) {
                const Rect x{right - 22, r.Cy() - 11, 22, 22};
                p.Icon("close", x.Center(16, 16), hoverX_ ? c.textPrimary : c.textTertiary);
                right -= 28;
            }
            const float tx = r.x + 42, tw = std::max(0.0f, right - tx);
            const Color fg = row.checked || hot ? c.textPrimary : c.textSecondary;
            if (row.detail.empty()) {
                p.Text(row.label, {tx, r.y, tw, r.h}, TextStyle::Body, fg);
            } else {
                p.Text(row.label, {tx, r.y + 7, tw, 20}, TextStyle::Body, fg);
                p.Text(row.detail, {tx, r.y + 27, tw, 17}, TextStyle::Small, c.textTertiary);
            }
        }
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const auto rows = rows_();
        const int i = RowAt(e.y, rows);
        const bool x = i >= 0 && rows[static_cast<size_t>(i)].remove && e.x >= RowRect(i, rows).Right() - 40;
        if (i != hover_ || x != hoverX_) {
            hover_ = i;
            hoverX_ = x;
            Invalidate();
        }
        return true;
    }
    void OnMouseLeave() override {
        hover_ = -1;
        hoverX_ = false;
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        const auto rows = rows_();
        const int i = RowAt(e.y, rows);
        if (i < 0) return true;
        focusRow_ = i;
        Activate(i, rows[static_cast<size_t>(i)].remove && e.x >= RowRect(i, rows).Right() - 40);
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
    // One choice is on at a time: list items with radio semantics (the chosen row is selected).
    void Describe(A11y& a) override {
        a.role = Role::List;
        const auto rows = rows_();
        if (!name.empty()) a.name = name;
        if (rows.empty()) a.name = empty_;
        a.items = static_cast<int>(rows.size());
        a.item = [this, rows](int i, A11y::Item& it) {
            const ChoiceRow& row = rows[static_cast<size_t>(i)];
            it.name = row.detail.empty() ? row.label : row.label + L", " + row.detail;
            it.rect = RowRect(i, rows);
            it.selected = row.checked;
        };
        a.selectItem = a.invokeItem = [this](int i) { Activate(i, false); };
        a.focusedItem = focusRow_;
    }

private:
    // Two-line rows when any row has a detail line, so the list keeps one rhythm.
    static float RowH(const std::vector<ChoiceRow>& rows) {
        for (const ChoiceRow& r : rows)
            if (!r.detail.empty()) return 52;
        return 40;
    }
    Rect RowRect(int i, const std::vector<ChoiceRow>& rows) const {
        const float h = RowH(rows);
        return {rect.x + pad.l, rect.y + h * static_cast<float>(i), rect.w - pad.H(), h};
    }
    int RowAt(float y, const std::vector<ChoiceRow>& rows) const {
        if (rows.empty()) return -1;
        const int i = static_cast<int>(std::floor((y - rect.y) / RowH(rows)));
        return i >= 0 && i < static_cast<int>(rows.size()) ? i : -1;
    }
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
        if (external) r.detail = r.detail.empty() ? std::wstring(Tr(L"external")) : r.detail + L" · " + Tr(L"external");
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

// One list: off, the file's own tracks, then the added ones (marked "external"); adding, finding
// online and removing sit on its title row.
// Off, the file's own tracks, then the added ones; adding (and, for subtitles, finding online)
// as rows under the list.
void AddTracksSection(Widget* page, const wchar_t* type) {
    const bool subs = std::wstring_view(type) == L"sub", video = std::wstring_view(type) == L"video";
    page->Add<Widget>()->height = Length::Px(6);
    auto* list = page->Add<ChoiceList>(
        [type] {
            std::vector<ChoiceRow> rows = OffRows(type);
            for (bool external : {false, true})
                for (ChoiceRow& r : TrackRows(type, external)) rows.push_back(std::move(r));
            return rows;
        },
        Tr(L"No tracks"));
    list->name = Tr(video ? L"Video" : subs ? L"Subtitles" : L"Audio");
    if (video) return;
    if (subs) {
        auto* find = page->Add<ListRow>("search", Tr(L"Find subtitles online"), nullptr, false);
        find->onClick = [] {
            if (!PS().fileLoaded) return;
            // The dialog replaces the popover; close it after the click returns.
            if (MainWindow* w = TheApp().main())
                w->Post([] {
                    ClosePlayerPanels();
                    ShowOnlineSubtitlesDialog();
                });
        };
    }
    auto* add = page->Add<ListRow>("add", Tr(subs ? L"Add subtitle file…" : L"Add audio file…"), nullptr, false);
    add->onClick = [subs] {
        if (!PS().fileLoaded) return;
        for (const std::wstring& f : PickFiles(subs ? 2 : 1)) {
            if (subs) TheApp().player().AddSubtitle(f);
            else TheApp().player().AddAudio(f);
        }
    };
}

}  // namespace

void AddTrackLists(Widget* page, const wchar_t* type) {
    AddTracksSection(page, type);
}

std::vector<ChoiceRow> OutputDeviceRows() {
    Live().RefreshDevices();
    return DeviceRows();
}

Widget* AddChoiceList(Widget* page, std::function<std::vector<ChoiceRow>()> rows, std::wstring name) {
    auto* list = page->Add<ChoiceList>(std::move(rows));
    list->name = std::move(name);
    return list;
}

}  // namespace app::panel
