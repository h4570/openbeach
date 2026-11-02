#include "game/game/command.h"
#include "game/game/command_controls.h"

#include <cmath>

#include <charconv>
#include <system_error>
#include <type_traits>

namespace obl::command {

namespace {

void SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

// Bytes that need no escape: printable ASCII except the escape itself and the field separator.
bool PlainByte(unsigned char c)
{
    return c >= '!' && c <= '~' && c != '%' && c != '=';
}

int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

template<class Int> bool ParseInt(std::string_view text, Int& out)
{
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

bool ParseValue(std::string_view text, std::string& out)
{
    auto decoded = DecodeString(text);
    if (!decoded) return false;
    out = std::move(*decoded);
    return true;
}
bool ParseValue(std::string_view text, int32_t& out)
{
    return ParseInt(text, out);
}
bool ParseValue(std::string_view text, uint32_t& out)
{
    return ParseInt(text, out);
}
bool ParseValue(std::string_view text, float& out)
{
    auto parsed = ParseFloat(text);
    if (!parsed) return false;
    out = *parsed;
    return true;
}
bool ParseValue(std::string_view text, bool& out)
{
    if (text != "0" && text != "1") return false;
    out = text == "1";
    return true;
}

std::string FormatValue(const std::string& value)
{
    return EncodeString(value);
}
std::string FormatValue(int32_t value)
{
    return std::to_string(value);
}
std::string FormatValue(uint32_t value)
{
    return std::to_string(value);
}
std::string FormatValue(float value)
{
    return FormatFloat(value);
}
std::string FormatValue(bool value)
{
    return value ? "1" : "0";
}

struct Token {
    std::string_view name;
    std::string_view value;
    bool used = false;
};

template<class T> std::optional<Command> ParseFields(std::vector<Token>& tokens, std::string* error)
{
    T value{};
    bool ok = true;
    T::Fields(value, [&](const char* name, auto& member) {
        if (!ok) return;
        for (Token& token : tokens) {
            if (token.name != name) continue;
            if (!ParseValue(token.value, member)) {
                SetError(error, std::string(T::kName) + ": bad value '" + std::string(token.value) + "' for " + name);
                ok = false;
                return;
            }
            token.used = true;
            return;
        }
        SetError(error, std::string(T::kName) + ": missing field " + name);
        ok = false;
    });
    if (!ok) return std::nullopt;
    for (const Token& token : tokens) {
        if (token.used) continue;
        SetError(error, std::string(T::kName) + ": no such field " + std::string(token.name));
        return std::nullopt;
    }
    return Command{std::move(value)};
}

template<size_t I = 0> std::optional<Command> ParseNamed(std::string_view name, std::vector<Token>& tokens, std::string* error)
{
    if constexpr (I == std::variant_size_v<Command>) {
        SetError(error, "no such command '" + std::string(name) + "'");
        return std::nullopt;
    } else {
        using T = std::variant_alternative_t<I, Command>;
        if (name == T::kName) return ParseFields<T>(tokens, error);
        return ParseNamed<I + 1>(name, tokens, error);
    }
}

} // namespace

namespace {
template<size_t I = 0>
std::optional<ObjectControl> DecodeObjectControl(ObjectId object, std::string_view name, float value, const char** error)
{
    if constexpr (I == std::variant_size_v<ObjectControl>) {
        if (error) *error = "no such object control";
        return std::nullopt;
    } else {
        using T = std::variant_alternative_t<I, ObjectControl>;
        if (name != ObjectControlTraits<T>::kKeyword) return DecodeObjectControl<I + 1>(object, name, value, error);
        T control{};
        control.object = object;
        if constexpr (std::is_same_v<decltype(control.value), float>) {
            control.value = value;
        } else {
            const double number = value;
            if (!std::isfinite(number) || std::trunc(number) != number) {
                if (error) *error = "keyword takes a whole number";
                return std::nullopt;
            }
            if (number < -2147483648.0 || number > 2147483647.0) {
                if (error) *error = "value outside int32";
                return std::nullopt;
            }
            control.value = static_cast<int32_t>(number);
            if (!ObjectControlWord(control)) {
                if (error) *error = "keyword takes 0..255";
                return std::nullopt;
            }
        }
        return control;
    }
}
} // namespace

std::optional<ObjectControl> MakeObjectControl(ObjectId object, std::string_view name, float value, const char** error)
{
    return DecodeObjectControl(object, name, value, error);
}

std::string_view Name(const Command& command)
{
    return std::visit([](const auto& c) { return std::decay_t<decltype(c)>::kName; }, command);
}

std::string Format(const Command& command)
{
    return std::visit(
            [](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                std::string text(T::kName);
                T::Fields(c, [&](const char* name, const auto& member) {
                    text += ' ';
                    text += name;
                    text += '=';
                    text += FormatValue(member);
                });
                return text;
            },
            command);
}

std::optional<Command> Parse(std::string_view text, std::string* error)
{
    std::vector<std::string_view> words;
    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] == ' ' || text[pos] == '\t') {
            ++pos;
            continue;
        }
        const size_t end = text.find_first_of(" \t", pos);
        const size_t stop = end == std::string_view::npos ? text.size() : end;
        words.push_back(text.substr(pos, stop - pos));
        pos = stop;
    }
    if (words.empty()) {
        SetError(error, "empty command");
        return std::nullopt;
    }
    std::vector<Token> tokens;
    for (size_t i = 1; i < words.size(); ++i) {
        const size_t eq = words[i].find('=');
        if (eq == std::string_view::npos || eq == 0) {
            SetError(error, "expected field=value, got '" + std::string(words[i]) + "'");
            return std::nullopt;
        }
        const std::string_view name = words[i].substr(0, eq);
        for (const Token& token : tokens) {
            if (token.name == name) {
                SetError(error, "duplicate field " + std::string(name));
                return std::nullopt;
            }
        }
        tokens.push_back({name, words[i].substr(eq + 1)});
    }
    return ParseNamed(words[0], tokens, error);
}

std::string EncodeString(std::string_view value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (PlainByte(c)) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

std::optional<std::string> DecodeString(std::string_view text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '%') {
            if (i + 2 >= text.size()) return std::nullopt;
            const int hi = HexDigit(text[i + 1]);
            const int lo = HexDigit(text[i + 2]);
            if (hi < 0 || lo < 0) return std::nullopt;
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else if (PlainByte(c)) {
            out += static_cast<char>(c);
        } else {
            return std::nullopt; // one canonical spelling per string
        }
    }
    return out;
}

std::string FormatFloat(float value)
{
    char buffer[32]; // the shortest round-trip form of a float needs at most 15 characters
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

std::optional<float> ParseFloat(std::string_view text)
{
    if (text.empty()) return std::nullopt;
    float value = 0.0f;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

} // namespace obl::command
