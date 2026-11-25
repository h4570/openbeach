#include "core/settings_schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace obl::settings_schema {

namespace {

std::optional<double> ParseNumber(std::string_view text)
{
    if (text.empty() || text.size() > 32) return std::nullopt;
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

} // namespace

bool IsValidId(std::string_view id)
{
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string NumberText(double value)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%.10g", value);
    return text;
}

std::optional<std::string> Canonical(const Field& field, std::string_view text)
{
    switch (field.kind) {
    case Kind::Toggle: {
        const std::string lower = Lower(text);
        if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return "1";
        if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return "0";
        return std::nullopt;
    }
    case Kind::Choice:
        for (const Option& option : field.options)
            if (option.value == text) return option.value;
        return std::nullopt;
    case Kind::Number: {
        const std::optional<double> value = ParseNumber(text);
        if (!value || *value < field.min || *value > field.max) return std::nullopt;
        return NumberText(*value);
    }
    }
    return std::nullopt;
}

std::string ValueOrDefault(const Field& field, const std::optional<std::string>& text)
{
    if (text)
        if (const auto canonical = Canonical(field, *text)) return *canonical;
    return field.defaultValue;
}

bool IsOn(std::string_view canonical)
{
    return canonical == "1";
}

std::vector<Option> Choices(const Field& field, std::string_view current)
{
    std::vector<Option> list = field.options;
    if (field.kind != Kind::Choice || current.empty()) return list;
    if (std::any_of(list.begin(), list.end(), [&](const Option& o) { return o.value == current; })) return list;
    const std::optional<double> value = ParseNumber(current);
    const bool numeric = value && std::all_of(list.begin(), list.end(), [](const Option& o) { return ParseNumber(o.value).has_value(); });
    Option extra{std::string(current), std::string(current)};
    if (!numeric) {
        list.push_back(std::move(extra));
        return list;
    }
    // Numeric order, with "0" (no limit) last when the field says so.
    const auto rank = [&](double number) { return field.zeroLast && number == 0 ? INFINITY : number; };
    const auto at = std::find_if(list.begin(), list.end(), [&](const Option& o) { return rank(*ParseNumber(o.value)) > rank(*value); });
    list.insert(at, std::move(extra));
    return list;
}

std::string Step(const Field& field, std::string_view current, int delta)
{
    switch (field.kind) {
    case Kind::Toggle: return IsOn(current) ? "0" : "1";
    case Kind::Choice: {
        const std::vector<Option> list = Choices(field, current);
        if (list.empty()) return std::string(current);
        const auto found = std::find_if(list.begin(), list.end(), [&](const Option& o) { return o.value == current; });
        const int count = static_cast<int>(list.size());
        const int at = found == list.end() ? 0 : static_cast<int>(found - list.begin());
        return list[static_cast<size_t>(((at + delta) % count + count) % count)].value;
    }
    case Kind::Number: {
        const double value = ParseNumber(current).value_or(ParseNumber(field.defaultValue).value_or(field.min));
        return NumberText(std::clamp(value + field.step * delta, field.min, field.max));
    }
    }
    return std::string(current);
}

std::string Display(const Field& field, std::string_view canonical)
{
    switch (field.kind) {
    case Kind::Toggle: return IsOn(canonical) ? "on" : "off";
    case Kind::Choice:
        for (const Option& option : field.options)
            if (option.value == canonical) return option.label;
        return std::string(canonical);
    case Kind::Number: return std::string(canonical);
    }
    return std::string(canonical);
}

bool IsWhole(const Field& field)
{
    const auto whole = [](double v) { return v == std::floor(v); };
    return whole(field.min) && whole(field.max) && whole(field.step) && whole(ParseNumber(field.defaultValue).value_or(0.5));
}

} // namespace obl::settings_schema
