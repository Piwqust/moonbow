// "Add Subtitles" dialog (design/ref/Add_Subtitles_to_Playlist_Folder_*.png) and the
// memory of video -> subtitle pairs.
//
// Pairs live in %APPDATA%\Lumen\subtitle-pairs.json:
//   {"version": 1, "pairs": [{"video": "D:\\...\\E01.mkv", "subs": ["D:\\...\\E01.en.srt"]}]}
// Whenever a file finishes loading, its remembered subtitles are added with sub-add
// (InstallSubtitleMemory, called from InitDialogs).
#include "app/Dialogs.h"

#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "gfx/Text.h"
#include "library/MediaTypes.h"
#include "net/Online.h"

#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <memory>
#include <set>

namespace app {

using namespace ui;
using namespace dlg;

namespace {

// --- pair store ------------------------------------------------------------------------------

struct PairStore {
    bool loaded = false;
    std::map<std::wstring, std::pair<std::wstring, std::vector<std::wstring>>> pairs;  // PathKey -> (video, subs)

    void Load() {
        if (loaded) return;
        loaded = true;
        std::string text;
        if (!json::ReadFileUtf8(DataFilePath(L"subtitle-pairs.json"), &text)) return;
        auto doc = json::Parse(text);
        if (!doc) return;
        if (const json::Value* list = doc->Find("pairs"))
            for (const json::Value& e : list->AsArray()) {
                const std::wstring video = e.GetWString("video");
                if (video.empty()) continue;
                std::vector<std::wstring> subs;
                if (const json::Value* s = e.Find("subs"))
                    for (const json::Value& v : s->AsArray())
                        if (v.IsString()) subs.push_back(v.AsWString());
                if (!subs.empty()) pairs[library::PathKey(video)] = {video, std::move(subs)};
            }
    }
    void Save() const {
        json::Value doc = json::Value::MakeObject();
        doc.Set("version", json::Value(1));
        json::Value list = json::Value::MakeArray();
        for (const auto& [key, entry] : pairs) {
            json::Value e = json::Value::MakeObject();
            e.Set("video", json::Value(json::ToUtf8(entry.first)));
            json::Value subs = json::Value::MakeArray();
            for (const auto& s : entry.second) subs.Push(json::Value(json::ToUtf8(s)));
            e.Set("subs", std::move(subs));
            list.Push(std::move(e));
        }
        doc.Set("pairs", std::move(list));
        json::WriteFileAtomic(DataFilePath(L"subtitle-pairs.json"), json::Serialize(doc, true));
    }
};

PairStore& Store() {
    static PairStore store;
    store.Load();
    return store;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// mpv (subtitles.autoLoad) already picks up subtitles named exactly like the video.
bool AutoLoadedByMpv(const std::wstring& video, const std::wstring& sub) {
    if (!TheApp().config().GetBool("subtitles.autoLoad")) return false;
    return library::PathKey(library::ParentFolder(video)) == library::PathKey(library::ParentFolder(sub)) &&
           Lower(library::StemOf(video)) == Lower(library::StemOf(sub));
}

void AttachRemembered() {
    const PlayerState& s = TheApp().player().State();
    if (!s.fileLoaded || s.path.empty()) return;
    auto& pairs = Store().pairs;
    auto it = pairs.find(library::PathKey(s.path));
    if (it == pairs.end()) return;
    for (const auto& sub : it->second.second)
        if (!AutoLoadedByMpv(s.path, sub) && PathFileExistsW(sub.c_str())) TheApp().player().AddSubtitle(sub);
}

// --- matching --------------------------------------------------------------------------------

std::vector<std::wstring> ListFiles(const std::wstring& folder, bool subtitles, bool subfolders) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    std::vector<std::wstring> dirs;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        const std::wstring path = folder + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (subfolders && Lower(name).find(L"sub") != std::wstring::npos) dirs.push_back(path);
            continue;
        }
        if (subtitles ? library::IsSubtitlePath(path) : library::KindOf(path) == library::MediaKind::Video)
            out.push_back(path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    // "Subs" / "Subtitles" folders next to the videos (one level).
    for (const auto& d : dirs) {
        auto more = ListFiles(d, subtitles, false);
        out.insert(out.end(), more.begin(), more.end());
    }
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(library::FileNameOf(a).c_str(), library::FileNameOf(b).c_str()) < 0;
    });
    return out;
}

