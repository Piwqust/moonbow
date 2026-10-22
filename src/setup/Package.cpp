#include "Package.h"  // relative: the unit tests build it without /I src

#include <windows.h>
#include <compressapi.h>

#include <cstring>

#pragma comment(lib, "cabinet.lib")
#pragma comment(lib, "version.lib")

namespace setup {
namespace {

constexpr char kMagic[8] = {'L', 'U', 'M', 'E', 'N', 'P', 'K', '1'};
constexpr size_t kFooterSize = 8 + 4 + 8;

struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    File(const std::wstring& path, bool write) {
        h = write ? CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                  : CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~File() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE; }
    uint64_t Size() const {
        LARGE_INTEGER s{};
        return GetFileSizeEx(h, &s) ? static_cast<uint64_t>(s.QuadPart) : 0;
    }
    bool ReadAt(uint64_t offset, void* data, uint64_t n) const {
        LARGE_INTEGER at{};
        at.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(h, at, nullptr, FILE_BEGIN)) return false;
        auto* p = static_cast<uint8_t*>(data);
        while (n > 0) {
            const DWORD chunk = static_cast<DWORD>(n < (1u << 30) ? n : (1u << 30));
            DWORD got = 0;
            if (!ReadFile(h, p, chunk, &got, nullptr) || got != chunk) return false;
            p += got;
            n -= got;
        }
        return true;
    }
    bool Write(const void* data, uint64_t n) {
        auto* p = static_cast<const uint8_t*>(data);
        while (n > 0) {
            const DWORD chunk = static_cast<DWORD>(n < (1u << 30) ? n : (1u << 30));
            DWORD put = 0;
            if (!WriteFile(h, p, chunk, &put, nullptr) || put != chunk) return false;
            p += put;
            n -= put;
        }
        return true;
    }
};

bool ReadWhole(const std::wstring& path, std::vector<uint8_t>* out) {
    File f(path, false);
    if (!f) return false;
    out->resize(static_cast<size_t>(f.Size()));
    return f.ReadAt(0, out->data(), out->size());
}

std::wstring BaseName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::wstring FileVersion(const std::wstring& exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (size == 0) return {};
    std::vector<uint8_t> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info)
        return {};
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) +
           L"." + std::to_wstring(HIWORD(info->dwFileVersionLS));
}

bool OpenPackage(const std::wstring& exe, Package* out) {
    File f(exe, false);
    if (!f) return false;
    const uint64_t size = f.Size();
    if (size < kFooterSize) return false;
    uint8_t footer[kFooterSize];
    if (!f.ReadAt(size - kFooterSize, footer, kFooterSize) || memcmp(footer + 12, kMagic, 8) != 0) return false;
    uint64_t first = 0;
    uint32_t count = 0;
    memcpy(&first, footer, 8);
    memcpy(&count, footer + 8, 4);
    if (first == 0 || first >= size || count > 1000) return false;

    Package p;
    p.path = exe;
    p.version = FileVersion(exe);
    p.files.push_back({L"Lumen.exe", first, first, 0, true});
    uint64_t at = first;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = 0;
        if (!f.ReadAt(at, &nameLen, 2) || nameLen == 0 || nameLen > 260) return false;
        PackedFile e;
        e.name.resize(nameLen);
        if (!f.ReadAt(at + 2, e.name.data(), nameLen * 2ull) || !f.ReadAt(at + 2 + nameLen * 2ull, &e.size, 8) ||
            !f.ReadAt(at + 10 + nameLen * 2ull, &e.packedSize, 8))
            return false;
        // A name is a plain file name: never a path out of the install folder.
        if (e.name.find_first_of(L"\\/:") != std::wstring::npos || e.name == L"." || e.name == L"..") return false;
        e.offset = at + 18 + nameLen * 2ull;
        if (e.offset + e.packedSize > size - kFooterSize) return false;
        at = e.offset + e.packedSize;
        p.files.push_back(std::move(e));
    }
    *out = std::move(p);
    return true;
}

bool WritePackage(const std::wstring& exe, const std::vector<std::wstring>& files, const std::wstring& out,
                  std::wstring* error) {
    std::vector<uint8_t> image;
    if (!ReadWhole(exe, &image)) return *error = L"cannot read " + exe, false;
    Package existing;
    if (OpenPackage(exe, &existing)) return *error = exe + L" is a setup already", false;
    COMPRESSOR_HANDLE c = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c)) return *error = L"no LZMS compressor", false;
    File f(out, true);
    bool ok = f && f.Write(image.data(), image.size());
    const uint64_t first = image.size();
    for (const std::wstring& path : files) {
        std::vector<uint8_t> data;
        if (!ok || !ReadWhole(path, &data)) {
            *error = L"cannot read " + path;
            ok = false;
            break;
        }
        // Buffer mode can come out a little larger than the input on data that does not compress.
        std::vector<uint8_t> packed(data.size() + data.size() / 8 + 65536);
        SIZE_T packedSize = 0;
        if (!Compress(c, data.data(), data.size(), packed.data(), packed.size(), &packedSize)) {
            *error = L"cannot compress " + path;
            ok = false;
            break;
        }
        const std::wstring name = BaseName(path);
        const uint16_t nameLen = static_cast<uint16_t>(name.size());
        const uint64_t size = data.size(), packedSize64 = packedSize;
        ok = f.Write(&nameLen, 2) && f.Write(name.data(), nameLen * 2ull) && f.Write(&size, 8) &&
             f.Write(&packedSize64, 8) && f.Write(packed.data(), packedSize);
    }
    CloseCompressor(c);
    const uint32_t count = static_cast<uint32_t>(files.size());
    ok = ok && f.Write(&first, 8) && f.Write(&count, 4) && f.Write(kMagic, 8);
    if (!ok && error->empty()) *error = L"cannot write " + out;
    return ok;
}

bool ReadPackedFile(const Package& package, const PackedFile& file, std::vector<uint8_t>* out, std::wstring* error) {
    File f(package.path, false);
    std::vector<uint8_t> packed(static_cast<size_t>(file.packedSize));
    if (!f || !f.ReadAt(file.offset, packed.data(), packed.size())) return *error = L"cannot read the setup file", false;
    if (file.stored) {
        *out = std::move(packed);
        return true;
    }
    DECOMPRESSOR_HANDLE d = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d)) return *error = L"no LZMS decompressor", false;
    out->resize(static_cast<size_t>(file.size));
    SIZE_T got = 0;
    const bool ok = Decompress(d, packed.data(), packed.size(), out->data(), out->size(), &got) && got == file.size;
    CloseDecompressor(d);
    if (!ok) *error = L"the setup file is damaged (" + file.name + L"): download it again";
    return ok;
}

}  // namespace setup
