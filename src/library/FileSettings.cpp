#include "FileSettings.h"

#include "MediaTypes.h"

#include "../core/Json.h"
#include "../core/Paths.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace library {

namespace {

constexpr const wchar_t* kFile = L"file-settings.json";

void ReadNum(const json::Value& v, const char* key, std::optional<double>* out) {
    const json::Value* n = v.Find(key);
    if (n && n->IsNumber() && std::isfinite(n->AsNumber())) *out = n->AsNumber();
}

void ReadInt(const json::Value& v, const char* key, std::optional<int64_t>* out) {
    const json::Value* n = v.Find(key);
    if (n && n->IsNumber() && n->AsInt() >= 0) *out = n->AsInt();
}

void ReadStr(const json::Value& v, const char* key, std::optional<std::string>* out) {
    const json::Value* s = v.Find(key);
    if (s && s->IsString() && !s->AsString().empty()) *out = s->AsString();
}

void ReadWStr(const json::Value& v, const char* key, std::optional<std::wstring>* out) {
    const json::Value* s = v.Find(key);
    if (s && s->IsString() && !s->AsString().empty()) *out = s->AsWString();
}

void WriteNum(json::Value& v, const char* key, const std::optional<double>& n) {
    if (n) v.Set(key, std::round(*n * 1000) / 1000);
}

FileOptions ReadOptions(const json::Value& v) {
    FileOptions o;
    ReadInt(v, "aid", &o.aid);
    ReadInt(v, "sid", &o.sid);
    ReadInt(v, "secondarySid", &o.secondarySid);
    ReadWStr(v, "audioFile", &o.audioFile);
    ReadWStr(v, "subFile", &o.subFile);
    ReadNum(v, "audioDelay", &o.audioDelay);
    ReadNum(v, "subDelay", &o.subDelay);
    ReadNum(v, "speed", &o.speed);
    ReadStr(v, "aspect", &o.aspect);
    ReadStr(v, "zoomMode", &o.zoomMode);
    ReadNum(v, "zoom", &o.zoom);
    ReadNum(v, "panX", &o.panX);
    ReadNum(v, "panY", &o.panY);
    ReadNum(v, "rotate", &o.rotate);
    if (const json::Value* f = v.Find("flip"); f && f->IsNumber() && (f->AsInt() == 1 || f->AsInt() == 2))
        o.flip = static_cast<int>(f->AsInt());
    ReadStr(v, "deinterlace", &o.deinterlace);
    ReadNum(v, "abA", &o.abA);
    ReadNum(v, "abB", &o.abB);
    ReadNum(v, "subPos", &o.subPos);
    ReadNum(v, "subScale", &o.subScale);
    // The strings go into mpv's loadfile options ("a=1,b=2"): only the values Lumen writes.
    if (o.aspect && o.aspect->find_first_not_of("0123456789.:") != std::string::npos) o.aspect.reset();
    if (o.zoomMode && *o.zoomMode != "quarter" && *o.zoomMode != "half" && *o.zoomMode != "original" &&
        *o.zoomMode != "double")
        o.zoomMode.reset();
    if (o.deinterlace && *o.deinterlace != "yes" && *o.deinterlace != "no" && *o.deinterlace != "auto")
        o.deinterlace.reset();
    // A speed of 0 would freeze playback and mpv rejects it anyway.
    if (o.speed && (*o.speed < 0.01 || *o.speed > 100)) o.speed.reset();
    if (o.subScale && *o.subScale <= 0) o.subScale.reset();
    return o;
}

void WriteOptions(json::Value& v, const FileOptions& o) {
    if (o.aid) v.Set("aid", *o.aid);
    if (o.sid) v.Set("sid", *o.sid);
    if (o.secondarySid) v.Set("secondarySid", *o.secondarySid);
    if (o.audioFile) v.Set("audioFile", json::ToUtf8(*o.audioFile));
    if (o.subFile) v.Set("subFile", json::ToUtf8(*o.subFile));
    WriteNum(v, "audioDelay", o.audioDelay);
    WriteNum(v, "subDelay", o.subDelay);
    WriteNum(v, "speed", o.speed);
    if (o.aspect) v.Set("aspect", *o.aspect);
    if (o.zoomMode) v.Set("zoomMode", *o.zoomMode);
    WriteNum(v, "zoom", o.zoom);
    WriteNum(v, "panX", o.panX);
    WriteNum(v, "panY", o.panY);
    WriteNum(v, "rotate", o.rotate);
    if (o.flip) v.Set("flip", *o.flip);
    if (o.deinterlace) v.Set("deinterlace", *o.deinterlace);
    WriteNum(v, "abA", o.abA);
    WriteNum(v, "abB", o.abB);
    WriteNum(v, "subPos", o.subPos);
    WriteNum(v, "subScale", o.subScale);
}

}  // namespace