bool IsFolder(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// --- dialog state ------------------------------------------------------------------------------

struct SubsModel {
    std::vector<std::wstring> videos;  // "To This Folder" targets
    std::wstring current;              // "To This File" target
    std::wstring videoFolder;
    std::vector<std::wstring> subs;    // candidates
    std::wstring sourceFolder;
    bool fromFiles = false;            // source picked as files instead of a folder
    bool fileMode = false;             // target: the current file
    std::map<std::wstring, std::vector<std::wstring>> assign;  // video PathKey -> subs
    std::set<std::wstring> checked;    // file mode: PathKey of subs

    void LoadSubs() {
        if (!fromFiles) subs = sourceFolder.empty() ? std::vector<std::wstring>{} : ListFiles(sourceFolder, true, true);
    }
    void AutoMatch() {
        assign.clear();
        checked.clear();
        for (const auto& v : videos) {
            std::vector<std::pair<int, std::wstring>> scored;
            for (const auto& s : subs)
                if (int sc = library::SubtitleMatchScore(v, s)) scored.push_back({sc, s});
            if (scored.empty()) continue;
            std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
            auto& list = assign[library::PathKey(v)];
            // Several languages of the same episode all go to it; otherwise the best guess.
            for (const auto& [sc, s] : scored)
                if (sc >= 90 || list.empty()) list.push_back(s);
        }
        if (!current.empty())
            for (const auto& s : subs)
                if (library::SubtitleMatchScore(current, s) >= 60) checked.insert(library::PathKey(s));
        if (!current.empty() && checked.empty() && subs.size() == 1) checked.insert(library::PathKey(subs[0]));
    }
    std::vector<std::wstring>& For(const std::wstring& video) { return assign[library::PathKey(video)]; }
    int MatchedVideos() const {
        int n = 0;
        for (const auto& v : videos) {
            auto it = assign.find(library::PathKey(v));
            if (it != assign.end() && !it->second.empty()) ++n;
        }
        return n;
    }
};

using Model = std::shared_ptr<SubsModel>;

// Clickable field with a folder icon and the source path as a breadcrumb.
class PathField : public Widget {
public:
    PathField() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(34);
    }
    std::wstring text;
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = IsHot() || IsPressed();
        p.Fill(rect, hot ? Color::Mix(c.bgRaised, c.bgSelected, 0.5f) : c.bgRaised, 6);
        p.Icon("folder", {rect.x + 8, rect.Cy() - 12, 24, 24}, hot ? c.textPrimary : c.textSecondary);
        p.Text(text, {rect.x + 42, rect.y, rect.w - 54, rect.h}, TextStyle::Body, hot ? c.textPrimary : c.textSecondary);
        PaintFocusRing(p, rect, 6);
    }
};

// Folder mode row: video name, dashed connector, matched subtitle (click to change).
class PairRow : public Widget {
public:
    PairRow(Model m, std::wstring video, std::function<void()> changed)
        : model_(std::move(m)), video_(std::move(video)), changed_(std::move(changed)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(40);
    }
    Rect SubRect() const {
        const float mid = std::round(rect.x + rect.w / 2);
        return {mid + 20, rect.y + 2, rect.Right() - mid - 40, 36};
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const auto& subs = model_->For(video_);
        const float mid = std::round(rect.x + rect.w / 2);
        p.Icon("video", {rect.x + 30, rect.Cy() - 12, 24, 24}, c.textTertiary);
        const std::wstring name = library::FileNameOf(video_);
        const float maxText = mid - rect.x - 68 - 40;
        const float tw = std::min(gfx::Text::Get().Measure(name, TextStyle::Body).width, maxText);
        p.Text(name, {rect.x + 68, rect.y, maxText, rect.h}, TextStyle::Body, c.textSecondary);
        const Color lineC = subs.empty() ? c.line : Color::Mix(c.line, c.lineStrong, 0.7f);
        DashedLine(p, rect.x + 68 + tw + 14, mid, std::round(rect.Cy()), lineC);
        const Rect r = SubRect();
        const Point m = GetHost() ? GetHost()->MousePos() : Point{};
        const bool hot = IsHot() && r.Contains(m.x, m.y);
        if (hot) p.Fill(r, c.hover, 6);
        if (IsFocused()) PaintFocusRing(p, r, 6);
        if (subs.empty()) {
            p.Icon("add", {r.x + 8, r.Cy() - 12, 24, 24}, hot ? c.textPrimary : c.textTertiary);
            p.Text(Tr(L"Not matched — choose a subtitle"), {r.x + 46, r.y, r.w - 54, r.h}, TextStyle::Body,
                   hot ? c.textSecondary : c.textTertiary);
            return;
        }
        p.Icon("file", {r.x + 8, r.Cy() - 12, 24, 24}, hot ? c.textPrimary : c.textSecondary);
        std::wstring label = library::FileNameOf(subs[0]);
        std::wstring more = subs.size() > 1 ? L"+" + std::to_wstring(subs.size() - 1) : L"";
        const float mw = more.empty() ? 0 : gfx::Text::Get().Measure(more, TextStyle::Small).width + 16;
        p.Text(label, {r.x + 46, r.y, r.w - 54 - mw - 30, r.h}, TextStyle::Body, hot ? c.textPrimary : c.textSecondary);
        if (!more.empty()) {
            const float lw = std::min(gfx::Text::Get().Measure(label, TextStyle::Body).width, r.w - 54 - mw - 30);
            const Rect badge{r.x + 46 + lw + 8, r.Cy() - 10, mw, 20};
            p.Fill(badge, c.bgSelected, 4);
            p.Text(more, badge, TextStyle::Small, c.textSecondary, HAlign::Center);
        }
        if (hot) p.Icon("chevron_down", {r.Right() - 32, r.Cy() - 12, 24, 24}, c.textSecondary);
    }
    bool OnMouseMove(const MouseEvent&) override {
        Invalidate();
        return true;
    }
    void OnMouseLeave() override { Invalidate(); }
    void OnClick() override {
        Host* host = GetHost();
        if (!host || model_->subs.empty()) return;
        std::vector<MenuItem> items;
        auto& subs = model_->For(video_);
        std::vector<std::pair<int, std::wstring>> ranked;
        for (const auto& s : model_->subs) ranked.push_back({library::SubtitleMatchScore(video_, s), s});
        std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });
        MenuItem none;
        none.label = Tr(L"No subtitles");
        none.checked = subs.empty();
        none.action = [this] {
            model_->For(video_).clear();
            Changed();
        };
        items.push_back(std::move(none));
        items.push_back(MenuItem::Separator());
        for (const auto& [score, s] : ranked) {
            MenuItem it;
            it.label = library::FileNameOf(s);
            it.icon = "file";
            it.checked = std::find(subs.begin(), subs.end(), s) != subs.end();
            const std::wstring sub = s;
            it.action = [this, sub] {
                auto& list = model_->For(video_);
                auto f = std::find(list.begin(), list.end(), sub);
                if (f != list.end()) list.erase(f);
                else list.push_back(sub);
                Changed();
            };
            items.push_back(std::move(it));
            if (items.size() > 40) break;
        }
        ShowMenu(host, SubRect(), std::move(items), Place::Below, SubRect().w);
    }

