#include "Secret.h"  // relative: the unit tests build it without /I src

#include <windows.h>
#include <wincrypt.h>

#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace net {

namespace {

constexpr std::string_view kPrefix = "dpapi:";
// Ties the blobs to Lumen: another program running as the same user must pass the same entropy.
const char kEntropy[] = "Lumen settings secret v1";

DATA_BLOB EntropyBlob() {
    DATA_BLOB b;
    b.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
    b.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);
    return b;
}

}  // namespace

bool IsProtectedSecret(std::string_view stored) {
    return stored.substr(0, kPrefix.size()) == kPrefix;
}

std::string ProtectSecret(std::string_view plain) {
    if (plain.empty()) return {};
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB entropy = EntropyBlob();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Lumen", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    DWORD len = 0;
    std::string result;
    if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len) && len) {
        std::string b64(len, '\0');
        if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len)) {
            b64.resize(len);
            result = std::string(kPrefix) + b64;
        }
    }
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

bool UnprotectSecret(std::string_view stored, std::string* plain) {
    plain->clear();
    if (!IsProtectedSecret(stored)) {
        plain->assign(stored);
        return true;
    }
    const std::string b64(stored.substr(kPrefix.size()));
    DWORD len = 0;
    if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, nullptr, &len, nullptr,
                              nullptr) ||
        !len)
        return false;
    std::vector<BYTE> blob(len);
    if (!CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, blob.data(), &len,
                              nullptr, nullptr))
        return false;
    DATA_BLOB in{len, blob.data()};
    DATA_BLOB entropy = EntropyBlob();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    plain->assign(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

}  // namespace net
