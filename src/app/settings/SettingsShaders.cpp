// Settings > Video > Shaders: the shader files in <data>\shaders, each with a toggle. Writes
// video.shaders ("a.glsl|b.hook"), the same list the player's quick settings edit.
#include "app/I18n.h"
#include "app/SettingsActions.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "core/Json.h"

#include <algorithm>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

std::vector<std::string> SplitList(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t bar = list.find('|', start);
        if (bar == std::string::npos) bar = list.size();
        if (bar > start) out.push_back(list.substr(start, bar - start));
        start = bar + 1;
    }
    return out;
}

bool IsShaderFile(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot);
    for (wchar_t& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
    return ext == L".glsl" || ext == L".hook";
}

// File names only; a missing folder is an empty list. A handful of files, so listing them on the
// UI thread when the page is built costs microseconds.
std::vector<std::wstring> ListShaders() {
    std::vector<std::wstring> out;
    const std::wstring dir = DataDir();
    if (dir.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\shaders\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsShaderFile(fd.cFileName)) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return out;
}

class ShaderToggle : public Toggle, public Syncable {
public:
    ShaderToggle(const Item& it, std::string name) : item_(it), name_(std::move(name)) {
        SetValue(On(), false);
        onChange = [this](bool on) {
            std::vector<std::string> list = SplitList(Cfg().GetString(item_.key));
            list.erase(std::remove(list.begin(), list.end(), name_), list.end());
            if (on) list.push_back(name_);  // names of files not on disk stay in the list
            std::string joined;
            for (const std::string& n : list) joined += (joined.empty() ? "" : "|") + n;
            Cfg().SetString(item_.key, joined);
        };
    }
    void Sync() override { SetValue(On()); }

private:
    bool On() const {
        const std::vector<std::string> list = SplitList(Cfg().GetString(item_.key));
        return std::find(list.begin(), list.end(), name_) != list.end();
    }
    const Item& item_;
    std::string name_;
};

class ShaderList : public Widget {
public:
    ShaderList(const Item& it, PageHost& page) : item_(it), page_(page) {
        dir = Dir::Column;
        gap = 8;
        width = Length::Px(kControlW);
        auto* panel = Add<Panel>();
        panel->fill = [] { return P().bgDeep; };
        panel->radius = 6;
        panel->dir = Dir::Column;
        panel->width = Length::Grow();
        panel->pad = {12, 2, 2, 2};
        list_ = panel;
        auto* bar = Add<Widget>();
        bar->dir = Dir::Row;
        bar->gap = 8;
        bar->align = Align::Center;
        bar->width = Length::Grow();
        auto* open = bar->Add<Button>(Tr(L"Open folder"), ButtonKind::Secondary, "folder");
        open->id = "open-shaders-folder";
        open->onClick = [] { RunSettingsAction("open-shaders-folder"); };
        bar->Add<Spacer>();
        auto* rescan = bar->Add<IconButton>("refresh", Tr(L"Look for new shaders"), 32, 20);
        rescan->onClick = [this] { Fill(); };
        Fill();
    }

private:
    void Fill() {
        list_->ClearChildren();
        const std::vector<std::wstring> files = ListShaders();
        if (files.empty()) {
            auto* none = list_->Add<Label>(Tr(L"No shaders"), TextStyle::Body, Tone::Tertiary);
            none->width = Length::Grow();
            none->height = Length::Px(36);
        }
        for (const std::wstring& f : files) {
            auto* line = list_->Add<Widget>();
            line->dir = Dir::Row;
            line->align = Align::Center;
            line->width = Length::Grow();
            line->height = Length::Px(36);
            auto* name = line->Add<Label>(f, TextStyle::Body, Tone::Secondary);
            name->width = Length::Grow();
            name->tooltip = f;
            line->Add<ShaderToggle>(item_, json::ToUtf8(f));
        }
        InvalidateLayout();
        if (page_.relayout) page_.relayout();
    }

    const Item& item_;
    PageHost& page_;
    Widget* list_ = nullptr;
};

}  // namespace

std::unique_ptr<Widget> MakeShaderList(const Item& item, PageHost& page) {
    return std::make_unique<ShaderList>(item, page);
}

}  // namespace app::sui