private:
    void Changed() {
        Invalidate();
        if (changed_) changed_();
    }
    Model model_;
    std::wstring video_;
    std::function<void()> changed_;
};

// File mode row: a subtitle file with a check mark when it will be added.
class SubRow : public Widget {
public:
    SubRow(Model m, std::wstring sub, std::function<void()> changed)
        : model_(std::move(m)), sub_(std::move(sub)), changed_(std::move(changed)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(36);
        onClick = [this] {
            const std::wstring key = library::PathKey(sub_);
            if (!model_->checked.erase(key)) model_->checked.insert(key);
            Invalidate();
            if (changed_) changed_();
        };
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool on = model_->checked.count(library::PathKey(sub_)) > 0;
        if (on) p.Fill(rect, c.bgDeep, 6);
        else if (IsHot()) p.Fill(rect, c.hover, 6);
        PaintFocusRing(p, rect, 6);
        p.Icon("file", {rect.x + 8, rect.Cy() - 12, 24, 24}, on || IsHot() ? c.textPrimary : c.textSecondary);
        std::wstring name = library::FileNameOf(sub_);
        const std::wstring parent = library::FileNameOf(library::ParentFolder(sub_));
        if (library::PathKey(library::ParentFolder(sub_)) != library::PathKey(model_->sourceFolder) && !parent.empty())
            name = parent + L" / " + name;
        p.Text(name, {rect.x + 46, rect.y, rect.w - 90, rect.h}, TextStyle::Body, on ? SoftText() : c.textSecondary);
        if (on) p.Icon("check_active", {rect.Right() - 32, rect.Cy() - 12, 24, 24}, c.accent);
    }

private:
    Model model_;
    std::wstring sub_;
    std::function<void()> changed_;
};

class SubsDialog {
public:
    SubsDialog(MainWindow* w, Model m) : window_(w), model_(std::move(m)) {}

    std::unique_ptr<Frame> Build() {
        auto frame = std::make_unique<Frame>(Tr(L"Add Subtitles"), FitWidth(window_, 1048), FitHeight(window_, 800));
        frame_ = frame.get();
        target_ = frame_->Toolbar()->Add<FieldDropdown>(
            std::vector<Choice>{{Tr(L"To This Folder"), "folder"}, {Tr(L"To This File"), "file"}},
            model_->fileMode ? 1 : 0);
        target_->width = Length::Px(200);
        target_->height = Length::Px(34);
        if (model_->current.empty()) target_->SetChoices({{Tr(L"To This Folder"), "folder"}}, 0);
        source_ = frame_->Toolbar()->Add<FieldDropdown>(
            std::vector<Choice>{{Tr(L"From Folder"), "folder"}, {Tr(L"From Files"), "files"}}, 0);
        source_->width = Length::Px(200);
        source_->height = Length::Px(34);
        path_ = frame_->Toolbar()->Add<PathField>();

        Widget* body = frame_->Body();
        body->pad = {};
        body->gap = 0;
        scroll_ = body->Add<ScrollView>();
        scroll_->width = Length::Grow();
        scroll_->height = Length::Grow();

        frame_->Footer()->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [this] { frame_->Close(); };
        // OpenSubtitles for the playing file (off and explained in the next dialog until the
        // user turns it on with their key).
        if (!model_->current.empty()) {
            auto* online = frame_->Footer()->Add<DButton>(Tr(L"Find Online"), BtnStyle::Neutral);
            online->icon = "search";
            online->tooltip = Tr(L"Search OpenSubtitles for subtitles to this file");
            online->onClick = [this] {
                const std::wstring video = model_->current;
                frame_->Close();
                if (MainWindow* w = TheApp().main()) w->Post([video] { ShowOnlineSubtitlesDialog(video); });
            };
        }
        status_ = frame_->Footer()->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
        status_->pad = {8, 0, 0, 0};
        frame_->Footer()->Add<Spacer>();
        apply_ = frame_->Footer()->Add<DButton>(Tr(L"Save"), BtnStyle::Accent);
        apply_->onClick = [this] { Apply(); };
        frame_->onSubmit = [this] {
            if (apply_->IsEnabled()) Apply();
        };

        target_->onChange = [this](int i) {
            model_->fileMode = i == 1;
            Rebuild();
        };
        source_->onChange = [this](int i) {
            if (i == 1) {
                auto files = PickFiles(window_->Hwnd(), true, SubtitleFilters(), model_->sourceFolder);
                if (files.empty()) {
                    source_->SetSelected(model_->fromFiles ? 1 : 0);
                    return;
                }
                model_->fromFiles = true;
                model_->subs = files;
            } else {
                model_->fromFiles = false;
                model_->LoadSubs();
            }
            model_->AutoMatch();
            Rebuild();
        };
        path_->onClick = [this] { ChooseSource(); };
        Rebuild();
        return frame;
    }

private:
    static std::vector<FileFilter> SubtitleFilters() {
        return {{Tr(L"Subtitles"), L"*.srt;*.ass;*.ssa;*.vtt;*.sub;*.sup;*.idx;*.smi"}, {Tr(L"All files"), L"*.*"}};
    }

