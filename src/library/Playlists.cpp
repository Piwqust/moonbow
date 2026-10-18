#include "Playlists.h"

#include "MediaTypes.h"

#include "../core/Json.h"
#include "../core/Paths.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace library {

namespace {

bool IsUrl(std::wstring_view s) {
    size_t colon = s.find(L"://");
    return colon != std::wstring_view::npos && colon > 1;
}

bool IsAbsolute(std::wstring_view s) {
    return (s.size() >= 2 && s[1] == L':') || (s.size() >= 2 && s[0] == L'\\' && s[1] == L'\\');
}

std::wstring Resolve(const std::wstring& base, std::wstring entry) {
    if (entry.compare(0, 8, L"file:///") == 0) {
        // file:///C:/Movies/a%20b.mkv
        wchar_t buf[2048];
        DWORD n = static_cast<DWORD>(std::size(buf));
        if (SUCCEEDED(PathCreateFromUrlW(entry.c_str(), buf, &n, 0))) return buf;
        return entry;
    }
    if (IsUrl(entry)) return entry;
    entry = NormalizePath(entry);
    if (IsAbsolute(entry)) return entry;
    if (!entry.empty() && entry[0] == L'\\') return base.substr(0, 2) + entry;  // "\x\y" on the same drive
    wchar_t buf[MAX_PATH * 4];
    std::wstring joined = base + L"\\" + entry;
    if (PathCanonicalizeW(buf, joined.c_str())) return buf;  // collapses ".\" and "..\"
    return joined;
}

bool ValidUtf8(std::string_view s) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0 ||
           s.empty();
}

std::wstring AnsiToWide(std::string_view s, unsigned codePage) {
    if (s.empty()) return {};
    if (!codePage) codePage = json::LegacyCodePage();
    int n = MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(n, 0)), L'\0');
    MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

void Trim(std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n' || s[b] == 0xFEFF)) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    s = s.substr(b, e - b);
}

}  // namespace

// --- M3U ----------------------------------------------------------------------------------------

bool ReadM3u(const std::wstring& path, std::vector<PlaylistItem>* items, unsigned codePage) {
    if (!items) return false;
    items->clear();
    std::string raw;
    if (!json::ReadFileUtf8(path, &raw)) return false;  // strips a UTF-8 BOM
    std::wstring text = ValidUtf8(raw) ? json::ToWide(raw) : AnsiToWide(raw, codePage);
    std::wstring base = ParentFolder(path);

    double pendingDuration = 0;
    std::wstring pendingTitle;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = nl == std::wstring::npos ? text.size() + 1 : nl + 1;
        Trim(line);
        if (line.empty()) continue;
        if (line[0] == L'#') {
            if (line.compare(0, 8, L"#EXTINF:") == 0) {
                // #EXTINF:123,Artist - Title   (attributes like tvg-id="" may precede the comma)
                size_t comma = line.find(L',', 8);
                pendingDuration = std::wcstod(line.c_str() + 8, nullptr);
                if (!std::isfinite(pendingDuration) || pendingDuration < 0) pendingDuration = 0;
                pendingTitle = comma == std::wstring::npos ? std::wstring() : line.substr(comma + 1);
                Trim(pendingTitle);
            }
            continue;
        }
        PlaylistItem item;
        item.path = Resolve(base, line);
        item.title = std::move(pendingTitle);
        item.duration = pendingDuration;
        items->push_back(std::move(item));
        pendingTitle.clear();
        pendingDuration = 0;
    }
    return true;
}

std::string FormatM3u(const std::vector<PlaylistItem>& items) {
    std::string out = "#EXTM3U\r\n";
    for (const PlaylistItem& it : items) {
        long long seconds = it.duration > 0 ? std::llround(it.duration) : -1;
        std::wstring title = it.title.empty() ? StemOf(it.path) : it.title;
        for (wchar_t& c : title)
            if (c == L'\r' || c == L'\n') c = L' ';
        out += "#EXTINF:" + std::to_string(seconds) + "," + json::ToUtf8(title) + "\r\n";
        out += json::ToUtf8(it.path) + "\r\n";
    }
    return out;
}

// --- Playlists ----------------------------------------------------------------------------------

Playlists::~Playlists() {
    if (loaded_) Flush();
}

bool Playlists::Load() {
    loaded_ = true;
    lists_.clear();
    dirty_ = false;
    readOnly_ = false;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"playlists.json"), &text)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        readOnly_ = true;  // locked by a backup or sync tool: saving now would wipe it
        return false;
    }
    std::optional<json::Value> doc = json::Parse(text);
    // Valid JSON of the wrong shape ([], "playlists": {}) is as unreadable as broken JSON.
    const json::Value* lists = doc && doc->IsObject() ? doc->Find("playlists") : nullptr;
    if (!doc || !doc->IsObject() || (lists && !lists->IsArray())) {
        BackupCorruptFile(DataFilePath(L"playlists.json"));  // the next save would overwrite it
        return false;
    }
    if (!lists) return true;
    for (const json::Value& v : lists->AsArray()) {
        Playlist p;
        p.id = v.GetString("id");
        if (p.id.empty() || Find(p.id)) continue;
        p.name = v.GetWString("name");
        p.created = v.GetInt("created");
        p.modified = v.GetInt("modified");
        if (const json::Value* items = v.Find("items")) {
            for (const json::Value& iv : items->AsArray()) {
                PlaylistItem item;
                item.path = iv.GetWString("path");
                if (item.path.empty()) continue;
                item.title = iv.GetWString("title");
                item.duration = std::max(0.0, iv.GetNumber("duration"));
                p.items.push_back(std::move(item));
            }
        }
        lists_.push_back(std::move(p));
    }
    return true;
}

