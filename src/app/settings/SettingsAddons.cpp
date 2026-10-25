// Settings > Extensions: list of mpv scripts in <data dir>\scripts with search, add and remove
#include "ui/Host.h"
// (design/ref/Settings-extensions_6575-17137.png).
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "gfx/Gpu.h"

#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <future>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

struct Addon {
    std::wstring name;
    std::wstring path;
    std::wstring kind;  // translated "Lua script" ...
    bool folder = false;
};

std::wstring ScriptsDir() {
    return DataDir() + L"\\scripts";
}

const wchar_t* KindOf(const std::wstring& name, bool folder) {
    if (folder) return L"Script folder";
    const size_t dot = name.find_last_of(L'.');
    const std::wstring ext = dot == std::wstring::npos ? L"" : name.substr(dot);
    if (_wcsicmp(ext.c_str(), L".lua") == 0) return L"Lua script";
    if (_wcsicmp(ext.c_str(), L".js") == 0) return L"JavaScript";
    return nullptr;
}

// Runs on a worker thread; returns English kind names (translated on the UI thread).
std::vector<Addon> Scan(std::wstring dir) {
    std::vector<Addon> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const wchar_t* kind = KindOf(name, folder);
        if (!kind) continue;
        out.push_back({name, dir + L"\\" + name, kind, folder});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const Addon& a, const Addon& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}

class AddonsList;
AddonsList* g_list = nullptr;  // the one in the open settings window
Widget* g_remove = nullptr;    // its "Remove" button: enabled while a row is selected

void UpdateRemove();

class AddonsList : public Widget {
public:
    AddonsList() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(194);
        g_list = this;
    }
    ~AddonsList() override {
        if (g_list == this) g_list = nullptr;
        if (poll_)
            if (Host* h = GetHost()) h->CancelTimer(poll_);
    }

    void Rescan() {
        Host* host = GetHost();
        if (!host) {
            needScan_ = true;
            return;
        }
        if (poll_) return;  // a scan is running
        scanning_ = true;
        future_ = std::async(std::launch::async, Scan, ScriptsDir());
        poll_ = host->StartTimer(30, [this] {
            if (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            if (Host* h = GetHost()) h->CancelTimer(poll_);
            poll_ = 0;
            all_ = future_.get();
            scanning_ = false;
            Filter();
        }, true);
    }
    void SetQuery(std::wstring q) {
        query_ = std::move(q);
        Filter();
    }
    const Addon* Selected() const {
        return selected_ >= 0 && selected_ < static_cast<int>(shown_.size()) ? &shown_[static_cast<size_t>(selected_)] : nullptr;
    }

    void Paint(Painter& p) override {
        if (needScan_) {
            needScan_ = false;
            Rescan();
        }
        const Palette& c = P();
        p.Fill(rect, c.bgDeep, 6);
        p.Stroke(rect, c.bgPanel, 6, 2);
        PaintFocusRing(p, rect, 6);
        p.PushClip(rect.Inset(2));
        if (shown_.empty()) {
            const wchar_t* msg = scanning_ ? L"" : (all_.empty() ? Tr(L"No add-ons installed. Add .lua or .js mpv scripts.")
                                                                  : Tr(L"No add-ons match the search."));
            p.Text(msg, rect, TextStyle::Body, c.textTertiary, HAlign::Center);
        }
        for (size_t i = 0; i < shown_.size(); ++i) {
            const Rect r = RowRect(static_cast<int>(i));
            if (r.y > rect.Bottom()) break;
            if (static_cast<int>(i) == selected_)
                p.Fill(r, c.bgSelected, 4);
            else if (static_cast<int>(i) == hover_ && IsHot())
                p.Fill(r, c.hover, 4);
            const Addon& a = shown_[i];
            p.Icon(a.folder ? "folder" : "file", {r.x + 10, r.Cy() - 10, 20, 20}, c.textTertiary);
            p.Text(a.name, {r.x + 40, r.y, r.w - 200, r.h}, TextStyle::Body,
                   static_cast<int>(i) == selected_ ? c.textPrimary : c.textSecondary);
            p.Text(Tr(a.kind.c_str()), {r.Right() - 160, r.y, 148, r.h}, TextStyle::Small, c.textTertiary, HAlign::Trailing);
        }
        p.PopClip();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        selected_ = IndexAt(e.y);
        UpdateRemove();
        Invalidate();
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const int h = IndexAt(e.y);
        if (h != hover_) {
            hover_ = h;
            Invalidate();
        }
        return false;
    }
    bool OnWheel(const WheelEvent& e) override {
        const float maxScroll = std::max(0.0f, shown_.size() * kRowH + 8 - rect.h);
        if (maxScroll <= 0) return false;
        scroll_ = std::clamp(scroll_ - e.delta * kRowH * 2, 0.0f, maxScroll);
        Invalidate();
        return true;
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (shown_.empty()) return false;
        if (e.key == VK_UP) selected_ = std::max(0, selected_ - 1);
        else if (e.key == VK_DOWN) selected_ = std::min(static_cast<int>(shown_.size()) - 1, selected_ + 1);
        else return false;
        UpdateRemove();
        Invalidate();
        return true;
    }

private:
    static constexpr float kRowH = 32;
    Rect RowRect(int i) const { return {rect.x + 4, rect.y + 4 + i * kRowH - scroll_, rect.w - 8, kRowH}; }
    int IndexAt(float y) const {
        const int i = static_cast<int>((y - rect.y - 4 + scroll_) / kRowH);
        return i >= 0 && i < static_cast<int>(shown_.size()) ? i : -1;
    }
    void Filter() {
        std::wstring keep = Selected() ? Selected()->path : L"";
        shown_.clear();
        std::wstring q = query_;
        CharLowerBuffW(q.data(), static_cast<DWORD>(q.size()));
        for (const Addon& a : all_) {
            std::wstring n = a.name;
            CharLowerBuffW(n.data(), static_cast<DWORD>(n.size()));
            if (q.empty() || n.find(q) != std::wstring::npos) shown_.push_back(a);
        }
        selected_ = -1;
        for (size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i].path == keep) selected_ = static_cast<int>(i);
        scroll_ = 0;
        UpdateRemove();
        Invalidate();
    }

    std::vector<Addon> all_, shown_;
    std::wstring query_;
    int selected_ = -1, hover_ = -1;
    float scroll_ = 0;
    bool scanning_ = false, needScan_ = true;
    std::future<std::vector<Addon>> future_;
    UINT_PTR poll_ = 0;
};