    void ChooseSource() {
        if (model_->fromFiles) {
            auto files = PickFiles(window_->Hwnd(), true, SubtitleFilters(), model_->sourceFolder);
            if (files.empty()) return;
            model_->subs = files;
            model_->sourceFolder = library::ParentFolder(files[0]);
        } else {
            const std::wstring folder = PickFolder(window_->Hwnd(), model_->sourceFolder);
            if (folder.empty()) return;
            model_->sourceFolder = folder;
            model_->LoadSubs();
        }
        model_->AutoMatch();
        Rebuild();
    }

    void ChooseVideoFolder() {
        const std::wstring folder = PickFolder(window_->Hwnd(), model_->videoFolder);
        if (folder.empty()) return;
        model_->videoFolder = folder;
        model_->videos = ListFiles(folder, false, false);
        if (model_->sourceFolder.empty()) {
            model_->sourceFolder = folder;
            model_->LoadSubs();
        }
        model_->AutoMatch();
        Rebuild();
    }

    void EmptyState(Widget* content, const std::wstring& text, const std::wstring& button, std::function<void()> fn) {
        content->align = Align::Center;
        content->pad = {30, 80, 30, 30};
        content->gap = 16;
        content->Add<Label>(text, TextStyle::Body, Tone::Tertiary);
        auto* b = content->Add<DButton>(button, BtnStyle::Neutral);
        b->minWidth = 160;
        b->onClick = std::move(fn);
    }

