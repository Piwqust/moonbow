#include "UpdateFeed.h"  // relative: the unit tests build it without /I src

#include "../core/Json.h"

#include <windows.h>
#include <bcrypt.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>

#pragma comment(lib, "bcrypt.lib")

namespace setup {
namespace {

// Public keys whose signature Moonbow accepts (tools/release/new-update-key.ps1 prints one).
// To change keys: add the new one here, ship a version signed with the old key, then sign with
// the new one. Removing a key cuts off every Moonbow that knows only that key.
constexpr std::string_view kUpdateKeys[] = {
    "ea382bbbda4ff46a32fc451ebe5e212539aeaf1c1543e7dfa78086829d99f590"   // X
    "f9ac2f318519776e1a2775d9d0fe1d693704bad56e19f0684331728c086d94c0",  // Y
};

constexpr wchar_t kGitHubFeed[] = L"https://github.com/Piwqust/moonbow/releases/latest/download/moonbow-update.json";
constexpr wchar_t kMirrorFeed[] = L"https://moonbow.angleford.co/update/moonbow-update.json";

constexpr uint64_t kMaxSetupSize = 512ull << 20;

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool FromHex(std::string_view hex, std::string* out) {
    if (hex.size() % 2) return false;
    out->clear();
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = HexDigit(hex[i]), lo = HexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out->push_back(static_cast<char>(hi * 16 + lo));
    }
    return true;
}

std::string ToHex(const unsigned char* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (size_t i = 0; i < size; ++i) {
        hex.push_back(digits[data[i] >> 4]);
        hex.push_back(digits[data[i] & 15]);
    }
    return hex;
}

bool FromBase64(std::string_view text, std::string* out) {
    out->clear();
    uint32_t acc = 0;
    int bits = 0;
    size_t pad = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++pad; continue; }
        else return false;
        if (pad) return false;  // data after the padding
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return pad <= 2;
}

struct AlgCloser {
    void operator()(BCRYPT_ALG_HANDLE h) const { BCryptCloseAlgorithmProvider(h, 0); }
};
struct KeyCloser {
    void operator()(BCRYPT_KEY_HANDLE h) const { BCryptDestroyKey(h); }
};
using Alg = std::unique_ptr<void, AlgCloser>;
using Key = std::unique_ptr<void, KeyCloser>;

Alg OpenAlg(const wchar_t* id) {
    BCRYPT_ALG_HANDLE h = nullptr;
    return BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, id, nullptr, 0)) ? Alg(h) : Alg();
}

// SHA-256 of what `feed` hands over in pieces (true when all of it was read).
template <class Feed>
bool Sha256(Feed&& feed, unsigned char digest[32]) {
    Alg alg = OpenAlg(BCRYPT_SHA256_ALGORITHM);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!alg || !BCRYPT_SUCCESS(BCryptCreateHash(alg.get(), &hash, nullptr, 0, nullptr, 0, 0))) return false;
    const bool ok = feed([hash](const void* data, size_t size) {
                        return BCRYPT_SUCCESS(BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)),
                                                             static_cast<ULONG>(size), 0));
                    }) &&
                    BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, 32, 0));
    BCryptDestroyHash(hash);
    return ok;
}

// "0.2.0": one to four numbers.
bool PlainVersion(const std::wstring& v) {
    if (v.empty() || v.size() > 32 || v.front() == L'.' || v.back() == L'.') return false;
    int parts = 1;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == L'.') {
            if (v[i - 1] == L'.') return false;
            ++parts;
        } else if (v[i] < L'0' || v[i] > L'9') {
            return false;
        }
    }
    return parts <= 4;
}

bool HttpsUrl(const std::wstring& url) {
    if (url.size() < 9 || url.size() > 2048 || _wcsnicmp(url.c_str(), L"https://", 8) != 0) return false;
    for (wchar_t c : url)
        if (c <= L' ' || c > L'~' || c == L'"' || c == L'\\') return false;
    const size_t hostEnd = url.find_first_of(L"/?#", 8);
    const std::wstring host = url.substr(8, hostEnd == std::wstring::npos ? std::wstring::npos : hostEnd - 8);
    return !host.empty() && host.find(L'@') == std::wstring::npos;
}

bool Fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

}  // namespace

