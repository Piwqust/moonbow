#include "History.h"

#include "../core/Json.h"
#include "../core/Paths.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace library {

History::~History() {
    if (loaded_) Flush();
}

bool History::Load() {
    loaded_ = true;
    entries_.clear();
    dirty_ = false;
    readOnly_ = false;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"history.json"), &text)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        readOnly_ = true;  // locked by a backup or sync tool: saving now would wipe it
        return false;
    }
    std::optional<json::Value> doc = json::Parse(text);
    // Valid JSON of the wrong shape ([], "items": {}) is as unreadable as broken JSON.
    const json::Value* items = doc && doc->IsObject() ? doc->Find("items") : nullptr;
    if (!doc || !doc->IsObject() || (items && !items->IsArray())) {
        BackupCorruptFile(DataFilePath(L"history.json"));  // the next save would overwrite it
        return false;
    }
    if (!items) return true;
    for (const json::Value& v : items->AsArray()) {
        HistoryEntry e;
        e.path = v.GetWString("path");
        if (e.path.empty()) continue;
        e.title = v.GetWString("title");
        e.kind = KindFromName(v.GetString("kind"));
        e.lastPlayed = v.GetInt("lastPlayed");
        e.position = std::max(0.0, v.GetNumber("position"));
        e.duration = std::max(0.0, v.GetNumber("duration"));
        e.playCount = static_cast<uint32_t>(std::clamp<int64_t>(v.GetInt("playCount"), 0, UINT32_MAX));
        entries_.push_back(std::move(e));
    }
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const HistoryEntry& a, const HistoryEntry& b) { return a.lastPlayed > b.lastPlayed; });
    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    return true;
}

bool History::Save() {
    if (readOnly_) return false;
    json::Value items = json::Value::MakeArray();
    for (const HistoryEntry& e : entries_) {
        json::Value v = json::Value::MakeObject();
        v.Set("path", json::ToUtf8(e.path));
        if (!e.title.empty()) v.Set("title", json::ToUtf8(e.title));
        v.Set("kind", KindName(e.kind));
        v.Set("lastPlayed", e.lastPlayed);
        v.Set("position", std::round(e.position * 1000) / 1000);
        v.Set("duration", std::round(e.duration * 1000) / 1000);
        v.Set("playCount", e.playCount);
        items.Push(std::move(v));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("items", std::move(items));
    if (!json::WriteFileAtomic(DataFilePath(L"history.json"), json::Serialize(doc, true))) return false;
    dirty_ = false;
    return true;
}

bool History::IsExcluded(const std::wstring& path) const {
    if (exclude_.empty()) return false;
    // PMSF_MULTIPLE treats the spec as a ';'-separated list; S_OK means a match.
    return PathMatchSpecExW(path.c_str(), exclude_.c_str(), PMSF_MULTIPLE) == S_OK;
}

HistoryEntry* History::Touch(const std::wstring& path) {
    std::wstring key = PathKey(path);
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const HistoryEntry& e) { return PathKey(e.path) == key; });
    HistoryEntry entry;
    if (it != entries_.end()) {
        entry = std::move(*it);
        entries_.erase(it);
    } else {
        entry.path = NormalizePath(path);
        entry.kind = KindOf(path);
    }
    entry.lastPlayed = std::max(UnixMsNow(), entries_.empty() ? 0 : entries_.front().lastPlayed);
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > kMaxEntries) entries_.resize(kMaxEntries);
    dirty_ = true;
    return &entries_.front();
}

bool History::RecordPlay(const std::wstring& path, const std::wstring& title, double duration) {
    if (path.empty() || IsExcluded(path)) return false;
    HistoryEntry* e = Touch(path);
    e->title = title.empty() ? (e->title.empty() ? StemOf(path) : e->title) : title;
    if (duration > 0) e->duration = duration;
    ++e->playCount;
    return true;
}

bool History::UpdatePosition(const std::wstring& path, double position, double duration) {
    if (path.empty() || IsExcluded(path)) return false;
    position = std::isfinite(position) ? std::max(0.0, position) : 0;
    // Paused (or saved again within a second): nothing changed, so don't rewrite history.json.
    if (!entries_.empty() && PathKey(entries_.front().path) == PathKey(path)) {
        const HistoryEntry& f = entries_.front();
        const bool sameDuration = !(duration > 0 && std::isfinite(duration)) || std::abs(f.duration - duration) < 1;
        if (std::abs(f.position - position) < 1 && sameDuration && !f.title.empty()) return true;
    }
    HistoryEntry* e = Touch(path);
    if (e->title.empty()) e->title = StemOf(path);
    e->position = position;
    if (duration > 0 && std::isfinite(duration)) e->duration = duration;
    return true;
}

const HistoryEntry* History::Find(const std::wstring& path) const {
    std::wstring key = PathKey(path);
    for (const HistoryEntry& e : entries_)
        if (PathKey(e.path) == key) return &e;
    return nullptr;
}

std::vector<HistoryEntry> History::Recent(size_t max, MediaKind kind) const {
    std::vector<HistoryEntry> out;
    for (const HistoryEntry& e : entries_) {
        if (out.size() >= max) break;
        if (kind == MediaKind::Other || e.kind == kind) out.push_back(e);
    }
    return out;
}

std::vector<HistoryEntry> History::ContinueWatching(size_t max) const {
    std::vector<HistoryEntry> out;
    for (const HistoryEntry& e : entries_) {
        if (out.size() >= max) break;
        double p = e.Progress();
        if (e.kind == MediaKind::Video && p > 0.02 && p < 0.95) out.push_back(e);
    }
    return out;
}

std::vector<HistoryEntry> History::ListenAgain(size_t max) const {
    return Recent(max, MediaKind::Audio);
}

std::vector<FolderStat> History::FrequentFolders(size_t max) const {
    std::unordered_map<std::wstring, FolderStat> byKey;
    for (const HistoryEntry& e : entries_) {
        std::wstring folder = ParentFolder(e.path);
        if (folder.empty()) continue;
        FolderStat& s = byKey[PathKey(folder)];
        if (s.folder.empty()) s.folder = folder;
        s.plays += std::max<uint32_t>(e.playCount, 1);
        s.lastPlayed = std::max(s.lastPlayed, e.lastPlayed);
    }
    std::vector<FolderStat> out;
    out.reserve(byKey.size());
    for (auto& [key, stat] : byKey) out.push_back(std::move(stat));
    std::sort(out.begin(), out.end(), [](const FolderStat& a, const FolderStat& b) {
        if (a.plays != b.plays) return a.plays > b.plays;
        return a.lastPlayed > b.lastPlayed;
    });
    if (out.size() > max) out.resize(max);
    return out;
}

bool History::Remove(const std::wstring& path) {
    std::wstring key = PathKey(path);
    size_t before = entries_.size();
    std::erase_if(entries_, [&](const HistoryEntry& e) { return PathKey(e.path) == key; });
    if (entries_.size() == before) return false;
    dirty_ = true;
    return true;
}

void History::Clear() {
    if (entries_.empty()) return;
    entries_.clear();
    dirty_ = true;
}

}  // namespace library