    void Rebuild() {
        Widget* content = scroll_->SetContent<Widget>();
        content->dir = Dir::Column;
        content->width = Length::Grow();
        auto changed = [this] { UpdateStatus(); };
        if (model_->fromFiles)
            path_->text = model_->subs.size() == 1 ? library::FileNameOf(model_->subs[0])
                                                   : Fmt(Tr(L"{0} subtitle files"), {std::to_wstring(model_->subs.size())});
        else
            path_->text = model_->sourceFolder.empty() ? Tr(L"Choose a folder with subtitles")
                                                       : Breadcrumb(model_->sourceFolder);
        path_->tooltip = model_->sourceFolder;
        path_->Invalidate();

        if (model_->fileMode) {
            content->pad = {20, 22, 20, 22};
            content->gap = 4;
            if (model_->subs.empty()) {
                EmptyState(content, Tr(L"No subtitle files found here."), Tr(L"Choose folder"), [this] { ChooseSource(); });
            } else {
                // Best matches for the current file first.
                std::vector<std::pair<int, std::wstring>> ranked;
                for (const auto& s : model_->subs) ranked.push_back({library::SubtitleMatchScore(model_->current, s), s});
                std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });
                for (const auto& [score, s] : ranked) content->Add<SubRow>(model_, s, changed);
            }
        } else {
            content->pad = {0, 16, 0, 16};
            if (model_->videos.empty()) {
                EmptyState(content, Tr(L"There are no videos to add subtitles to."), Tr(L"Choose video folder"),
                           [this] { ChooseVideoFolder(); });
            } else if (model_->subs.empty()) {
                EmptyState(content, Tr(L"No subtitle files found here."), Tr(L"Choose folder"), [this] { ChooseSource(); });
            } else {
                for (const auto& v : model_->videos) content->Add<PairRow>(model_, v, changed);
            }
        }
        scroll_->ScrollTo(0, false);
        scroll_->InvalidateLayout();
        UpdateStatus();
    }

    void UpdateStatus() {
        int count = 0;
        if (model_->fileMode) {
            count = static_cast<int>(model_->checked.size());
            status_->SetText(count ? Fmt(Tr(L"{0} selected"), {std::to_wstring(count)}) : L"");
        } else {
            count = model_->MatchedVideos();
            status_->SetText(model_->videos.empty() || model_->subs.empty()
                                 ? L""
                                 : Fmt(Tr(L"{0} of {1} videos matched"),
                                       {std::to_wstring(count), std::to_wstring(model_->videos.size())}));
        }
        apply_->enabled = count > 0;
        apply_->Invalidate();
        status_->InvalidateLayout();
    }

    void Apply() {
        auto& store = Store();
        const std::wstring playing = TheApp().player().State().fileLoaded ? TheApp().player().State().path : L"";
        int videos = 0;
        auto remember = [&](const std::wstring& video, const std::vector<std::wstring>& subs) {
            const std::wstring key = library::PathKey(video);
            if (subs.empty()) {
                store.pairs.erase(key);
                return;
            }
            store.pairs[key] = {video, subs};
            ++videos;
            if (!playing.empty() && library::PathKey(playing) == key)
                for (const auto& s : subs) TheApp().player().AddSubtitle(s);
        };
        if (model_->fileMode) {
            std::vector<std::wstring> subs;
            for (const auto& s : model_->subs)
                if (model_->checked.count(library::PathKey(s))) subs.push_back(s);
            remember(model_->current, subs);
        } else {
            for (const auto& v : model_->videos) remember(v, model_->For(v));
        }
        store.Save();
        MainWindow* w = window_;
        frame_->Close();
        const std::wstring msg = videos == 1 ? std::wstring(Tr(L"Subtitles added"))
                                             : Fmt(Tr(L"Subtitles added to {0} videos"), {std::to_wstring(videos)});
        ShowToast(w, msg, "subtitles");
    }

    MainWindow* window_;
    Model model_;
    Frame* frame_ = nullptr;
    FieldDropdown* target_ = nullptr;
    FieldDropdown* source_ = nullptr;
    PathField* path_ = nullptr;
    ScrollView* scroll_ = nullptr;
    Label* status_ = nullptr;
    DButton* apply_ = nullptr;
};

// --- online search (OpenSubtitles) ------------------------------------------------------------

struct LangName {
    const char* code;
    const wchar_t* en;
    const wchar_t* ru;
};
const LangName kLangNames[] = {
    {"en", L"English", L"Английский"},     {"ru", L"Russian", L"Русский"},       {"uk", L"Ukrainian", L"Украинский"},
    {"kk", L"Kazakh", L"Казахский"},       {"de", L"German", L"Немецкий"},       {"fr", L"French", L"Французский"},
    {"es", L"Spanish", L"Испанский"},      {"it", L"Italian", L"Итальянский"},   {"pt-pt", L"Portuguese", L"Португальский"},
    {"pt-br", L"Portuguese (Brazil)", L"Португальский (Бразилия)"},             {"pl", L"Polish", L"Польский"},
    {"tr", L"Turkish", L"Турецкий"},       {"ar", L"Arabic", L"Арабский"},       {"id", L"Indonesian", L"Индонезийский"},
    {"ja", L"Japanese", L"Японский"},      {"zh-cn", L"Chinese", L"Китайский"},  {"zh-tw", L"Chinese (Traditional)", L"Китайский (традиционный)"},
    {"ko", L"Korean", L"Корейский"},       {"nl", L"Dutch", L"Нидерландский"},   {"sv", L"Swedish", L"Шведский"},
    {"cs", L"Czech", L"Чешский"},          {"he", L"Hebrew", L"Иврит"},          {"hu", L"Hungarian", L"Венгерский"},
    {"ro", L"Romanian", L"Румынский"},     {"el", L"Greek", L"Греческий"},       {"fi", L"Finnish", L"Финский"},
    {"bg", L"Bulgarian", L"Болгарский"},   {"sr", L"Serbian", L"Сербский"},      {"vi", L"Vietnamese", L"Вьетнамский"},
};

std::wstring LanguageName(const std::string& code) {
    std::string c = code;
    for (char& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const LangName& l : kLangNames)
        if (c == l.code || (c.size() == 2 && std::string(l.code).rfind(c + "-", 0) == 0))
            return CurrentLanguage() == Language::Russian ? l.ru : l.en;
    std::wstring w = Utf8ToWide(code);
    for (wchar_t& ch : w) ch = static_cast<wchar_t>(std::towupper(ch));
    return w;
}

// OpenSubtitles writes Portuguese and Chinese with a region; the rest is ISO 639-1.
std::string OsLanguageCode(const std::string& code) {
    if (code == "pt") return "pt-pt";
    if (code == "zh") return "zh-cn";
    return code;
}

std::wstring Grouped(long long n) {
    std::wstring s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), L" ");
    return s;
}

