#pragma once
// Runtime settings store keyed by schema key ("general.theme").
//
// Threading: UI thread only. Config is not synchronized; listeners run synchronously inside
// Set()/Reset*()/Load*() on the calling thread. Worker threads that need a setting should be
// handed a copy of the value.
//
// Persistence: %APPDATA%\Lumen\settings.json (see Paths.h), only non-default values are written.
// Changes are not saved immediately: Set() marks the store dirty and the UI calls Flush() from a
// timer and at exit (the destructor flushes too). Unknown keys found in the file are kept and
// written back, so a newer settings.json survives a round-trip through an older Lumen.

#include "Json.h"
#include "SettingsSchema.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class Config {
public:
    using Token = uint64_t;
    using Listener = std::function<void(const std::string& key)>;

    Config();  // every setting at its schema default
    ~Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // --- Persistence -------------------------------------------------------------------------
    // Load() reads DataFilePath(L"settings.json"). A missing file is not an error. A file that
    // fails to parse is copied to settings.corrupt-<timestamp>.json (BackupCorruptFile) and false is returned (defaults stay).
    // Values that fail validation are skipped. Listeners are notified for every key that changed.
    bool Load();
    bool LoadFrom(const std::wstring& path);
    bool Save();  // writes now to the path of the last Load (or the default path)
    bool SaveTo(const std::wstring& path);
    void MarkDirty() { dirty_ = true; }
    bool Dirty() const { return dirty_; }
    bool Flush();  // Save() when dirty; true when nothing needed saving
    const std::wstring& FilePath() const { return path_; }

    // The document Save() would write: {"version": 1, "values": {key: value, ...}}.
    json::Value ToJson() const;

    // --- Reads -------------------------------------------------------------------------------
    // Unknown keys return null / false / 0 / "".
    const json::Value& Get(std::string_view key) const;
    bool GetBool(std::string_view key) const;
    int GetInt(std::string_view key) const;
    double GetDouble(std::string_view key) const;
    std::string GetString(std::string_view key) const;  // UTF-8
    std::wstring GetWString(std::string_view key) const;
    bool IsDefault(std::string_view key) const;
    // Evaluates the item's enabledWhen condition (true when it has none).
    bool IsEnabled(const settings::Item& item) const;
    bool IsEnabled(std::string_view key) const;

    // --- Writes ------------------------------------------------------------------------------
    // Validates against the schema. Returns false (and changes nothing) for unknown keys,
    // Button / Info rows, a wrong JSON type, a choice not in the list, or a malformed color.
    // Numbers are clamped to [min, max] and snapped to `step`. Setting the current value is a
    // no-op that still returns true.
    bool Set(std::string_view key, json::Value value);
    bool SetBool(std::string_view key, bool v) { return Set(key, json::Value(v)); }
    bool SetInt(std::string_view key, int v) { return Set(key, json::Value(v)); }
    bool SetDouble(std::string_view key, double v) { return Set(key, json::Value(v)); }
    bool SetString(std::string_view key, std::string_view v) { return Set(key, json::Value(v)); }
    bool SetWString(std::string_view key, std::wstring_view v) { return Set(key, json::Value(json::ToUtf8(v))); }

    void Reset(std::string_view key);
    void ResetCategory(std::string_view categoryId);  // "audio" resets every "audio.*" item
    void ResetAll();

    // Copies an equalizer preset into audio.eqPreamp / audio.eq60.. and sets audio.eqPreset.
    bool ApplyEqPreset(std::string_view presetId);

    // Sanitized copy of `value` for `item`, or nullopt when it is not acceptable.
    static std::optional<json::Value> Validate(const settings::Item& item, const json::Value& value);

    // --- Change notifications ----------------------------------------------------------------
    // `listener(key)` runs after `key` changed, for every key starting with `prefix` ("" = all,
    // "audio." = the audio category, "audio.eq" = equalizer rows). Unsubscribing (or
    // subscribing) from inside a listener is allowed.
    Token Subscribe(std::string prefix, Listener listener);
    void Unsubscribe(Token token);

private:
    struct Subscription {
        Token token = 0;
        std::string prefix;
        Listener listener;
        bool alive = true;
    };

    // Stores `value` (already validated) and notifies; returns true when it changed.
    bool Store(const std::string& key, json::Value value);
    void Notify(const std::string& key);
    bool ApplyDocument(const json::Value& doc);

    std::unordered_map<std::string, json::Value> values_;
    json::Value unknown_ = json::Value::MakeObject();  // keys from the file we do not know
    std::vector<std::shared_ptr<Subscription>> subs_;
    Token nextToken_ = 1;
    std::wstring path_;
    bool dirty_ = false;
    bool readOnly_ = false;  // the file exists but could not be read: never overwrite it
    // Secrets sealed on another account or PC: written back as they were until replaced.
    std::unordered_map<std::string, std::string> sealedKept_;
};

// --- libmpv mapping ------------------------------------------------------------------------

using MpvOption = std::pair<std::string, std::string>;

// Every mpv option derived from the current settings, in schema order, each option once.
// Settings at a value that means "mpv default" are omitted. Pass these to mpv_set_option_string
// before mpv_initialize (after Lumen's fixed options such as wid / osc / input-*).
std::vector<MpvOption> MpvOptions(const Config& config);

// Options to set at runtime (mpv_set_property_string) after `key` changed. Usually one pair;
// some settings affect several options (playback.repeat -> loop-file + loop-playlist) and
// startup-only settings (default speed, startup volume, ...) return nothing. Options that would
// be omitted come back with the value that restores mpv's default.
std::vector<MpvOption> MpvOptionFor(const Config& config, std::string_view key);

// Splits "--name=value --flag name2=value2" (quotes allowed) into options; "--flag" -> "yes".
std::vector<MpvOption> ParseMpvArgs(std::string_view args);

// yt-dlp.exe next to Lumen.exe or in a local folder on PATH; empty when there is none. Looked up
// once per process (network drives on PATH are skipped so the lookup cannot stall). Lumen never
// downloads it. Thread-safe.
const std::wstring& YtDlpPath();
