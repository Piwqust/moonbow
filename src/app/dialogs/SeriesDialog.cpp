// "Series Settings…" in a library video's menu: the user says what a folder is when its file
// names don't (library::SeriesOverride). Kept in <data>\series.json:
//   {"version": 1, "folders": [{"folder": "D:\\Anime\\Show", "show": "Show", "season": 2},
//                              {"folder": "D:\\Clips", "notSeries": true}]}
#include "app/Dialogs.h"

#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "library/MediaTypes.h"

#include <algorithm>

namespace app {

using namespace ui;
using namespace dlg;

namespace {

void SaveSeriesOverrides() {
    json::Value folders = json::Value::MakeArray();
    for (const auto& [folder, o] : library::SeriesOverrides()) {
        json::Value v = json::Value::MakeObject();
        v.Set("folder", json::ToUtf8(folder));
        if (o.notSeries) {
            v.Set("notSeries", true);
        } else {
            if (!o.show.empty()) v.Set("show", json::ToUtf8(o.show));
            v.Set("season", o.season);
        }
        folders.Push(std::move(v));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("folders", std::move(folders));
    json::WriteFileAtomic(DataFilePath(L"series.json"), json::Serialize(doc, true));
}

std::wstring Trimmed(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && iswspace(s[i])) ++i;
    return s.substr(i);
}

}  // namespace

void LoadSeriesOverrides() {
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"series.json"), &text)) return;
    const auto doc = json::Parse(text);
    if (!doc) return;
    if (const json::Value* folders = doc->Find("folders"))
        for (const json::Value& v : folders->AsArray()) {
            const std::wstring folder = v.GetWString("folder");
            if (folder.empty()) continue;
            library::SeriesOverride o;
            o.notSeries = v.GetBool("notSeries");
            o.show = v.GetWString("show");
            o.season = static_cast<int>(std::clamp<int64_t>(v.GetInt("season", 1), 0, 99));
            library::SetSeriesOverride(folder, &o);
        }
}

void ShowSeriesDialog(const std::wstring& videoPath) {
    MainWindow* w = TheApp().main();
    if (!w || videoPath.empty()) return;
    const std::wstring folder = library::ParentFolder(videoPath);
    library::SeriesOverride current;
    const bool had = library::SeriesOverrideFor(folder, &current);
    library::EpisodeInfo ep;
    const bool series = had ? !current.notSeries : library::ParseEpisodePath(videoPath, &ep);
    if (!had && series) {
        current.show = ep.show;
        current.season = ep.season;
    }
    if (current.show.empty()) current.show = library::FileNameOf(folder);

    auto frame = std::make_unique<Frame>(Tr(L"Series Settings"), FitWidth(w, 560));
    Frame* f = frame.get();
    Widget* body = f->Body();
    AddFieldHeader(body, Tr(L"Folder"), Breadcrumb(folder));
    auto* isSeries = body->Add<Checkbox>(Tr(L"The videos in this folder are episodes of a series"), series);
    AddFieldHeader(body, Tr(L"Series name"), Tr(L"Episodes of the same name in other folders join it."));
    auto* name = body->Add<TextBox>(Tr(L"e.g., The Office"));
    name->width = Length::Grow();
    name->height = Length::Px(37);
    name->SetText(current.show);
    AddFieldHeader(body, Tr(L"Season"), Tr(L"Episode numbers come from the file names."));
    auto* season = body->Add<TextBox>(L"1");
    season->width = Length::Px(120);
    season->height = Length::Px(37);
    season->SetText(std::to_wstring(std::max(current.season, 0)));
    auto sync = [name, season](bool on) {
        name->enabled = on;
        season->enabled = on;
        name->Invalidate();
        season->Invalidate();
    };
    sync(series);
    isSeries->onChange = sync;

    if (had) {
        auto* reset = f->Footer()->Add<DButton>(Tr(L"Read From Names"), BtnStyle::Neutral);
        reset->tooltip = Tr(L"Forget these settings: the file names say again what the folder is");
        reset->onClick = [f, folder] {
            library::SetSeriesOverride(folder, nullptr);
            SaveSeriesOverrides();
            TheApp().libraryChanged.Emit();
            f->Close();
        };
    }
    f->Footer()->Add<Spacer>();
    f->Footer()->Add<DButton>(Tr(L"Cancel"), BtnStyle::Neutral)->onClick = [f] { f->Close(); };
    auto* save = f->Footer()->Add<DButton>(Tr(L"Save"), BtnStyle::Accent);
    auto submit = [f, folder, isSeries, name, season] {
        library::SeriesOverride o;
        o.notSeries = !isSeries->Value();
        o.show = Trimmed(name->Text());
        o.season = std::clamp(_wtoi(season->Text().c_str()), 0, 99);
        library::SetSeriesOverride(folder, &o);
        SaveSeriesOverrides();
        TheApp().libraryChanged.Emit();
        f->Close();
    };
    save->onClick = submit;
    f->onSubmit = submit;
    ShowFrame(w, std::move(frame), name);
}

}  // namespace app