// One search result: the release name, then language, downloads, rating and the flags.
class OnlineSubRow : public Widget {
public:
    OnlineSubRow(net::SubtitleResult r, std::function<void(const net::SubtitleResult&)> pick)
        : r_(std::move(r)), pick_(std::move(pick)) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(56);
        tooltip = r_.release;
        onClick = [this] {
            if (pick_ && IsEnabled()) pick_(r_);
        };
        std::wstring d = LanguageName(WideToUtf8(r_.language));
        d += L" · " + Grouped(r_.downloads) + L" " +
             (CurrentLanguage() == Language::Russian ? Plural(r_.downloads, L"загрузка", L"загрузки", L"загрузок")
                                                     : r_.downloads == 1 ? L"download" : L"downloads");
        if (r_.rating > 0 && r_.votes > 0) {
            wchar_t buf[16];
            swprintf_s(buf, L"%.1f", r_.rating);
            std::wstring v = buf;
            if (CurrentLanguage() == Language::Russian) std::replace(v.begin(), v.end(), L'.', L',');
            d += L" · ★ " + v;
        }
        if (r_.hearingImpaired) d += L" · " + std::wstring(Tr(L"for the hard of hearing"));
        if (r_.foreignOnly) d += L" · " + std::wstring(Tr(L"foreign parts only"));
        if (r_.machine) d += L" · " + std::wstring(Tr(L"machine translation"));
        else if (r_.ai) d += L" · " + std::wstring(Tr(L"AI translation"));
        detail_ = std::move(d);
        if (r_.hashMatch) tooltip += L"\n" + std::wstring(Tr(L"Made for this exact file"));
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = (IsHot() || IsPressed()) && IsEnabled();
        if (hot) p.Fill(rect, IsPressed() ? c.press : c.hover, 6);
        PaintFocusRing(p, rect, 6);
        const Color fg = !IsEnabled() ? c.textDisabled : hot ? c.textPrimary : c.textSecondary;
        p.Icon(r_.hashMatch ? "check_selected" : "subtitles", {rect.x + 10, rect.Cy() - 12, 24, 24},
               r_.hashMatch ? c.accent : fg, c.accentMid);
        const float right = hot ? 40.0f : 12.0f;
        p.Text(r_.release, {rect.x + 46, rect.y + 7, rect.w - 46 - right, 22}, TextStyle::Body, fg);
        p.Text(detail_, {rect.x + 46, rect.y + 29, rect.w - 46 - right, 18}, TextStyle::Small, c.textTertiary);
        if (hot) p.Icon("save", {rect.Right() - 34, rect.Cy() - 12, 24, 24}, c.textPrimary);
    }

private:
    net::SubtitleResult r_;
    std::wstring detail_;
    std::function<void(const net::SubtitleResult&)> pick_;
};

class OnlineSubsDialog : public std::enable_shared_from_this<OnlineSubsDialog> {
public:
    OnlineSubsDialog(MainWindow* w, std::wstring video) : window_(w), video_(std::move(video)) {}

    std::unique_ptr<Frame> Build() {
        auto frame = std::make_unique<Frame>(Tr(L"Find Subtitles Online"), FitWidth(window_, 900), FitHeight(window_, 700));
        frame_ = frame.get();
        Widget* bar = frame_->Toolbar();
        const net::VideoGuess g = net::online::Guess(video_);
        query_ = bar->Add<TextBox>(Tr(L"Title or file name"), "search");
        query_->raised = true;
        query_->width = Length::Grow();
        query_->height = Length::Px(34);
        query_->SetText(g.title.empty() ? library::StemOf(video_) : g.title);
        query_->tooltip = Tr(L"What to look for. The file's hash is sent too, so exact matches come first.");
        query_->onSubmit = [this](const std::wstring&) { Search(); };
        season_ = bar->Add<TextBox>(Tr(L"Season"));
        season_->raised = true;
        season_->width = Length::Px(84);
        season_->height = Length::Px(34);
        season_->onSubmit = [this](const std::wstring&) { Search(); };
        episode_ = bar->Add<TextBox>(Tr(L"Episode"));
        episode_->raised = true;
        episode_->width = Length::Px(84);
        episode_->height = Length::Px(34);
        episode_->onSubmit = [this](const std::wstring&) { Search(); };
        if (g.kind == net::VideoGuess::Episode) {
            season_->SetText(std::to_wstring(g.season));
            episode_->SetText(std::to_wstring(g.episode));
        }
        // The user's languages together, each alone, or every language.
        langs_ = net::online::PreferredSubtitleLanguages();
        std::vector<Choice> choices;
        std::wstring both;
        for (const std::string& l : langs_) both += (both.empty() ? L"" : L" + ") + LanguageName(l);
        choices.push_back({both, "pref"});
        if (langs_.size() > 1)
            for (const std::string& l : langs_) choices.push_back({LanguageName(l), l});
        choices.push_back({Tr(L"All languages"), "all"});
        lang_ = bar->Add<FieldDropdown>(choices, 0);
        lang_->width = Length::Px(220);
        lang_->height = Length::Px(34);
        lang_->tooltip = Tr(L"Subtitle languages (Settings › Subtitles › Preferred language)");
        lang_->onChange = [this](int) { Search(); };
        search_ = bar->Add<DButton>(Tr(L"Find"), BtnStyle::Accent);
        search_->onClick = [this] { Search(); };

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
        frame_->onSubmit = [this] { Search(); };
        return frame;
    }

