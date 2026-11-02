#include "core/ini_file.h"

#include "core/ascii_case.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace obl::ini {
namespace {

bool IsBlank(char c)
{
    return c == ' ' || c == '\t';
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && IsBlank(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsBlank(text.back())) text.remove_suffix(1);
    return text;
}

bool SameName(std::string_view a, std::string_view b)
{
    return ascii::EqualsNoCase(Trim(a), Trim(b));
}

struct Line {
    size_t begin = 0; // first character
    size_t end = 0; // one past the content, before the line ending
    size_t next = 0; // one past the line ending (== end at the end of a file without one)
};

std::vector<Line> SplitLines(std::string_view text)
{
    std::vector<Line> lines;
    size_t at = 0;
    while (at < text.size()) {
        Line line;
        line.begin = at;
        while (at < text.size() && text[at] != '\r' && text[at] != '\n') ++at;
        line.end = at;
        if (at < text.size() && text[at] == '\r') ++at;
        if (at < text.size() && text[at] == '\n' && (at == line.end || text[at - 1] == '\r')) ++at;
        line.next = at;
        lines.push_back(line);
    }
    return lines;
}

std::string_view Content(std::string_view text, const Line& line)
{
    return text.substr(line.begin, line.end - line.begin);
}

// The section name of a header line, or nullopt for any other line.
std::optional<std::string_view> HeaderName(std::string_view content)
{
    const std::string_view trimmed = Trim(content);
    if (trimmed.empty() || trimmed.front() != '[') return std::nullopt;
    const size_t close = trimmed.find(']');
    return trimmed.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1);
}

// The position of `=` of an entry line (not a comment), or npos.
size_t EntryEquals(std::string_view content)
{
    const std::string_view trimmed = Trim(content);
    if (trimmed.empty() || trimmed.front() == ';') return std::string_view::npos;
    return content.find('=');
}

struct Located {
    std::optional<size_t> header; // line index of the first matching section
    std::optional<size_t> entry; // line index of the key in it
};

Located Locate(std::string_view text, const std::vector<Line>& lines, std::string_view section, std::string_view key)
{
    Located found;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view content = Content(text, lines[i]);
        if (const auto name = HeaderName(content)) {
            if (found.header) break; // only the first section of the name counts
            if (SameName(*name, section)) found.header = i;
            continue;
        }
        if (!found.header) continue;
        const size_t equals = EntryEquals(content);
        if (equals != std::string_view::npos && SameName(content.substr(0, equals), key)) {
            found.entry = i;
            break;
        }
    }
    return found;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

std::optional<std::string> Find(std::string_view text, std::string_view section, std::string_view key)
{
    const std::vector<Line> lines = SplitLines(text);
    const Located found = Locate(text, lines, section, key);
    if (!found.entry) return std::nullopt;
    const std::string_view content = Content(text, lines[*found.entry]);
    std::string_view value = Trim(content.substr(content.find('=') + 1));
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
        value = value.substr(1, value.size() - 2);
    return std::string(value);
}

std::string Set(std::string_view sourceText, std::string_view section, std::string_view key, std::string_view value)
{
    // Every write first ends an unterminated last line with \r\n (also when the value does not change).
    std::string text(sourceText);
    if (!text.empty() && text.back() != '\n') text += "\r\n";
    const std::vector<Line> lines = SplitLines(text);
    const Located found = Locate(text, lines, section, key);
    std::string out = text;
    if (found.entry) {
        const Line& line = lines[*found.entry];
        const size_t from = line.begin + Content(text, line).find('=') + 1;
        size_t to = line.end;
        while (to > from && IsBlank(text[to - 1])) --to;
        out.replace(from, to - from, value);
        return out;
    }
    const std::string entry = std::string(key) + "=" + std::string(value) + "\r\n";
    if (found.header) {
        size_t last = *found.header;
        for (size_t i = last + 1; i < lines.size(); ++i) {
            const std::string_view trimmed = Trim(Content(text, lines[i]));
            if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '[') break;
            last = i;
        }
        out.insert(lines[last].next, entry);
        return out;
    }
    out += "[" + std::string(section) + "]\r\n" + entry;
    return out;
}

std::string ReadString(
        const std::filesystem::path& path, std::string_view section, std::string_view key, std::string_view fallback, size_t size)
{
    std::string result;
    const std::optional<std::string> text = ReadFile(path);
    if (const std::optional<std::string> value = text ? Find(*text, section, key) : std::nullopt) {
        result = *value;
    } else {
        while (!fallback.empty() && IsBlank(fallback.back())) fallback.remove_suffix(1);
        result = fallback;
    }
    if (result.size() >= size) result.resize(size ? size - 1 : 0);
    return result;
}

int ReadInt(const std::filesystem::path& path, std::string_view section, std::string_view key, int fallback)
{
    const std::optional<std::string> text = ReadFile(path);
    const std::optional<std::string> value = text ? Find(*text, section, key) : std::nullopt;
    if (!value || value->empty()) return fallback;
    size_t at = 0;
    const bool negative = (*value)[0] == '-';
    if ((*value)[0] == '-' || (*value)[0] == '+') ++at;
    uint32_t number = 0;
    if (value->size() > at + 1 && (*value)[at] == '0' && ((*value)[at + 1] == 'x' || (*value)[at + 1] == 'X')) {
        for (at += 2; at < value->size(); ++at) {
            const char c = ascii::Lower((*value)[at]);
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) break;
            number = number * 16 + static_cast<uint32_t>(digit);
        }
    } else {
        for (; at < value->size() && (*value)[at] >= '0' && (*value)[at] <= '9'; ++at)
            number = number * 10 + static_cast<uint32_t>((*value)[at] - '0');
    }
    return static_cast<int>(negative ? 0u - number : number);
}

bool WriteString(const std::filesystem::path& path, std::string_view section, std::string_view key, std::string_view value)
{
    const std::string text = Set(ReadFile(path).value_or(std::string()), section, key, value);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

} // namespace obl::ini