void UpdateRemove() {
    if (!g_remove) return;
    const bool on = g_list && g_list->Selected();
    if (g_remove->enabled != on) {
        g_remove->enabled = on;
        g_remove->Invalidate();
    }
}

class RemoveButton : public Button {
public:
    RemoveButton(std::wstring text) : Button(std::move(text), ButtonKind::Secondary) {
        g_remove = this;
        UpdateRemove();
    }
    ~RemoveButton() override {
        if (g_remove == this) g_remove = nullptr;
    }
};

// The search field and the buttons on one line; below ~620 DIP the buttons move to a second
// line (right-aligned) instead of running off the window.
class AddonsToolbar : public Widget {
public:
    static constexpr float kSearchMin = 160, kSearchMax = 210;
    Size MeasureContent(float availW, float) override {
        const float inner = availW - pad.H();
        const bool wrap = ButtonsWidth() + gap + kSearchMin > inner;
        return {availW, pad.V() + (wrap ? 36 + 10 + 32 : 36.0f)};
    }
    void OnLayout() override {
        const Rect in = rect.Inset(pad);
        const float buttons = ButtonsWidth();
        const bool wrap = buttons + gap + kSearchMin > in.w;
        const float searchW = wrap ? in.w : std::min(kSearchMax, in.w - buttons - gap);
        Children().front()->Arrange({in.x, in.y, searchW, 36});
        float x = in.Right() - buttons;
        const float y = wrap ? in.y + 46 : in.y + 2;
        for (size_t i = 1; i < Children().size(); ++i) {
            Widget* b = Children()[i].get();
            const float w = b->Measure(kInf, 32).w;
            b->Arrange({x, y, w, 32});
            x += w + gap;
        }
    }

private:
    float ButtonsWidth() {
        float w = 0;
        for (size_t i = 1; i < Children().size(); ++i) w += Children()[i]->Measure(kInf, 32).w + (i > 1 ? gap : 0);
        return w;
    }
};

