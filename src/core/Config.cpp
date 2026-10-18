#include "Config.h"

#include "Paths.h"
#include "../net/Secret.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

using settings::Item;
using settings::ItemType;
using settings::MpvConv;
using settings::MpvMap;

namespace {

const json::Value kNullValue;

// Text form used by enabledWhen conditions.
std::string ConditionText(const json::Value& v) {
    switch (v.type()) {
        case json::Type::Bool: return v.AsBool() ? "true" : "false";
        case json::Type::Number: return json::FormatNumber(v.AsNumber());
        case json::Type::String: return v.AsString();
        default: return {};
    }
}

// Shortcut overrides are keyed by app action ids ("playback.play_pause", see app/Actions.h) or,
// in older files, by settings::KeyActions() ids ("fullscreen").
bool IsActionId(const std::string& id) {
    if (settings::FindKeyAction(id)) return true;
    if (id.empty() || id.size() > 64 || id.find('.') == std::string::npos) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

bool IsHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

std::optional<std::string> MpvValue(const Config& config, const Item& item, const MpvMap& map) {
    const json::Value& v = config.Get(item.key);
    switch (map.conv) {
        case MpvConv::Direct:
            if (v.IsNumber()) return json::FormatNumber(v.AsNumber());
            if (v.IsBool()) return std::string(v.AsBool() ? "yes" : "no");
            if (v.IsString() && !v.AsString().empty()) return v.AsString();
            return std::nullopt;
        case MpvConv::Bool:
            return v.AsBool() ? map.on : map.off;
        case MpvConv::Choice: {
            const settings::Choice* choice = item.FindChoice(v.AsString());
            if (!choice) {
                if (item.dynamicChoices && !v.AsString().empty()) return v.AsString();
                return std::nullopt;
            }
            if (choice->mpv.empty()) return std::nullopt;
            return choice->mpv;
        }
        case MpvConv::Builder:
            return map.build ? map.build(config) : std::nullopt;
        case MpvConv::ExtraArgs:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

// --- Construction / persistence -------------------------------------------------------------

Config::Config() {
    for (const Item* item : settings::AllItems())
        if (item->HasValue()) values_.emplace(item->key, item->def);
}

Config::~Config() {
    if (!path_.empty()) Flush();
}

bool Config::Load() {
    return LoadFrom(DataFilePath(L"settings.json"));
}

bool Config::LoadFrom(const std::wstring& path) {
    path_ = path;
    readOnly_ = false;
    if (path.empty()) return false;
    std::string text;
    if (!json::ReadFileUtf8(path, &text)) {
        DWORD err = GetLastError();
        // No file yet: first start, keep defaults.
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        readOnly_ = true;  // locked by a backup or sync tool: saving now would write defaults over it
        return false;
    }
    std::optional<json::Value> doc = json::Parse(text);
    if (!doc || !doc->IsObject()) {
        // Keep the broken file for the user instead of overwriting it on the next save.
        BackupCorruptFile(path);
        return false;
    }
    bool ok = ApplyDocument(*doc);
    dirty_ = false;
    return ok;
}

bool Config::ApplyDocument(const json::Value& doc) {
    const json::Value* values = doc.Find("values");
    if (!values || !values->IsObject()) return doc.size() == 0;

    // Start from defaults so a reload also reverts keys that are no longer in the file.
    std::unordered_map<std::string, json::Value> next;
    for (const Item* item : settings::AllItems())
        if (item->HasValue()) next.emplace(item->key, item->def);
    unknown_ = json::Value::MakeObject();
    std::unordered_map<std::string, std::string> sealed;
    // Keys that were renamed: an old file keeps its value under the new name (unless the file
    // already has the new key).
    static const std::pair<const char*, const char*> kRenamed[] = {
        {"performance.interpolation", "video.smoothMotion"},
    };
    auto renamed = [&](const std::string& key) -> const char* {
        for (const auto& [from, to] : kRenamed)
            if (key == from && !values->Find(to)) return to;
        return nullptr;
    };
    for (const json::Member& m : values->AsObject()) {
        const char* newKey = renamed(m.key);
        const Item* item = settings::FindItem(newKey ? std::string(newKey) : m.key);
        if (!item) {
            unknown_.Set(m.key, m.value);
            continue;
        }
        if (newKey) {
            if (item->HasValue())
                if (std::optional<json::Value> v = Validate(*item, m.value)) next[item->key] = std::move(*v);
            continue;
        }
        if (!item->HasValue()) continue;
        json::Value value = m.value;
        // Secrets are stored DPAPI-protected (ToJson); one that no longer decrypts (another
        // account or PC) reads as empty rather than as the blob.
        if (item->secret && value.IsString()) {
            std::string plain;
            if (net::UnprotectSecret(value.AsString(), &plain)) {
                value = json::Value(plain);
            } else {
                sealed.emplace(m.key, value.AsString());
                value = json::Value("");
            }
        }
        if (std::optional<json::Value> v = Validate(*item, value)) next[m.key] = std::move(*v);
    }
    for (const Item* item : settings::AllItems()) {
        if (!item->HasValue()) continue;
        auto it = next.find(item->key);
        if (it != next.end()) Store(item->key, std::move(it->second));
    }
    sealedKept_ = std::move(sealed);
    return true;
}

json::Value Config::ToJson() const {
    json::Value values = json::Value::MakeObject();
    for (const Item* item : settings::AllItems()) {
        if (!item->HasValue()) continue;
        const json::Value& v = Get(item->key);
        if (item->secret && v.IsString() && v.AsString().empty())
            if (auto kept = sealedKept_.find(item->key); kept != sealedKept_.end()) {
                values.Set(item->key, kept->second);
                continue;
            }
        if (v == item->def) continue;
        // API keys and passwords never reach the file in plain text.
        if (item->secret && v.IsString() && !v.AsString().empty()) {
            const std::string sealed = net::ProtectSecret(v.AsString());
            if (!sealed.empty()) values.Set(item->key, sealed);
            continue;
        }
        values.Set(item->key, v);
    }
    for (const json::Member& m : unknown_.AsObject()) values.Set(m.key, m.value);
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("values", std::move(values));
    return doc;
}

bool Config::Save() {
    if (readOnly_) return false;
    if (path_.empty()) path_ = DataFilePath(L"settings.json");
    return SaveTo(path_);
}

bool Config::SaveTo(const std::wstring& path) {
    if (path.empty()) return false;
    if (!json::WriteFileAtomic(path, json::Serialize(ToJson(), true))) return false;
    dirty_ = false;
    return true;
}

bool Config::Flush() {
    return !dirty_ || Save();
}

// --- Reads ------------------------------------------------------------------------------------

const json::Value& Config::Get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    return it == values_.end() ? kNullValue : it->second;
}

bool Config::GetBool(std::string_view key) const {
    return Get(key).AsBool(false);
}

int Config::GetInt(std::string_view key) const {
    int64_t v = Get(key).AsInt(0);
    return static_cast<int>(std::clamp<int64_t>(v, INT32_MIN, INT32_MAX));
}

double Config::GetDouble(std::string_view key) const {
    return Get(key).AsNumber(0);
}

std::string Config::GetString(std::string_view key) const {
    return Get(key).AsString();
}

std::wstring Config::GetWString(std::string_view key) const {
    return json::ToWide(Get(key).AsString());
}

bool Config::IsDefault(std::string_view key) const {
    const Item* item = settings::FindItem(key);
    return !item || Get(key) == item->def;
}

bool Config::IsEnabled(const Item& item) const {
    if (!item.enabledWhen) return true;
    // A hidden switch cannot be changed, so a stale stored value must not lock its dependents.
    if (const Item* parent = settings::FindItem(item.enabledWhen->key); parent && parent->hidden) return true;
    bool equal = ConditionText(Get(item.enabledWhen->key)) == item.enabledWhen->value;
    return item.enabledWhen->negate ? !equal : equal;
}

bool Config::IsEnabled(std::string_view key) const {
    const Item* item = settings::FindItem(key);
    return item && IsEnabled(*item);
}

// --- Writes -----------------------------------------------------------------------------------

std::optional<json::Value> Config::Validate(const Item& item, const json::Value& value) {
    switch (item.type) {
        case ItemType::Toggle:
            if (!value.IsBool()) return std::nullopt;
            return value;

        case ItemType::Choice:
        case ItemType::Segmented:
        case ItemType::AccentPicker:
            if (!value.IsString()) return std::nullopt;
            if (item.FindChoice(value.AsString())) return value;
            if (item.dynamicChoices && !value.AsString().empty()) return value;
            return std::nullopt;

        case ItemType::Slider:
        case ItemType::Number: {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber())) return std::nullopt;
            double d = std::clamp(value.AsNumber(), item.min, item.max);
            if (item.step > 0) {
                d = item.min + std::round((d - item.min) / item.step) * item.step;
                d = std::clamp(d, item.min, item.max);
            }
            d = std::round(d * 1e9) / 1e9;  // drop floating-point noise such as 0.30000000000000004
            if (d == 0) d = 0;              // no "-0" in the file
            return json::Value(d);
        }

        case ItemType::Text:
        case ItemType::Path:
            if (!value.IsString()) return std::nullopt;
            return value;

        case ItemType::Color: {
            if (!value.IsString() || !IsHexColor(value.AsString())) return std::nullopt;
            std::string s = value.AsString();
            std::transform(s.begin(), s.end(), s.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
            return json::Value(std::move(s));
        }

        case ItemType::KeyBindings: {
            if (!value.IsObject()) return std::nullopt;
            // Keep only known actions with string fields; drop anything else silently.
            json::Value out = json::Value::MakeObject();
            for (const json::Member& m : value.AsObject()) {
                if (!IsActionId(m.key) || !m.value.IsObject()) continue;
                json::Value binding = json::Value::MakeObject();
                for (const char* field : {"key", "global"}) {
                    const json::Value* f = m.value.Find(field);
                    if (f && f->IsString()) binding.Set(field, *f);
                }
                if (binding.size()) out.Set(m.key, std::move(binding));
            }
            return out;
        }

        case ItemType::Button:
        case ItemType::Info:
            return std::nullopt;
    }
    return std::nullopt;
}

bool Config::Set(std::string_view key, json::Value value) {
    const Item* item = settings::FindItem(key);
    if (!item || !item->HasValue()) return false;
    std::optional<json::Value> v = Validate(*item, value);
    if (!v) return false;
    Store(item->key, std::move(*v));
    return true;
}

bool Config::Store(const std::string& key, json::Value value) {
    auto it = values_.find(key);
    if (it == values_.end() || it->second == value) return false;
    it->second = std::move(value);
    dirty_ = true;
    sealedKept_.erase(key);
    Notify(key);
    return true;
}

void Config::Reset(std::string_view key) {
    const Item* item = settings::FindItem(key);
    if (item && item->HasValue()) Store(item->key, item->def);
}

void Config::ResetCategory(std::string_view categoryId) {
    for (const Item* item : settings::AllItems())
        if (item->category == categoryId && item->HasValue()) Store(item->key, item->def);
}

void Config::ResetAll() {
    for (const Item* item : settings::AllItems())
        if (item->HasValue()) Store(item->key, item->def);
}

bool Config::ApplyEqPreset(std::string_view presetId) {
    const settings::EqPreset* preset = settings::FindEqPreset(presetId);
    if (!preset && presetId != "none") return false;
    // "none" is the flat curve: every band back to 0 dB.
    Set(settings::kEqPreampKey, json::Value(preset ? preset->preamp : 0.0));
    for (int i = 0; i < settings::kEqBands; ++i)
        Set(settings::kEqBandKeys[i], json::Value(preset ? preset->bands[i] : 0.0));
    return Set("audio.eqPreset", json::Value(presetId));
}

// --- Subscriptions ----------------------------------------------------------------------------

Config::Token Config::Subscribe(std::string prefix, Listener listener) {
    auto sub = std::make_shared<Subscription>();
    sub->token = nextToken_++;
    sub->prefix = std::move(prefix);
    sub->listener = std::move(listener);
    subs_.push_back(sub);
    return sub->token;
}

void Config::Unsubscribe(Token token) {
    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        if ((*it)->token == token) {
            (*it)->alive = false;
            subs_.erase(it);
            return;
        }
    }
}

void Config::Notify(const std::string& key) {
    // Iterate over a copy: listeners may subscribe or unsubscribe while we dispatch.
    std::vector<std::shared_ptr<Subscription>> subs = subs_;
    for (const auto& sub : subs) {
        if (!sub->alive || !sub->listener) continue;
        if (key.compare(0, sub->prefix.size(), sub->prefix) == 0) sub->listener(key);
    }
}

// --- libmpv mapping ---------------------------------------------------------------------------

std::vector<MpvOption> MpvOptions(const Config& config) {
    std::vector<MpvOption> out;
    std::vector<MpvOption> extra;
    std::unordered_set<std::string> seen;
    for (const Item* item : settings::AllItems()) {
        for (const MpvMap& map : item->mpv) {
            if (map.conv == MpvConv::ExtraArgs) {
                std::vector<MpvOption> args = ParseMpvArgs(config.GetString(item->key));
                extra.insert(extra.end(), args.begin(), args.end());
                continue;
            }
            if (!seen.insert(map.option).second) continue;
            if (std::optional<std::string> v = MpvValue(config, *item, map)) out.emplace_back(map.option, std::move(*v));
        }
    }
    // The user's own options come last so they win over everything above.
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

std::vector<MpvOption> MpvOptionFor(const Config& config, std::string_view key) {
    std::vector<MpvOption> out;
    const Item* item = settings::FindItem(key);
    if (!item) return out;
    for (const MpvMap& map : item->mpv) {
        if (map.startupOnly || map.conv == MpvConv::ExtraArgs) continue;
        bool dup = false;
        for (const MpvOption& o : out) dup = dup || o.first == map.option;
        if (dup) continue;
        std::optional<std::string> v = MpvValue(config, *item, map);
        out.emplace_back(map.option, v ? std::move(*v) : map.reset);
    }
    return out;
}

std::vector<MpvOption> ParseMpvArgs(std::string_view args) {
    std::vector<MpvOption> out;
    size_t i = 0;
    while (i < args.size()) {
        while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
        if (i >= args.size()) break;
        // One token; quotes group spaces and are removed: --title="My movie" -> title=My movie
        std::string token;
        char quote = 0;
        for (; i < args.size(); ++i) {
            char c = args[i];
            if (quote) {
                if (c == quote)
                    quote = 0;
                else
                    token += c;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                break;
            } else {
                token += c;
            }
        }
        size_t start = token.compare(0, 2, "--") == 0 ? 2 : 0;
        std::string name, value;
        size_t eq = token.find('=', start);
        if (eq == std::string::npos) {
            name = token.substr(start);
            value = "yes";
            if (name.compare(0, 3, "no-") == 0 && name.size() > 3) {
                name = name.substr(3);
                value = "no";
            }
        } else {
            name = token.substr(start, eq - start);
            value = token.substr(eq + 1);
        }
        if (!name.empty()) out.emplace_back(std::move(name), std::move(value));
    }
    return out;
}

namespace {

bool IsFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FindYtDlp() {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring dir(exe, n);
        dir.resize(dir.find_last_of(L"\\/") + 1);
        if (IsFile(dir + L"yt-dlp.exe")) return dir + L"yt-dlp.exe";
    }
    const DWORD len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (len == 0) return {};
    std::wstring path(len, L'\0');
    path.resize(GetEnvironmentVariableW(L"PATH", path.data(), len));
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        std::wstring dir = path.substr(start, end - start);
        start = end + 1;
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        // A UNC folder or a mapped network drive can take seconds to answer when it is offline.
        if (dir.size() < 3 || dir[1] != L':') continue;
        const wchar_t root[] = {dir[0], L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
        if (IsFile(dir + L"yt-dlp.exe")) return dir + L"yt-dlp.exe";
    }
    return {};
}

}  // namespace

const std::wstring& YtDlpPath() {
    static const std::wstring path = FindYtDlp();
    return path;
}
