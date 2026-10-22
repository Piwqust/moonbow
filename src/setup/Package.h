#pragma once
// Setup.exe is Lumen.exe with the other program files appended after its PE image, so the setup
// draws with Lumen's own UI and the Lumen.exe it installs is its own first bytes.
// Layout: <Lumen.exe> <entry>... <footer>
//   entry:  u16 name length, UTF-16 name, u64 size, u64 packed size, u64 FNV-1a of the contents,
//           blocks of up to 8 MB:
//           u32 size, u32 packed size, LZMS data (Windows' Compression API)
//   footer: u64 offset of the first entry, u32 entry count, "LUMENPK2"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace setup {

struct PackedFile {
    std::wstring name;
    uint64_t size = 0;
    uint64_t packedSize = 0;
    uint64_t checksum = 0;
    uint64_t offset = 0;  // of the data in the package file
    bool stored = false;  // Lumen.exe: the plain bytes at the start
};

struct Package {
    std::wstring path;
    std::wstring version;           // of the package's Lumen.exe
    std::vector<PackedFile> files;  // Lumen.exe first
};

// Reads the appended files of `exe`; false for a plain Lumen.exe (no package).
bool OpenPackage(const std::wstring& exe, Package* out);

// Writes `exe` (a plain Lumen.exe) followed by `files` (full paths) to `out`.
bool WritePackage(const std::wstring& exe, const std::vector<std::wstring>& files, const std::wstring& out,
                  std::wstring* error);

// The contents of one file of the package; `progress` gets the bytes unpacked so far.
bool ReadPackedFile(const Package& package, const PackedFile& file, std::vector<uint8_t>* out, std::wstring* error,
                    const std::function<void(uint64_t)>& progress = {});

// "0.1.0" from the version resource of an exe; empty when it has none.
std::wstring FileVersion(const std::wstring& exe);

}  // namespace setup
