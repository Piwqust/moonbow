#pragma once
// Caches for online answers (JSON bodies): a disk cache in the data folder with a time to live
// per entry, and an in-memory LRU in front of it. Both are thread-safe: the disk cache is used by
// the net workers, the memory cache also by the UI thread.
//
// Disk layout: <dir>\<16 hex of FNV-1a(key)>.bin, one file per entry:
//   "LUMENCACHE1\n<expires unix seconds>\n<http status>\n<key length>\n<key>\n<body>"
// The key is stored so a hash collision reads as a miss. Writes go through json::WriteFileAtomic
// (refused while another Lumen owns the data folder).
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace net {

int64_t UnixNow();  // seconds

struct CacheEntry {
    int status = 0;
    int64_t expires = 0;
    std::string body;
};

class DiskCache {
public:
    explicit DiskCache(std::wstring dir) : dir_(std::move(dir)) {}
    // A fresh entry (expires > now); expired files are deleted when found.
    bool Get(const std::string& key, CacheEntry* out, int64_t now = UnixNow());
    bool Put(const std::string& key, int status, std::string_view body, int64_t ttlSeconds, int64_t now = UnixNow());
    void Remove(const std::string& key);
    void Clear();  // every entry file (the folder stays)
    size_t Prune(int64_t now = UnixNow());  // deletes expired entries, returns how many
    const std::wstring& Dir() const { return dir_; }
    std::wstring FileFor(const std::string& key) const;

private:
    std::wstring dir_;
    std::mutex mutex_;
};

class MemoryCache {
public:
    explicit MemoryCache(size_t budgetBytes) : budget_(budgetBytes) {}
    bool Get(const std::string& key, CacheEntry* out, int64_t now = UnixNow());
    void Put(const std::string& key, CacheEntry entry);
    void Remove(const std::string& key);
    void Clear();
    size_t Bytes() const;
    size_t Count() const;

private:
    struct Slot {
        CacheEntry entry;
        std::list<std::string>::iterator lru;
    };
    void EvictLocked();
    size_t budget_;
    size_t bytes_ = 0;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Slot> map_;
    std::list<std::string> lru_;  // front = most recent
};

// The shared caches: <DataDir>\online\cache on disk and 2 MB in memory. Created on first use.
DiskCache& SharedDisk();
MemoryCache& SharedMemory();

// <DataDir>\online (cache, images, the metadata store). Empty when there is no data folder.
std::wstring OnlineDir();

uint64_t Fnv1a64(std::string_view s);

}  // namespace net
