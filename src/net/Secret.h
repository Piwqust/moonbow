#pragma once
// API keys and passwords at rest: DPAPI (CryptProtectData, current user) wrapped as
// "dpapi:<base64>". settings.json stores every `secret` setting in this form (Config.cpp), so a
// copy of the file is useless on another account or PC. In memory the values stay plain text.
#include <string>
#include <string_view>

namespace net {

// "dpapi:..." for a non-empty value; empty when `plain` is empty or DPAPI failed.
std::string ProtectSecret(std::string_view plain);
// True with the plain text for a "dpapi:" value that decrypts. A value without the prefix is
// returned as is (older settings files kept secrets in plain text) and also counts as success.
bool UnprotectSecret(std::string_view stored, std::string* plain);
bool IsProtectedSecret(std::string_view stored);

}  // namespace net