bool VerifySignature(std::string_view data, std::string_view signature, std::string_view publicKeyHex) {
    std::string xy;
    if (signature.size() != 64 || !FromHex(publicKeyHex, &xy) || xy.size() != 64) return false;
    struct {
        BCRYPT_ECCKEY_BLOB header;
        unsigned char xy[64];
    } blob{{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32}, {}};
    memcpy(blob.xy, xy.data(), 64);
    Alg alg = OpenAlg(BCRYPT_ECDSA_P256_ALGORITHM);
    BCRYPT_KEY_HANDLE raw = nullptr;
    if (!alg || !BCRYPT_SUCCESS(BCryptImportKeyPair(alg.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB, &raw,
                                                    reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0)))
        return false;
    Key key(raw);
    unsigned char digest[32];
    if (!Sha256([&](auto&& add) { return add(data.data(), data.size()); }, digest)) return false;
    std::string sig(signature);
    return BCRYPT_SUCCESS(BCryptVerifySignature(key.get(), nullptr, digest, sizeof(digest),
                                                reinterpret_cast<PUCHAR>(sig.data()), 64, 0));
}

std::string Sha256Hex(std::string_view data) {
    unsigned char digest[32];
    if (!Sha256([&](auto&& add) { return add(data.data(), data.size()); }, digest)) return {};
    return ToHex(digest, sizeof(digest));
}

std::string FileSha256(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    unsigned char digest[32];
    const bool ok = Sha256(
        [f](auto&& add) {
            std::unique_ptr<char[]> buf(new char[1 << 20]);
            for (;;) {
                DWORD got = 0;
                if (!ReadFile(f, buf.get(), 1 << 20, &got, nullptr)) return false;
                if (!got) return true;
                if (!add(buf.get(), got)) return false;
            }
        },
        digest);
    CloseHandle(f);
    return ok ? ToHex(digest, sizeof(digest)) : std::string();
}

bool SetupMatches(const std::wstring& path, const UpdateInfo& info) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
    const uint64_t size = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return size == info.setupSize && !info.setupSha256.empty() && FileSha256(path) == info.setupSha256;
}

bool ParseUpdateFeed(std::string_view feed, const std::vector<std::string_view>& keys, UpdateInfo* out,
                     std::string* error) {
    const std::optional<json::Value> outer = json::Parse(feed);
    if (!outer || !outer->IsObject()) return Fail(error, "not JSON");
    const json::Value* payload = outer->Find("payload");
    const json::Value* signature = outer->Find("signature");
    if (!payload || !payload->IsString() || !signature || !signature->IsString())
        return Fail(error, "no payload or signature");
    std::string sig;
    if (!FromBase64(signature->AsString(), &sig) || sig.size() != 64) return Fail(error, "malformed signature");
    bool trusted = false;
    for (std::string_view key : keys) trusted = trusted || VerifySignature(payload->AsString(), sig, key);
    if (!trusted) return Fail(error, "the signature does not match Moonbow's key");

    const std::optional<json::Value> m = json::Parse(payload->AsString());
    if (!m || !m->IsObject() || m->GetString("product") != "Moonbow") return Fail(error, "not a Moonbow manifest");
    const json::Value* setupValue = m->Find("setup");
    if (!setupValue || !setupValue->IsObject()) return Fail(error, "no setup in the manifest");
    const json::Value& s = *setupValue;
    UpdateInfo info;
    info.version = m->GetWString("version");
    info.date = m->GetWString("date");
    info.page = m->GetWString("page");
    info.setupName = s.GetWString("name");
    const double size = s.GetNumber("size", -1);
    info.setupSha256 = s.GetString("sha256");
    if (!PlainVersion(info.version)) return Fail(error, "bad version");
    if (!(size > 0 && size <= static_cast<double>(kMaxSetupSize))) return Fail(error, "bad setup size");
    info.setupSize = static_cast<uint64_t>(size);
    std::string digest;
    if (info.setupSha256.size() != 64 || !FromHex(info.setupSha256, &digest)) return Fail(error, "bad sha256");
    for (char& c : info.setupSha256) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (const json::Value* urls = s.Find("urls"); urls && urls->IsArray())
        for (const json::Value& u : urls->AsArray())
            if (u.IsString() && HttpsUrl(u.AsWString())) info.urls.push_back(u.AsWString());
    if (info.urls.empty()) return Fail(error, "no https download address");
    if (!info.page.empty() && !HttpsUrl(info.page)) info.page.clear();
    *out = std::move(info);
    return true;
}

bool ParseUpdateFeed(std::string_view feed, UpdateInfo* out, std::string* error) {
    return ParseUpdateFeed(feed, std::vector<std::string_view>(std::begin(kUpdateKeys), std::end(kUpdateKeys)), out,
                           error);
}

std::vector<std::wstring> UpdateFeedUrls() {
    wchar_t custom[2048];
    const DWORD n = GetEnvironmentVariableW(L"MOONBOW_UPDATE_FEED", custom, static_cast<DWORD>(std::size(custom)));
    if (n > 0 && n < std::size(custom)) return {std::wstring(custom, n)};
    return {kGitHubFeed, kMirrorFeed};
}

}  // namespace setup