class AddonSearch : public TextBox {
public:
    AddonSearch() : TextBox(Tr(L"Search")) {
        width = Length::Grow();
        height = Length::Px(36);
        onChange = [](const std::wstring& t) {
            if (g_list) g_list->SetQuery(t);
        };
    }
    void Paint(Painter& p) override {
        background = P().bgDeep;
        TextBox::Paint(p);
    }
};

void AddScripts(Host* host) {
    gfx::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    const std::wstring filterName = Tr(L"mpv scripts");
    const COMDLG_FILTERSPEC spec[] = {{filterName.c_str(), L"*.lua;*.js"}};
    dlg->SetFileTypes(1, spec);
    if (FAILED(dlg->Show(host ? host->Hwnd() : nullptr))) return;
    gfx::ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return;
    const std::wstring dir = ScriptsDir();
    CreateDirectoryW(DataDir().c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    DWORD n = 0, copied = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        gfx::ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (FAILED(items->GetItemAt(i, &item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) continue;
        std::wstring src = path;
        CoTaskMemFree(path);
        const size_t slash = src.find_last_of(L"\\/");
        const std::wstring dst = dir + L"\\" + (slash == std::wstring::npos ? src : src.substr(slash + 1));
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) ++copied;
    }
    if (copied && host) ShowToast(host, Tr(L"Add-ons load at the next start of Moonbow"), "plugins");
    if (g_list) g_list->Rescan();
}

void RemoveSelected(Host* host) {
    if (!g_list || !g_list->Selected()) {
        if (host) ShowToast(host, Tr(L"Select an add-on first"), "info");
        return;
    }
    const Addon a = *g_list->Selected();
    Confirm(host, Tr(L"Remove add-on?"), Fmt(Tr(L"“{0}” will be moved to the Recycle Bin."), {a.name}), Tr(L"Delete"), [a] {
        // Recycle Bin, so a removal can be undone from Explorer.
        std::wstring from = a.path;
        from.push_back(L'\0');
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        SHFileOperationW(&op);
        if (g_list) g_list->Rescan();
    });
}

}  // namespace

void RescanAddons() {
    if (g_list) g_list->Rescan();
}

std::unique_ptr<Widget> MakeAddonsList(const Item& item, PageHost&) {
    auto wrap = std::make_unique<Widget>();
    wrap->dir = Dir::Column;
    wrap->width = Length::Grow();
    wrap->id = item.key;
    wrap->Add<AddonsList>();
    return wrap;
}

std::unique_ptr<Widget> MakeAddonsToolbar(const std::vector<const Item*>& items, PageHost& page) {
    auto row = std::make_unique<AddonsToolbar>();
    row->width = Length::Grow();
    row->gap = 10;
    row->pad = {0, kRowPad, 0, 10};
    row->Add<AddonSearch>();
    Host* host = page.host;
    for (const Item* it : items) {
        Button* b = it->action == "remove-extension" ? row->Add<RemoveButton>(it->ActionLabel(Lang()))
                                                     : row->Add<Button>(it->ActionLabel(Lang()), ButtonKind::Secondary);
        b->id = it->action;
        if (it->action == "find-extensions") {
            b->minW = 185;
            b->onClick = [] {
                ui::Host::ShellOpen(nullptr, L"open", L"https://github.com/mpv-player/mpv/wiki/User-Scripts", nullptr, nullptr,
                              SW_SHOWNORMAL);
            };
        } else if (it->action == "add-extension") {
            b->width = Length::Px(kSmallButtonW);
            b->onClick = [host] { AddScripts(host); };
        } else if (it->action == "remove-extension") {
            b->width = Length::Px(kSmallButtonW);
            b->onClick = [host] { RemoveSelected(host); };
        } else {
            const Item* item = it;
            PageHost* ph = &page;
            b->onClick = [ph, item] {
                if (ph->runAction) ph->runAction(item->action, *item);
            };
        }
    }
    return row;
}

}  // namespace app::sui