FileSettings::~FileSettings() {
    if (loaded_) Flush();
}

bool FileSettings::Load() {
    loaded_ = true;
    entries_.clear();
    dirty_ = false;
    readOnly_ = false;
    lastStamp_ = 0;
    const std::wstring path = DataFilePath(kFile);
    std::string text;
    if (!json::ReadFileUtf8(path, &text)) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        readOnly_ = true;  // locked by a backup or sync tool: saving now would wipe it
        return false;
    }
    std::optional<json::Value> doc = json::Parse(text);
    const json::Value* files = doc && doc->IsObject() ? doc->Find("files") : nullptr;
    if (!doc || !doc->IsObject() || (files && !files->IsArray())) {
        BackupCorruptFile(path);  // the next save would overwrite it
        return false;
    }
    if (!files) return true;
    entries_.reserve(std::min(files->size(), kMaxEntries + 1));
    for (const json::Value& v : files->AsArray()) {
        Entry e;
        e.path = NormalizePath(v.GetWString("path"));
        if (e.path.empty()) continue;
        e.used = std::max<int64_t>(0, v.GetInt("used"));
        e.options = ReadOptions(v);
        if (e.options.Empty()) continue;
        lastStamp_ = std::max(lastStamp_, e.used);
        std::wstring key = PathKey(e.path);
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.used < e.used) entries_[std::move(key)] = std::move(e);
    }
    Evict();
    dirty_ = false;
    return true;
}

bool FileSettings::Save() {
    if (readOnly_) return false;
    std::vector<const Entry*> order;
    order.reserve(entries_.size());
    for (const auto& [key, e] : entries_) order.push_back(&e);
    std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) { return a->used > b->used; });
    json::Value files = json::Value::MakeArray();
    files.MutArray().reserve(order.size());
    for (const Entry* e : order) {
        json::Value v = json::Value::MakeObject();
        v.Set("path", json::ToUtf8(e->path));
        v.Set("used", e->used);
        WriteOptions(v, e->options);
        files.Push(std::move(v));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("files", std::move(files));
    // Compact: up to 5000 files, and no one edits this by hand.
    if (!json::WriteFileAtomic(DataFilePath(kFile), json::Serialize(doc))) return false;
    dirty_ = false;
    return true;
}

bool FileSettings::Flush() {
    return !dirty_ || Save();
}

int64_t FileSettings::NextStamp() {
    lastStamp_ = std::max(UnixMsNow(), lastStamp_ + 1);
    return lastStamp_;
}

void FileSettings::Evict() {
    // Over the cap only by one at a time in use (Put), by any amount after a hand-edited file:
    // one partial sort, not a scan per entry.
    if (entries_.size() <= kMaxEntries) return;
    std::vector<decltype(entries_)::iterator> all;
    all.reserve(entries_.size());
    for (auto it = entries_.begin(); it != entries_.end(); ++it) all.push_back(it);
    const size_t excess = entries_.size() - kMaxEntries;
    std::nth_element(all.begin(), all.begin() + static_cast<ptrdiff_t>(excess - 1), all.end(),
                     [](const auto& a, const auto& b) { return a->second.used < b->second.used; });
    for (size_t i = 0; i < excess; ++i) entries_.erase(all[i]);
    dirty_ = true;
}

const FileOptions* FileSettings::Find(const std::wstring& path) const {
    if (path.empty()) return nullptr;
    auto it = entries_.find(PathKey(path));
    return it == entries_.end() ? nullptr : &it->second.options;
}

bool FileSettings::Put(const std::wstring& path, const FileOptions& options) {
    if (path.empty()) return false;
    std::wstring key = PathKey(path);
    auto it = entries_.find(key);
    if (options.Empty()) {
        if (it == entries_.end()) return false;
        entries_.erase(it);
        dirty_ = true;
        return true;
    }
    if (it != entries_.end() && it->second.options == options) return false;
    Entry& e = entries_[std::move(key)];
    e.path = NormalizePath(path);
    e.used = NextStamp();
    e.options = options;
    dirty_ = true;
    Evict();
    return true;
}

void FileSettings::Touch(const std::wstring& path) {
    if (path.empty()) return;
    auto it = entries_.find(PathKey(path));
    if (it == entries_.end()) return;
    it->second.used = NextStamp();
    dirty_ = true;
}

bool FileSettings::Remove(const std::wstring& path) {
    if (path.empty() || !entries_.erase(PathKey(path))) return false;
    dirty_ = true;
    return true;
}

void FileSettings::Clear() {
    if (entries_.empty()) return;
    entries_.clear();
    dirty_ = true;
}

}  // namespace library