    // After ShowFrame: the controller is owned by the frame's close hook.
    void Start() {
        if (!net::online::SubtitlesOn() || !net::online::SubtitlesKeySet()) {
            ShowOff();
            return;
        }
        Search();
        auto self = weak_from_this();
        net::online::SubtitleQuota([self](int remaining, const std::wstring&) {
            if (auto d = self.lock()) d->ShowQuota(remaining);
        });
    }
    Widget* FirstFocus() const { return query_; }
    void Closed() { closed_ = true; }

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

    void Message(const std::wstring& title, const std::wstring& text, bool settingsButton = false) {
        Widget* content = Content();
        content->align = Align::Center;
        content->pad = {40, 70, 40, 30};
        content->gap = 12;
        auto* t = content->Add<Label>(title, TextStyle::Large);
        t->color = SoftText;
        if (!text.empty()) {
            auto* l = content->Add<Label>(text, TextStyle::Body, Tone::Tertiary);
            l->wrap = true;
            l->halign = HAlign::Center;
            l->maxW = 560;
        }
        if (settingsButton) {
            auto* b = content->Add<DButton>(Tr(L"Open Settings"), BtnStyle::Neutral);
            b->minWidth = 160;
            b->onClick = [this] {
                frame_->Close();
                if (MainWindow* w = TheApp().main()) w->Post([] { TheApp().OpenSettings("privacy"); });
            };
        }
    }

    void ShowOff() {
        for (Widget* w : std::initializer_list<Widget*>{query_, season_, episode_, lang_, search_}) w->enabled = false;
        Message(Tr(L"Subtitle search is off"),
                Tr(L"Turn on \"Search for subtitles online\" in Settings › Privacy and paste your own OpenSubtitles "
                   L"API key there. The key is free: sign in at opensubtitles.com, open your profile, then API "
                   L"Consumers, and create one."),
                true);
        status_->SetText({});
    }

    void ShowQuota(int remaining) {
        if (closed_ || busy_) return;
        status_->SetText(remaining >= 0 ? Fmt(Tr(L"Downloads left today: {0}"), {std::to_wstring(remaining)}) : L"");
        status_->InvalidateLayout();
    }

    int Number(TextBox* box) const { return std::max(0, _wtoi(box->Text().c_str())); }

    void Search() {
        if (busy_ || !net::online::SubtitlesOn() || !net::online::SubtitlesKeySet()) return;
        net::online::SubtitleSearch s;
        s.videoPath = video_;
        s.query = query_->Text();
        s.season = Number(season_);
        s.episode = Number(episode_);
        const std::string pick = lang_->Choices()[static_cast<size_t>(std::max(0, lang_->Selected()))].value;
        if (pick == "pref")
            for (const std::string& l : langs_) s.languages.push_back(OsLanguageCode(l));
        else if (pick != "all")
            s.languages.push_back(OsLanguageCode(pick));
        busy_ = true;
        Message(Tr(L"Searching…"), {});
        status_->SetText({});
        auto self = weak_from_this();
        net::online::SearchSubtitles(s, [self](const net::online::SubtitleSearchResult& r) {
            if (auto d = self.lock()) d->ShowResults(r);
        });
    }

    void ShowResults(const net::online::SubtitleSearchResult& r) {
        if (closed_) return;
        busy_ = false;
        if (!r.ok) {
            Message(Tr(L"Couldn't search"), r.error);
            return;
        }
        if (r.items.empty()) {
            Message(Tr(L"Nothing found"), Tr(L"Try another title, the season and episode, or all languages."));
            return;
        }
        Widget* content = Content();
        auto self = weak_from_this();
        for (const net::SubtitleResult& item : r.items)
            content->Add<OnlineSubRow>(item, [self](const net::SubtitleResult& it) {
                if (auto d = self.lock()) d->Download(it);
            });
        int exact = 0;
        for (const auto& it : r.items) exact += it.hashMatch ? 1 : 0;
        std::wstring s = Fmt(Tr(L"Found: {0}"), {std::to_wstring(r.items.size())});
        if (exact) s += L" · " + Fmt(Tr(L"{0} for this exact file"), {std::to_wstring(exact)});
        const int q = net::online::LastSubtitleQuota();
        if (q >= 0) s += L" · " + Fmt(Tr(L"Downloads left today: {0}"), {std::to_wstring(q)});
        status_->SetText(s);
        status_->InvalidateLayout();
    }

    void Download(const net::SubtitleResult& item) {
        if (busy_) return;
        busy_ = true;
        if (Widget* c = scroll_->Content()) c->enabled = false;
        scroll_->Invalidate();
        status_->SetText(Fmt(Tr(L"Downloading {0}…"), {item.release}));
        status_->InvalidateLayout();
        auto self = weak_from_this();
        const std::wstring video = video_;
        net::online::DownloadSubtitle(item, video, [self, video](const net::online::SubtitleFetch& f) {
            if (f.ok) Attach(video, f);
            auto d = self.lock();
            if (!d || d->closed_) return;
            d->busy_ = false;
            if (f.ok) {
                d->frame_->Close();
                return;
            }
            if (Widget* c = d->scroll_->Content()) c->enabled = true;
            d->scroll_->Invalidate();
            d->status_->SetText(f.error);
            d->status_->InvalidateLayout();
        });
    }

