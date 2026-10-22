#include "UserChoice.h"

#include <windows.h>

#include <bcrypt.h>
#include <wincrypt.h>

#include <cstring>
#include <cwctype>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    memcpy(&v, d.data() + at, 4);
    return v;
}

bool Md5(const std::vector<uint8_t>& data, uint8_t out[16]) {
    return BCryptHash(BCRYPT_MD5_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(data.data()),
                      static_cast<ULONG>(data.size()), out, 16) == 0;
}

}  // namespace

std::wstring UserChoiceHash(const std::wstring& ext, const std::wstring& sid, const std::wstring& progId,
                            uint64_t minute) {
    wchar_t time[17];
    swprintf_s(time, L"%016llx", static_cast<unsigned long long>(minute));
    std::wstring text = ext + sid + progId + time +
                        L"User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}";
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));

    std::vector<uint8_t> d((text.size() + 1) * 2);  // UTF-16 with its terminator
    memcpy(d.data(), text.c_str(), d.size());
    uint8_t md5[16];
    if (!Md5(d, md5)) return {};
    const size_t len = d.size();
    const size_t words = ((len & 4) == 0 ? 1 : 0) + (len >> 2) - 1;
    if (words <= 1) return {};
    const size_t rounds = ((words - 2) >> 1) + 1;
    uint32_t m0 = 0, m1 = 0;
    memcpy(&m0, md5, 4);
    memcpy(&m1, md5 + 4, 4);
    m0 |= 1;
    m1 |= 1;

    // Two mixing passes over pairs of 32-bit words; uint32_t wraps like the original.
    uint32_t a = m0 + 0x69FB0000u, b = m1 + 0x13DB0000u, cache = 0, h1 = 0, h2 = 0;
    for (size_t i = 0, p = 0; i < rounds; ++i, p += 8) {
        const uint32_t r0 = U32(d, p) + h1, r1 = U32(d, p + 4);
        const uint32_t r2 = r0 * a - 0x10FA9605u * (r0 >> 16);
        const uint32_t r2b = 0x79F8A395u * r2 + 0x689B6B9Fu * (r2 >> 16);
        const uint32_t r3 = 0xEA970001u * r2b - 0x3C101569u * (r2b >> 16);
        const uint32_t r4 = r3 + r1;
        const uint32_t r5 = cache + r3;
        const uint32_t r6 = r4 * b - 0x3CE8EC25u * (r4 >> 16);
        const uint32_t r6b = 0x59C3AF2Du * r6 - 0x2232E0F1u * (r6 >> 16);
        h1 = 0x1EC90001u * r6b + 0x35BD1EC9u * (r6b >> 16);
        h2 = r5 + h1;
        cache = h2;
    }
    const uint32_t first[2] = {h1, h2};

    cache = h1 = h2 = 0;
    for (size_t i = 0, p = 0; i < rounds; ++i, p += 8) {
        const uint32_t r0 = U32(d, p) + h1;
        const uint32_t r1 = r0 * m0;
        const uint32_t r1b = 0xB1110000u * r1 - 0x30674EEFu * (r1 >> 16);
        const uint32_t r2 = 0x5B9F0000u * r1b - 0x78F7A461u * (r1b >> 16);
        const uint32_t r2b = 0x12CEB96Du * (r2 >> 16) - 0x46930000u * r2;
        const uint32_t r3 = 0x1D830000u * r2b + 0x257E1D83u * (r2b >> 16);
        const uint32_t r4 = m1 * (r3 + U32(d, p + 4));
        const uint32_t r4b = 0x16F50000u * r4 - 0x5D8BE90Bu * (r4 >> 16);
        const uint32_t r5 = 0x96FF0000u * r4b - 0x2C7C6901u * (r4b >> 16);
        const uint32_t r5b = 0x2B890000u * r5 + 0x7C932B89u * (r5 >> 16);
        h1 = 0x9F690000u * r5b - 0x405B6097u * (r5b >> 16);
        h2 = h1 + cache + r3;
        cache = h2;
    }
    const uint32_t out[2] = {first[0] ^ h1, first[1] ^ h2};

    wchar_t b64[16];
    DWORD n = static_cast<DWORD>(std::size(b64));
    if (!CryptBinaryToStringW(reinterpret_cast<const BYTE*>(out), sizeof(out), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              b64, &n))
        return {};
    return std::wstring(b64, n);
}
