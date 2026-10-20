#include "app/settings/ShortcutStore.h"

#include "app/App.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

namespace app {

namespace {

constexpr const char* kKey = "controls.keyBindings";
bool g_applying = false;  // ignore our own writes while re-applying

struct KeyName {
    UINT vk;
    const char* name;
};

const KeyName kNames[] = {
    {VK_SPACE, "Space"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"}, {VK_DOWN, "Down"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_HOME, "Home"}, {VK_END, "End"}, {VK_RETURN, "Enter"},
    {VK_ESCAPE, "Esc"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"}, {VK_DELETE, "Delete"}, {VK_INSERT, "Insert"},
    {VK_PAUSE, "Pause"}, {VK_SNAPSHOT, "PrintScreen"}, {VK_OEM_PERIOD, "."}, {VK_OEM_COMMA, ","},
    {VK_OEM_4, "["}, {VK_OEM_6, "]"}, {VK_OEM_5, "\\"}, {VK_OEM_2, "/"}, {VK_OEM_MINUS, "-"}, {VK_OEM_PLUS, "="},
    {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "'"}, {VK_OEM_3, "`"}, {VK_MULTIPLY, "NumMultiply"}, {VK_ADD, "NumAdd"},
    {VK_SUBTRACT, "NumSubtract"}, {VK_DIVIDE, "NumDivide"}, {VK_DECIMAL, "NumDecimal"},
    {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"}, {VK_MEDIA_STOP, "MediaStop"}, {VK_MEDIA_NEXT_TRACK, "MediaNext"},
    {VK_MEDIA_PREV_TRACK, "MediaPrev"}, {VK_VOLUME_UP, "VolumeUp"}, {VK_VOLUME_DOWN, "VolumeDown"},
    {VK_VOLUME_MUTE, "VolumeMute"}, {VK_BROWSER_BACK, "BrowserBack"}, {VK_BROWSER_FORWARD, "BrowserForward"},
};

// Older spellings accepted when reading (the schema's KeyActions table, KeyText output).
const KeyName kAliases[] = {
    {VK_PRIOR, "PgUp"}, {VK_NEXT, "PgDn"}, {VK_ESCAPE, "Escape"}, {VK_DELETE, "Del"}, {VK_RETURN, "Return"},
    {VK_OEM_PLUS, "+"}, {VK_OEM_MINUS, "−"}, {VK_LEFT, "←"}, {VK_RIGHT, "→"}, {VK_UP, "↑"}, {VK_DOWN, "↓"},
    {VK_OEM_1, ";"},
};

// settings::KeyActions() ids -> ActionInfo ids.
const std::map<std::string_view, std::string_view>& LegacyIds() {
    static const std::map<std::string_view, std::string_view> m = {
        {"fullscreen", "video.fullscreen"},
        {"exitFullscreen", "video.exit_fullscreen"},
        {"openFile", "media.open_file"},
        {"closeFile", "media.close"},
        {"settings", "app.settings"},
        {"screenshot", "video.snapshot"},
        {"playPause", "playback.play_pause"},
        {"stop", "playback.stop"},
        {"next", "playback.next"},
        {"previous", "playback.previous"},
        {"seekForward", "playback.seek_forward"},
        {"seekBackward", "playback.seek_backward"},
        {"seekForwardVeryShort", "playback.seek_forward_short"},
        {"seekBackwardVeryShort", "playback.seek_backward_short"},
        {"seekForwardMedium", "playback.seek_forward_long"},
        {"seekBackwardMedium", "playback.seek_backward_long"},
        {"seekForwardLong", "playback.seek_forward_xl"},
        {"seekBackwardLong", "playback.seek_backward_xl"},
        {"frameStep", "playback.frame_step"},
        {"frameBackStep", "playback.frame_back"},
        {"speedUp", "playback.speed_up"},
        {"speedDown", "playback.speed_down"},
        {"speedReset", "playback.speed_reset"},
        {"volumeUp", "audio.volume_up"},
        {"volumeDown", "audio.volume_down"},
        {"mute", "audio.mute"},
        {"cycleAudio", "audio.cycle"},
        {"cycleSubtitles", "subtitles.cycle"},
        {"subtitleDelayUp", "subtitles.delay_up"},
        {"subtitleDelayDown", "subtitles.delay_down"},
    };
    return m;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

bool ParseKeyName(std::string_view name, UINT* vk) {
    for (const KeyName& k : kNames)
        if (EqualsNoCase(name, k.name)) return *vk = k.vk, true;
    for (const KeyName& k : kAliases)
        if (name == k.name || EqualsNoCase(name, k.name)) return *vk = k.vk, true;
    if (name.size() == 1) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return *vk = static_cast<UINT>(c), true;
    }
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        int n = 0;
        for (char c : name.substr(1)) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + (c - '0');
        }
        if (n >= 1 && n <= 24) return *vk = static_cast<UINT>(VK_F1 + n - 1), true;
        return false;
    }
    if (name.size() == 4 && EqualsNoCase(name.substr(0, 3), "num") && name[3] >= '0' && name[3] <= '9')
        return *vk = static_cast<UINT>(VK_NUMPAD0 + (name[3] - '0')), true;
    if (name.size() > 2 && EqualsNoCase(name.substr(0, 2), "vk")) {
        unsigned v = 0;
        if (sscanf_s(std::string(name.substr(2)).c_str(), "%x", &v) == 1 && v > 0 && v < 256)
            return *vk = v, true;
    }
    return false;
}

std::vector<KeyBinding> ParseList(std::string_view text) {
    std::vector<KeyBinding> out;
    while (!text.empty()) {
        const size_t semi = text.find(';');
        std::string_view part = Trim(text.substr(0, semi));
        text = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
        KeyBinding k;
        if (!part.empty() && ParseBinding(part, &k)) out.push_back(k);
    }
    return out;
}

void Apply(const json::Value& doc) {
    std::map<std::string, std::vector<KeyBinding>> overrides;
    for (const json::Member& m : doc.AsObject()) {
        const json::Value* key = m.value.Find("key");
        if (!key || !key->IsString()) continue;
        std::string id = m.key;
        auto legacy = LegacyIds().find(id);
        if (legacy != LegacyIds().end()) {
            if (doc.Find(legacy->second)) continue;  // the new id wins
            id = std::string(legacy->second);
        }
        auto keys = ParseList(key->AsString());
        if (keys.empty()) keys.push_back({0, ui::Mods{}});  // explicitly unassigned
        overrides[id] = std::move(keys);
    }
    // A key the user gave one action leaves the defaults of the others: a newer version may have
    // moved a default onto it (Ctrl+F was Open folder, now Search), and the user's choice wins.
    auto claimed = [&](const KeyBinding& k) {
        for (const auto& [id, keys] : overrides)
            for (const KeyBinding& o : keys)
                if (SameBinding(o, k)) return true;
        return false;
    };
    for (const ActionInfo& info : AllActions()) {
        if (info.action == Action::None) continue;
        auto it = overrides.find(info.id);
        if (it != overrides.end()) {
            SetBindings(info.action, it->second);
            continue;
        }
        std::vector<KeyBinding> keep;
        for (const KeyBinding& k : info.keys)
            if (!claimed(k)) keep.push_back(k);
        if (keep.size() == info.keys.size())
            SetBindings(info.action, {});
        else
            SetBindings(info.action, keep.empty() ? std::vector<KeyBinding>{{0, ui::Mods{}}} : keep);
    }
}

}  // namespace

bool SameBinding(const KeyBinding& a, const KeyBinding& b) {
    return a.first == b.first && a.second.ctrl == b.second.ctrl && a.second.shift == b.second.shift &&
           a.second.alt == b.second.alt;
}

std::string BindingToken(const KeyBinding& k) {
    std::string s;
    if (k.second.ctrl) s += "Ctrl+";
    if (k.second.alt) s += "Alt+";
    if (k.second.shift) s += "Shift+";
    const UINT vk = k.first;
    for (const KeyName& n : kNames)
        if (n.vk == vk) return s + n.name;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return s + static_cast<char>(vk);
    if (vk >= VK_F1 && vk <= VK_F24) return s + "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return s + "Num" + static_cast<char>('0' + (vk - VK_NUMPAD0));
    char buf[8];
    snprintf(buf, sizeof(buf), "VK%02X", vk & 0xFF);
    return s + buf;
}

bool ParseBinding(std::string_view text, KeyBinding* out) {
    text = Trim(text);
    if (text.empty()) return false;
    ui::Mods mods;
    // "Ctrl++" names the plus key: a trailing '+' after a separator is the key itself.
    std::string_view keyPart;
    while (true) {
        const size_t plus = text.find('+');
        if (plus == std::string_view::npos || plus + 1 >= text.size()) {
            keyPart = Trim(text);
            break;
        }
        const std::string_view mod = Trim(text.substr(0, plus));
        if (EqualsNoCase(mod, "ctrl") || EqualsNoCase(mod, "control")) mods.ctrl = true;
        else if (EqualsNoCase(mod, "alt")) mods.alt = true;
        else if (EqualsNoCase(mod, "shift")) mods.shift = true;
        else {
            keyPart = Trim(text);  // not a modifier: the rest is the key
            break;
        }
        text = text.substr(plus + 1);
    }
    UINT vk = 0;
    if (!ParseKeyName(keyPart, &vk)) return false;
    *out = {vk, mods};
    return true;
}

std::vector<KeyBinding> ActiveBindings(Action a) {
    std::vector<KeyBinding> out;
    for (const KeyBinding& k : Bindings(a))
        if (k.first != 0) out.push_back(k);
    return out;
}

bool IsDefaultBinding(Action a) {
    const auto& cur = Bindings(a);
    const auto& def = Info(a).keys;
    if (cur.size() != def.size()) return false;
    for (size_t i = 0; i < cur.size(); ++i)
        if (!SameBinding(cur[i], def[i])) return false;
    return true;
}

void SaveKeyBindings() {
    json::Value doc = json::Value::MakeObject();
    for (const ActionInfo& info : AllActions()) {
        if (info.action == Action::None || IsDefaultBinding(info.action)) continue;
        std::string text;
        for (const KeyBinding& k : ActiveBindings(info.action)) text += (text.empty() ? "" : "; ") + BindingToken(k);
        json::Value entry = json::Value::MakeObject();
        entry.Set("key", json::Value(text));
        doc.Set(info.id, std::move(entry));
    }
    g_applying = true;
    TheApp().config().Set(kKey, std::move(doc));
    g_applying = false;
}

void AssignBindings(Action a, std::vector<KeyBinding> keys) {
    keys.erase(std::remove_if(keys.begin(), keys.end(), [](const KeyBinding& k) { return k.first == 0; }), keys.end());
    // Drop duplicates, keep the order.
    std::vector<KeyBinding> unique;
    for (const KeyBinding& k : keys)
        if (std::none_of(unique.begin(), unique.end(), [&](const KeyBinding& u) { return SameBinding(u, k); }))
            unique.push_back(k);
    if (unique.empty()) unique.push_back({0, ui::Mods{}});
    SetBindings(a, std::move(unique));
    if (IsDefaultBinding(a)) SetBindings(a, {});
    SaveKeyBindings();
}

void ResetBindings(Action a) {
    SetBindings(a, {});
    SaveKeyBindings();
}

void ResetAllBindings() {
    for (const ActionInfo& info : AllActions())
        if (info.action != Action::None) SetBindings(info.action, {});
    SaveKeyBindings();
}

namespace {

struct PresetEntry {
    Action action;
    std::vector<KeyBinding> keys;  // replaces the action's bindings
};

const std::vector<PresetEntry>& YouTubePreset() {
    constexpr ui::Mods none{}, shift{false, true, false}, ctrlShift{true, true, false};
    static const std::vector<PresetEntry> preset = [&] {
        std::vector<PresetEntry> p = {
            {Action::PlayPause, {{'K', none}, {VK_SPACE, none}, {VK_MEDIA_PLAY_PAUSE, none}}},
            {Action::SeekBackward, {{'J', none}, {VK_LEFT, none}}},
            {Action::SeekForward, {{'L', none}, {VK_RIGHT, none}}},
            {Action::SpeedDown, {{VK_OEM_COMMA, shift}, {VK_OEM_4, none}}},
            {Action::SpeedUp, {{VK_OEM_PERIOD, shift}, {VK_OEM_6, none}}},
            {Action::ToggleSubtitles, {{'C', none}, {'V', shift}}},
            {Action::Next, {{'N', shift}, {VK_NEXT, none}, {VK_MEDIA_NEXT_TRACK, none}}},
            {Action::Previous, {{'P', shift}, {VK_PRIOR, none}, {VK_MEDIA_PREV_TRACK, none}}},
            {Action::PictureInPicture, {{'I', none}, {'P', ctrlShift}}},
            // Moved off J, K and L.
            {Action::AudioDelayDown, {{'J', shift}}},
            {Action::AudioDelayUp, {{'K', shift}}},
            {Action::ToggleRepeat, {{'R', none}}},
        };
        for (int i = 0; i < 10; ++i)
            p.push_back({static_cast<Action>(static_cast<int>(Action::JumpTo0) + i),
                         {{static_cast<UINT>('0' + i), none}, {static_cast<UINT>(VK_NUMPAD0 + i), none}}});
        return p;
    }();
    return preset;
}

}  // namespace

bool ApplyKeyPreset(std::string_view id) {
    if (id != "youtube") return false;
    const std::vector<PresetEntry>& preset = YouTubePreset();
    for (const ActionInfo& info : AllActions())
        if (info.action != Action::None) SetBindings(info.action, {});
    // The preset's keys leave every action they are not assigned to.
    for (const ActionInfo& info : AllActions()) {
        if (info.action == Action::None) continue;
        if (std::any_of(preset.begin(), preset.end(), [&](const PresetEntry& e) { return e.action == info.action; }))
            continue;
        std::vector<KeyBinding> keep;
        for (const KeyBinding& k : ActiveBindings(info.action)) {
            const bool taken = std::any_of(preset.begin(), preset.end(), [&](const PresetEntry& e) {
                return std::any_of(e.keys.begin(), e.keys.end(), [&](const KeyBinding& p) { return SameBinding(p, k); });
            });
            if (!taken) keep.push_back(k);
        }
        if (keep.size() != ActiveBindings(info.action).size())
            SetBindings(info.action, keep.empty() ? std::vector<KeyBinding>{{0, ui::Mods{}}} : keep);
    }
    for (const PresetEntry& e : preset) {
        SetBindings(e.action, e.keys);
        if (IsDefaultBinding(e.action)) SetBindings(e.action, {});
    }
    SaveKeyBindings();
    return true;
}

void LoadKeyBindings() {
    Config& config = TheApp().config();
    Apply(config.Get(kKey));
    static bool subscribed = false;
    if (subscribed) return;
    subscribed = true;
    config.Subscribe(kKey, [](const std::string&) {
        if (!g_applying) Apply(TheApp().config().Get(kKey));
    });
}

}  // namespace app