    // Loads the downloaded file at once and remembers it for the video (a copy in the data
    // folder is not found by name next time).
    static void Attach(const std::wstring& video, const net::online::SubtitleFetch& f) {
        const PlayerState& s = TheApp().player().State();
        if (s.fileLoaded && library::PathKey(s.path) == library::PathKey(video)) TheApp().player().AddSubtitle(f.path);
        auto& entry = Store().pairs[library::PathKey(video)];
        entry.first = video;
        if (std::find(entry.second.begin(), entry.second.end(), f.path) == entry.second.end()) entry.second.push_back(f.path);
        Store().Save();
        if (MainWindow* w = TheApp().main()) {
            std::wstring msg = Fmt(Tr(L"Subtitles downloaded: {0}"), {library::FileNameOf(f.path)});
            if (f.inDataDir) msg = Fmt(Tr(L"Subtitles saved to Lumen's data folder: {0}"), {library::FileNameOf(f.path)});
            ShowToast(w, msg, "subtitles", 3000);
        }
    }

    MainWindow* window_;
    std::wstring video_;
    std::vector<std::string> langs_;
    Frame* frame_ = nullptr;
    TextBox* query_ = nullptr;
    TextBox* season_ = nullptr;
    TextBox* episode_ = nullptr;
    FieldDropdown* lang_ = nullptr;
    DButton* search_ = nullptr;
    ScrollView* scroll_ = nullptr;
    Label* status_ = nullptr;
    bool busy_ = false, closed_ = false;
};

}  // namespace

void ShowOnlineSubtitlesDialog(const std::wstring& videoPath) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    std::wstring video = videoPath;
    if (video.empty()) {
        const PlayerState& s = TheApp().player().State();
        if (s.fileLoaded && s.path.find(L"://") == std::wstring::npos) video = s.path;
    }
    if (video.empty()) {
        ShowToast(w, Tr(L"Open a video to search subtitles for it"), "subtitles");
        return;
    }
    auto dialog = std::make_shared<OnlineSubsDialog>(w, video);
    auto frame = dialog->Build();
    Widget* focus = dialog->FirstFocus();
    frame->OnClosed([dialog] { dialog->Closed(); });  // keeps the controller alive as long as the frame
    ShowFrame(w, std::move(frame), focus);
    dialog->Start();
}

void dlg::InstallSubtitleMemory() {
    static ScopedConnection conn;
    // The file-loaded event reports file and state changes together.
    conn = Connect(TheApp().playerChanged, [](unsigned mask) {
        if ((mask & kChangeFile) && (mask & kChangeState)) AttachRemembered();
    });
}

void ShowAddSubtitlesDialog(const std::wstring& folderOrPlaylist) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    auto model = std::make_shared<SubsModel>();
    const PlayerState& s = TheApp().player().State();
    if (s.fileLoaded && !s.path.empty() && s.path.find(L"://") == std::wstring::npos) model->current = s.path;

    const std::wstring arg = folderOrPlaylist;
    const library::Playlist* pl = arg.rfind(L"pl-", 0) == 0 ? TheApp().playlists().Find(WideToUtf8(arg)) : nullptr;
    if (pl) {
        for (const auto& item : pl->items)
            if (library::KindOf(item.path) == library::MediaKind::Video) model->videos.push_back(item.path);
    } else if (!arg.empty()) {
        model->videoFolder = IsFolder(arg) ? arg : library::ParentFolder(arg);
        model->videos = ListFiles(model->videoFolder, false, false);
    } else if (!model->current.empty()) {
        model->videoFolder = library::ParentFolder(model->current);
        // The queue when it is a folder/playlist of videos, else the file's folder.
        for (const QueueItem& q : TheApp().queue().Items())
            if (library::KindOf(q.path) == library::MediaKind::Video && q.path.find(L"://") == std::wstring::npos)
                model->videos.push_back(q.path);
        if (model->videos.size() <= 1) model->videos = ListFiles(model->videoFolder, false, false);
        model->fileMode = true;
    }
    if (model->videoFolder.empty() && !model->videos.empty()) model->videoFolder = library::ParentFolder(model->videos[0]);
    model->sourceFolder = model->videoFolder;
    model->LoadSubs();
    model->AutoMatch();
    // Remembered pairs win over the automatic guess.
    for (const auto& v : model->videos) {
        auto it = Store().pairs.find(library::PathKey(v));
        if (it != Store().pairs.end()) model->For(v) = it->second.second;
    }

    auto dialog = std::make_shared<SubsDialog>(w, model);
    auto frame = dialog->Build();
    frame->OnClosed([dialog] {});  // keeps the controller alive as long as the frame
    ShowFrame(w, std::move(frame));
}

}  // namespace app
