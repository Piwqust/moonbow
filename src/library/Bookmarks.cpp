#include "Bookmarks.h"

#include "MediaTypes.h"

#include "../core/Json.h"
#include "../core/Paths.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace library {

namespace {

// Shared start of Load(): false only for a file that exists but cannot be read or parsed.
bool ReadDoc(const wchar_t* name, std::optional<json::Value>* doc) {
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(name), &text)) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    *doc = json::Parse(text);
    if (!doc->has_value()) BackupCorruptFile(DataFilePath(name));  // the next save would overwrite it
    return doc->has_value();
}

}  // namespace

// --- Bookmarks ----------------------------------------------------------------------------------

Bookmarks::~Bookmarks() {
    if (loaded_) Flush();
}

bool Bookmarks::Load() {
    loaded_ = true;
    files_.clear();
    dirty_ = false;
    std::optional<json::Value> doc;
    if (!ReadDoc(L"bookmarks.json", &doc)) return false;
    if (!doc) return true;
    const json::Value* files = doc->Find("files");
    if (!files) return true;
    for (const json::Value& f : files->AsArray()) {
        FileMarks fm;
        fm.path = f.GetWString("path");
        if (fm.path.empty()) continue;
        if (const json::Value* marks = f.Find("bookmarks")) {
            for (const json::Value& m : marks->AsArray()) {
                Bookmark b;
                b.time = std::max(0.0, m.GetNumber("time"));
                b.name = m.GetWString("name");
                b.created = m.GetInt("created");
                fm.marks.push_back(std::move(b));
            }
        }
        if (fm.marks.empty()) continue;
        std::stable_sort(fm.marks.begin(), fm.marks.end(),
                         [](const Bookmark& a, const Bookmark& b) { return a.time < b.time; });
        files_[PathKey(fm.path)] = std::move(fm);
    }
    return true;
}

bool Bookmarks::Save() {
    // Stable output order: by path.
    std::vector<const FileMarks*> sorted;
    for (const auto& kv : files_) sorted.push_back(&kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const FileMarks* a, const FileMarks* b) { return a->path < b->path; });

    json::Value files = json::Value::MakeArray();
    for (const FileMarks* fm : sorted) {
        json::Value f = json::Value::MakeObject();
        f.Set("path", json::ToUtf8(fm->path));
        json::Value marks = json::Value::MakeArray();
        for (const Bookmark& b : fm->marks) {
            json::Value m = json::Value::MakeObject();
            m.Set("time", std::round(b.time * 1000) / 1000);
            m.Set("name", json::ToUtf8(b.name));
            m.Set("created", b.created);
            marks.Push(std::move(m));
        }
        f.Set("bookmarks", std::move(marks));
        files.Push(std::move(f));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("files", std::move(files));
    if (!json::WriteFileAtomic(DataFilePath(L"bookmarks.json"), json::Serialize(doc, true))) return false;
    dirty_ = false;
    return true;
}

std::vector<Bookmark> Bookmarks::For(const std::wstring& path) const {
    auto it = files_.find(PathKey(path));
    return it == files_.end() ? std::vector<Bookmark>() : it->second.marks;
}

size_t Bookmarks::Add(const std::wstring& path, double time, const std::wstring& name) {
    FileMarks& fm = files_[PathKey(path)];
    if (fm.path.empty()) fm.path = NormalizePath(path);
    Bookmark b;
    b.time = std::isfinite(time) ? std::max(0.0, time) : 0;
    b.name = name;
    b.created = UnixMsNow();
    auto pos = std::upper_bound(fm.marks.begin(), fm.marks.end(), b.time,
                                [](double t, const Bookmark& m) { return t < m.time; });
    size_t index = static_cast<size_t>(pos - fm.marks.begin());
    fm.marks.insert(pos, std::move(b));
    dirty_ = true;
    return index;
}

bool Bookmarks::Rename(const std::wstring& path, size_t index, const std::wstring& name) {
    auto it = files_.find(PathKey(path));
    if (it == files_.end() || index >= it->second.marks.size()) return false;
    it->second.marks[index].name = name;
    dirty_ = true;
    return true;
}

bool Bookmarks::Remove(const std::wstring& path, size_t index) {
    auto it = files_.find(PathKey(path));
    if (it == files_.end() || index >= it->second.marks.size()) return false;
    auto& marks = it->second.marks;
    marks.erase(marks.begin() + static_cast<ptrdiff_t>(index));
    if (marks.empty()) files_.erase(it);
    dirty_ = true;
    return true;
}

void Bookmarks::ClearFile(const std::wstring& path) {
    if (files_.erase(PathKey(path))) dirty_ = true;
}

// --- Favorites ----------------------------------------------------------------------------------

Favorites::~Favorites() {
    if (loaded_) Flush();
}

bool Favorites::Load() {
    loaded_ = true;
    paths_.clear();
    dirty_ = false;
    std::optional<json::Value> doc;
    if (!ReadDoc(L"favorites.json", &doc)) return false;
    if (!doc) return true;
    if (const json::Value* list = doc->Find("paths"))
        for (const json::Value& p : list->AsArray())
            if (p.IsString() && !p.AsString().empty() && !Contains(p.AsWString())) paths_.push_back(p.AsWString());
    return true;
}

bool Favorites::Save() {
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    json::Value list = json::Value::MakeArray();
    for (const std::wstring& p : paths_) list.Push(json::ToUtf8(p));
    doc.Set("paths", std::move(list));
    if (!json::WriteFileAtomic(DataFilePath(L"favorites.json"), json::Serialize(doc, true))) return false;
    dirty_ = false;
    return true;
}

bool Favorites::Contains(const std::wstring& path) const {
    std::wstring key = PathKey(path);
    return std::any_of(paths_.begin(), paths_.end(), [&](const std::wstring& p) { return PathKey(p) == key; });
}

bool Favorites::Add(const std::wstring& path) {
    if (path.empty() || Contains(path)) return false;
    paths_.push_back(NormalizePath(path));
    dirty_ = true;
    return true;
}

bool Favorites::Remove(const std::wstring& path) {
    std::wstring key = PathKey(path);
    size_t before = paths_.size();
    std::erase_if(paths_, [&](const std::wstring& p) { return PathKey(p) == key; });
    if (paths_.size() == before) return false;
    dirty_ = true;
    return true;
}

bool Favorites::Toggle(const std::wstring& path) {
    if (Remove(path)) return false;
    Add(path);
    return true;
}

}  // namespace library
