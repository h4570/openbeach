#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// One description of a setting, used by everything that shows or reads settings: the settings window draws the engine's rows from
// it and the mods screen draws a mod's (docs/modding/reference/mods.md#settings), and a mod's `settings` in mod.json is parsed
// into it. A value is kept as text, the way the ini file holds it; Canonical turns what a file contains into the one spelling the
// rest of the code compares. Pure logic: no game, no Windows API.
namespace obl::settings_schema {

enum class Kind { Toggle, Choice, Number };

struct Option {
    std::string value; // what the file holds
    std::string label; // what a player reads
};

struct Field {
    std::string id; // the key in the ini file; lowercase letters, digits, _ and -
    Kind kind = Kind::Toggle;
    std::string label;
    std::string hint;
    std::string defaultValue; // canonical
    std::vector<Option> options; // Kind::Choice
    double min = 0, max = 0, step = 1; // Kind::Number
    // A numeric choice whose option "0" means "no limit" (a frame cap) goes last when a value of the file is added to the list.
    bool zeroLast = false;
};

inline constexpr size_t kMaxChoices = 32;
inline constexpr size_t kMaxText = 80; // label, hint and option text

// Whether `id` is a setting id.
bool IsValidId(std::string_view id);

// The canonical spelling of `text` for the field, or nullopt when it is not a value of it: a toggle takes 1/0 and
// true/false/yes/no/on/off in any case and gives "1" or "0"; a choice must equal an option's value; a number must lie within
// [min, max] and gives its shortest decimal form.
std::optional<std::string> Canonical(const Field& field, std::string_view text);

// The value to use for a file's `text` (nullopt when the key is missing): its canonical form, else the default.
std::string ValueOrDefault(const Field& field, const std::optional<std::string>& text);

// A toggle's state.
bool IsOn(std::string_view canonical);

// The values a row steps through: a choice's options, plus `current` when the field does not offer it (a file may hold one),
// in numeric order when every option is a number.
std::vector<Option> Choices(const Field& field, std::string_view current);

// The value `delta` steps from `current` (a canonical value): a toggle flips, a choice moves through Choices and wraps, a number
// moves by `step` within its range and stops at the ends.
std::string Step(const Field& field, std::string_view current, int delta);

// What a player reads for a canonical value: an option's label, or the number as written; "on" or "off" for a toggle.
std::string Display(const Field& field, std::string_view canonical);

// The text of a number without trailing zeros.
std::string NumberText(double value);

// Whether every value of the field (default, bounds, step) is a whole number, so a script gets an integer.
bool IsWhole(const Field& field);

} // namespace obl::settings_schema
