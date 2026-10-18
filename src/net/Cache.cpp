#include "Cache.h"  // relative: the unit tests build it without /I src

#include "../core/Json.h"
#include "../core/Paths.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace net {

int64_t UnixNow() {
    return static_cast<int64_t>(std::time(nullptr));
}

uint64_t Fnv1a64(std::string_view s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::wstring OnlineDir() {
    const std::wstring dir = DataDir();
    return dir.empty() ? std::wstring() : dir + L"\\online";
}

// --- disk ---------------------------------------------------------------------------------

std::wstring DiskCache::FileFor(const std::string& key) const {
    wchar_t name[32];
    swprintf_s(name, L"%016llx.bin", static_cast<unsigned long long>(Fnv1a64(key)));
    return dir_ + L"\\" + name;
}

namespace {

// Splits the header lines off the entry file; false when it is not one of ours.
bool ParseEntry(const std::string& data, const std::string& key, CacheEntry* out) {
    size_t pos = 0;
    auto line = [&](std::string* s) {
        const size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) return false;
        *s = data.substr(pos, nl - pos);
        pos = nl + 1;
        return true;
    };
    std::string magic, expires, status, keyLen;
    if (!line(&magic) || magic != "LUMENCACHE1" || !line(&expires) || !line(&status) || !line(&keyLen)) return false;
    const size_t n = static_cast<size_t>(std::strtoull(keyLen.c_str(), nullptr, 10));
    if (pos + n + 1 > data.size() || data.compare(pos, n, key) != 0 || data[pos + n] != '\n') return false;
    out->expires = std::strtoll(expires.c_str(), nullptr, 10);
    out->status = std::atoi(status.c_str());
    out->body = data.substr(pos + n + 1);
    return true;
}

}  // namespace

bool DiskCache::Get(const std::string& key, CacheEntry* out, int64_t now) {
    if (dir_.empty()) return false;
    std::lock_guard lock(mutex_);
    const std::wstring file = FileFor(key);
    std::string data;
    if (!json::ReadFileUtf8(file, &data)) return false;
    CacheEntry e;
    if (!ParseEntry(data, key, &e)) return false;
    if (e.expires <= now) {
        DeleteFileW(file.c_str());
        return false;
    }
    *out = std::move(e);
    return true;
}

bool DiskCache::Put(const std::string& key, int status, std::string_view body, int64_t ttlSeconds, int64_t now) {
    if (dir_.empty() || ttlSeconds <= 0) return false;
    std::string data = "LUMENCACHE1\n" + std::to_string(now + ttlSeconds) + "\n" + std::to_string(status) + "\n" +
                       std::to_string(key.size()) + "\n" + key + "\n";
    data.append(body);
    std::lock_guard lock(mutex_);
    return json::WriteFileAtomic(FileFor(key), data);
}

void DiskCache::Remove(const std::string& key) {
    if (dir_.empty()) return;
    std::lock_guard lock(mutex_);
    DeleteFileW(FileFor(key).c_str());
}

void DiskCache::Clear() {
    if (dir_.empty()) return;
    std::lock_guard lock(mutex_);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir_ + L"\\*.bin").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir_ + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

size_t DiskCache::Prune(int64_t now) {
    if (dir_.empty()) return 0;
    std::lock_guard lock(mutex_);
    size_t removed = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir_ + L"\\*.bin").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring file = dir_ + L"\\" + fd.cFileName;
        std::string data;
        if (!json::ReadFileUtf8(file, &data)) continue;
        // The expiry is the second line; the key does not matter here.
        const size_t a = data.find('\n'), b = a == std::string::npos ? a : data.find('\n', a + 1);
        if (data.rfind("LUMENCACHE1\n", 0) != 0 || b == std::string::npos ||
            std::strtoll(data.substr(a + 1, b - a - 1).c_str(), nullptr, 10) <= now) {
            if (DeleteFileW(file.c_str())) ++removed;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return removed;
}

// --- memory -------------------------------------------------------------------------------

bool MemoryCache::Get(const std::string& key, CacheEntry* out, int64_t now) {
    std::lock_guard lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    if (it->second.entry.expires <= now) {
        bytes_ -= it->second.entry.body.size() + key.size();
        lru_.erase(it->second.lru);
        map_.erase(it);
        return false;
    }
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    *out = it->second.entry;
    return true;
}

void MemoryCache::Put(const std::string& key, CacheEntry entry) {
    std::lock_guard lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        bytes_ -= it->second.entry.body.size() + key.size();
        lru_.erase(it->second.lru);
        map_.erase(it);
    }
    // One answer larger than the whole budget is not worth keeping in memory.
    if (entry.body.size() + key.size() > budget_) return;
    lru_.push_front(key);
    bytes_ += entry.body.size() + key.size();
    map_.emplace(key, Slot{std::move(entry), lru_.begin()});
    EvictLocked();
}

void MemoryCache::EvictLocked() {
    while (bytes_ > budget_ && !lru_.empty()) {
        auto it = map_.find(lru_.back());
        if (it != map_.end()) {
            bytes_ -= it->second.entry.body.size() + it->first.size();
            map_.erase(it);
        }
        lru_.pop_back();
    }
}

void MemoryCache::Remove(const std::string& key) {
    std::lock_guard lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) return;
    bytes_ -= it->second.entry.body.size() + key.size();
    lru_.erase(it->second.lru);
    map_.erase(it);
}

void MemoryCache::Clear() {
    std::lock_guard lock(mutex_);
    map_.clear();
    lru_.clear();
    bytes_ = 0;
}

size_t MemoryCache::Bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_;
}

size_t MemoryCache::Count() const {
    std::lock_guard lock(mutex_);
    return map_.size();
}

// The shared instances are never destroyed: detached net workers may still use them while
// the process exits. The disk cache is first used on a net worker (every feature, not only
// TMDB): expired answers are deleted then, once per session.
DiskCache& SharedDisk() {
    static DiskCache* disk = [] {
        auto* d = new DiskCache(OnlineDir().empty() ? std::wstring() : OnlineDir() + L"\\cache");
        d->Prune();
        return d;
    }();
    return *disk;
}

MemoryCache& SharedMemory() {
    static MemoryCache* memory = new MemoryCache(2u << 20);
    return *memory;
}

}  // namespace net