bool Playlists::Save() {
    if (readOnly_) return false;
    json::Value lists = json::Value::MakeArray();
    for (const Playlist& p : lists_) {
        json::Value v = json::Value::MakeObject();
        v.Set("id", p.id);
        v.Set("name", json::ToUtf8(p.name));
        v.Set("created", p.created);
        v.Set("modified", p.modified);
        json::Value items = json::Value::MakeArray();
        for (const PlaylistItem& it : p.items) {
            json::Value iv = json::Value::MakeObject();
            iv.Set("path", json::ToUtf8(it.path));
            if (!it.title.empty()) iv.Set("title", json::ToUtf8(it.title));
            if (it.duration > 0) iv.Set("duration", it.duration);
            items.Push(std::move(iv));
        }
        v.Set("items", std::move(items));
        lists.Push(std::move(v));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("playlists", std::move(lists));
    if (!json::WriteFileAtomic(DataFilePath(L"playlists.json"), json::Serialize(doc, true))) return false;
    dirty_ = false;
    return true;
}

const Playlist* Playlists::Find(std::string_view id) const {
    for (const Playlist& p : lists_)
        if (p.id == id) return &p;
    return nullptr;
}

Playlist* Playlists::Mutable(std::string_view id) {
    for (Playlist& p : lists_)
        if (p.id == id) return &p;
    return nullptr;
}

void Playlists::Touch(Playlist& p) {
    p.modified = UnixMsNow();
    dirty_ = true;
}

std::string Playlists::Create(const std::wstring& name) {
    Playlist p;
    p.created = p.modified = UnixMsNow();
    char buf[48];
    do {
        snprintf(buf, sizeof(buf), "pl-%llx-%x", static_cast<unsigned long long>(p.created), ++counter_);
    } while (Find(buf));
    p.id = buf;
    p.name = name;
    lists_.push_back(std::move(p));
    dirty_ = true;
    return lists_.back().id;
}

bool Playlists::Rename(std::string_view id, const std::wstring& name) {
    Playlist* p = Mutable(id);
    if (!p) return false;
    p->name = name;
    Touch(*p);
    return true;
}

bool Playlists::Delete(std::string_view id) {
    size_t before = lists_.size();
    std::erase_if(lists_, [&](const Playlist& p) { return p.id == id; });
    if (lists_.size() == before) return false;
    dirty_ = true;
    return true;
}

bool Playlists::Add(std::string_view id, const std::vector<PlaylistItem>& items, int insertAt) {
    Playlist* p = Mutable(id);
    if (!p) return false;
    size_t at = insertAt < 0 ? p->items.size() : std::min(static_cast<size_t>(insertAt), p->items.size());
    p->items.insert(p->items.begin() + static_cast<ptrdiff_t>(at), items.begin(), items.end());
    Touch(*p);
    return true;
}

bool Playlists::AddPaths(std::string_view id, const std::vector<std::wstring>& paths, int insertAt) {
    std::vector<PlaylistItem> items;
    items.reserve(paths.size());
    for (const std::wstring& path : paths) items.push_back(PlaylistItem{path, {}, 0});
    return Add(id, items, insertAt);
}

bool Playlists::RemoveAt(std::string_view id, size_t index) {
    Playlist* p = Mutable(id);
    if (!p || index >= p->items.size()) return false;
    p->items.erase(p->items.begin() + static_cast<ptrdiff_t>(index));
    Touch(*p);
    return true;
}

bool Playlists::RemoveIndices(std::string_view id, std::vector<size_t> indices) {
    Playlist* p = Mutable(id);
    if (!p) return false;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removed = false;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it >= p->items.size()) continue;
        p->items.erase(p->items.begin() + static_cast<ptrdiff_t>(*it));
        removed = true;
    }
    if (removed) Touch(*p);
    return removed;
}

bool Playlists::Move(std::string_view id, size_t from, size_t to) {
    Playlist* p = Mutable(id);
    if (!p || from >= p->items.size() || to >= p->items.size()) return false;
    if (from == to) return true;
    auto first = p->items.begin();
    if (from < to)
        std::rotate(first + static_cast<ptrdiff_t>(from), first + static_cast<ptrdiff_t>(from) + 1,
                    first + static_cast<ptrdiff_t>(to) + 1);
    else
        std::rotate(first + static_cast<ptrdiff_t>(to), first + static_cast<ptrdiff_t>(from),
                    first + static_cast<ptrdiff_t>(from) + 1);
    Touch(*p);
    return true;
}

bool Playlists::Clear(std::string_view id) {
    Playlist* p = Mutable(id);
    if (!p) return false;
    p->items.clear();
    Touch(*p);
    return true;
}

std::string Playlists::Import(const std::wstring& m3uPath, const std::wstring& name) {
    std::vector<PlaylistItem> items;
    if (!ReadM3u(m3uPath, &items)) return {};
    std::string id = Create(name.empty() ? StemOf(m3uPath) : name);
    Add(id, items);
    return id;
}

bool Playlists::Export(std::string_view id, const std::wstring& m3uPath) const {
    const Playlist* p = Find(id);
    if (!p) return false;
    return json::WriteFileAtomic(m3uPath, FormatM3u(p->items));
}

}  // namespace library
