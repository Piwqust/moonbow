#pragma once
// Static description of every setting shown in the Settings window (design/ref/Settings-*.png).
//
// Layout: group (App / Media / System) -> category (General, Interface, ...) -> section
// ("Appearance", "Startup", ...) -> item. Items under the design's "Advanced Options" expander
// have `advanced = true`; the UI lists them after the expander, in schema order.
//
// Every item carries English and Russian text, its default value, validation data (choices,
// range) and, when the setting maps to libmpv, one or more `MpvMap`s that Config turns into mpv
// options (see MpvOptions() in Config.h).
//
// The schema is built once on first use and never changes; pointers into it stay valid for the
// lifetime of the process. Read-only after construction, so it is safe from any thread.

#include "Json.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Config;

namespace settings {

enum class Lang { En, Ru };

enum class ItemType {
    Toggle,        // bool
    Choice,        // dropdown; string value from `choices`
    Segmented,     // segmented buttons; string value from `choices`
    Slider,        // number in [min, max], snapped to `step`
    Number,        // number field in [min, max], snapped to `step`
    Text,          // free text (`secret` = masked input)
    Path,          // text field + Browse; see `pathKind`
    Color,         // "#RRGGBB"
    AccentPicker,  // swatches; string value from `choices`, swatch color in Choice::extra
    KeyBindings,   // object {actionId: {"key": "...", "global": "..."}} of overrides; see KeyActions()
    Button,        // action row; no value. `action` names what the UI should do
    Info,          // custom widget / read-only row; no value. `action` names the widget
};

enum class PathKind { File, Folder, SaveFile };

// How a setting becomes an mpv option.
enum class MpvConv {
    Direct,     // value as text: numbers via json::FormatNumber, strings as-is (empty -> omitted)
    Bool,       // toggle -> `on` / `off`
    Choice,     // choice -> Choice::mpv (empty -> omitted)
    Builder,    // `build(config)`; nullopt -> omitted. Used for options combining several settings
    ExtraArgs,  // free-form "--name=value ..." list, expanded into several options
};

using MpvBuilder = std::optional<std::string> (*)(const Config&);

struct MpvMap {
    std::string option;  // mpv option / property name, e.g. "hwdec"
    MpvConv conv = MpvConv::Direct;
    std::string on = "yes";  // MpvConv::Bool values
    std::string off = "no";
    MpvBuilder build = nullptr;
    std::string reset;         // value that restores mpv's default when the option is omitted
    bool startupOnly = false;  // a "default" applied when mpv starts, never pushed live
};

struct Choice {
    std::string value;     // stored in settings.json
    std::wstring label;    // English
    std::wstring labelRu;  // Russian
    std::string mpv;       // mpv value for MpvConv::Choice
    std::string extra;     // swatch color ("#EDEDED") or icon name ("eq_eq-rock")

    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
};

// The item is enabled (not greyed out) when the value of `key` equals `value` (or, with
// `negate`, differs from it). Toggles compare as "true" / "false".
struct Condition {
    std::string key;
    std::string value;
    bool negate = false;
};

struct Item {
    std::string key;  // "general.theme"; the prefix before the first dot is the category id
    ItemType type = ItemType::Toggle;
    std::wstring label, labelRu;
    std::wstring description, descriptionRu;  // the (i) tooltip; empty when the design has none
    json::Value def;                          // null for Button / Info

    std::vector<Choice> choices;
    bool dynamicChoices = false;  // choices are filled at runtime (audio devices); any string is accepted

    double min = 0, max = 0, step = 0;  // Slider / Number (step 0 = continuous)
    std::wstring unit, unitRu;          // "s", "%", "dB" ...

    PathKind pathKind = PathKind::File;
    std::wstring placeholder, placeholderRu;  // hint text for empty Text / Path fields

    std::string action;                       // Button / Info: what to do / which widget
    std::wstring actionLabel, actionLabelRu;  // Button caption ("Browse", "Reset", ...)

    bool advanced = false;  // under "Advanced Options"
    // Shown in the Settings window's "Basic" mode, with `icon` and the one-line `summary` under the label.
    bool basic = false;
    std::string icon;
    std::wstring summary, summaryRu;
    bool secret = false;    // password-like text
    bool hidden = false;    // not a row in the Settings window (edited elsewhere or internal)
    bool inferred = false;  // not visible in the design frames (below the fold); chosen by us
    std::string group;      // rows the UI may draw as one widget: "equalizer", "compressor", ...
    std::optional<Condition> enabledWhen;
    std::vector<MpvMap> mpv;

    std::string category;  // filled by the schema
    std::string section;

    bool HasValue() const { return type != ItemType::Button && type != ItemType::Info; }
    bool HasChoices() const {
        return type == ItemType::Choice || type == ItemType::Segmented || type == ItemType::AccentPicker;
    }
    const Choice* FindChoice(std::string_view value) const;
    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
    const std::wstring& Description(Lang lang) const { return lang == Lang::Ru ? descriptionRu : description; }
    const std::wstring& Unit(Lang lang) const { return lang == Lang::Ru ? unitRu : unit; }
    const std::wstring& Placeholder(Lang lang) const { return lang == Lang::Ru ? placeholderRu : placeholder; }
    const std::wstring& ActionLabel(Lang lang) const { return lang == Lang::Ru ? actionLabelRu : actionLabel; }
    const std::wstring& Summary(Lang lang) const { return lang == Lang::Ru ? summaryRu : summary; }
};

struct Section {
    std::string id;
    std::wstring title, titleRu;
    std::vector<Item> items;

    const std::wstring& Title(Lang lang) const { return lang == Lang::Ru ? titleRu : title; }
};

struct Category {
    std::string id;     // "general"
    std::string group;  // "app", "media", "system"
    std::string icon;   // design/icons name ("preferences"); add "_active" for the selected state
    std::wstring label, labelRu;  // sidebar entry and page title: one name, so the page is where the click led
    std::vector<Section> sections;

    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
};

struct Group {
    std::string id;
    std::wstring label, labelRu;

    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
};

const std::vector<Group>& Groups();
const std::vector<Category>& Categories();
const Category* FindCategory(std::string_view id);
const Item* FindItem(std::string_view key);
std::vector<const Item*> AllItems();  // schema order

// Settings search box: case-insensitive, in both languages (so "громк" and "volume" both work).
// Every word of the query must appear in the row's label, description, choices, section or page
// title. Hidden items are skipped.
std::vector<const Item*> Search(std::wstring_view query);

// Text of the "Advanced Options" expander.
const std::wstring& AdvancedOptionsLabel(Lang lang);

// --- Equalizer -----------------------------------------------------------------------------

inline constexpr int kEqBands = 10;
extern const char* const kEqBandKeys[kEqBands];  // "audio.eq60" ... "audio.eq16k"
extern const double kEqFrequencies[kEqBands];    // 60 ... 16000 Hz
inline constexpr const char* kEqPreampKey = "audio.eqPreamp";

// mpv "hwdec" for performance.hwdec = auto, and the decoder thread count for decodeThreads = 0
// (also Player's defaults before the settings are applied).
inline constexpr const char* kAutoHwdec = "d3d11va,d3d11va-copy";
int AutoDecodeThreads();

struct EqPreset {
    std::string id;  // same as the audio.eqPreset choice value
    double preamp = 0;
    double bands[kEqBands] = {};
};

// VLC's presets, with the preamp shifted so that 0 dB means "no change" (VLC uses +12 dB as unity).
const std::vector<EqPreset>& EqPresets();
const EqPreset* FindEqPreset(std::string_view id);

// --- Keyboard shortcuts (Keyboard_Shortcuts dialog) ------------------------------------------
// The settings window edits shortcuts through app/Actions.h (the single source of truth for the
// UI); controls.keyBindings is keyed by ActionInfo ids there. The ids below are still accepted and
// mapped when settings.json is read (app/settings/ShortcutStore.cpp).

struct KeyAction {
    std::string id;     // "fullscreen"
    std::string group;  // "basic", "playback", "audio", "subtitles", "view"
    std::wstring label, labelRu;
    std::wstring defaultKey;  // "F", "Space", "Ctrl+Right"; empty = unassigned

    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
};

struct KeyActionGroup {
    std::string id;
    std::wstring label, labelRu;

    const std::wstring& Label(Lang lang) const { return lang == Lang::Ru ? labelRu : label; }
};

const std::vector<KeyActionGroup>& KeyActionGroups();
const std::vector<KeyAction>& KeyActions();
const KeyAction* FindKeyAction(std::string_view id);

}  // namespace settings
