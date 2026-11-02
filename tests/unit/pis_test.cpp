
#include "unit_test.h"

#include "core/ascii_case.h"
#include "importer/src/readers/pis.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace obl::pis;

namespace {

// The pis file load routine passes size+1 bytes, the last one a NUL.
std::vector<Entry> Parse(const std::string& text)
{
    std::string buffer = text;
    buffer.push_back('\0');
    buffer.push_back('\0'); // the padding byte the scanners may peek at
    return ParseEntries(buffer.data(), static_cast<int>(text.size()) + 1);
}

int Count(const std::string& text)
{
    std::string buffer = text;
    buffer.push_back('\0');
    buffer.push_back('\0');
    return CountEntries(buffer.data(), static_cast<int>(text.size()) + 1);
}

} // namespace

OBL_TEST_CASE("pis.scanners")
{
    OBL_CHECK_EQ(QuotedLength("\"ab\"", 4), 4);
    OBL_CHECK_EQ(QuotedLength("x\"a", 3), 0); // one quote only
    OBL_CHECK_EQ(QuotedLength("\"a\"b\"", 5), 3); // the second quote ends it
    OBL_CHECK_EQ(QuotedLength("\"ab\"", 3), 0); // the window ends before the closing quote

    OBL_CHECK_EQ(FindKeyEnd("'AB' 1", 6), 4);
    OBL_CHECK_EQ(FindKeyEnd("'AB", 3), 3); // unterminated: the window end
    OBL_CHECK_EQ(FindKeyEnd("'", 1), 1);
    OBL_CHECK_EQ(FindKeyEnd("''", 2), 2);

    OBL_CHECK_EQ(TokenLength("\"a b\" 5", 7), 5);
    OBL_CHECK_EQ(TokenLength("\"abc", 4), 4); // unterminated
    OBL_CHECK_EQ(TokenLength("123 x", 5), 3);
    OBL_CHECK_EQ(TokenLength("ab{c", 4), 2); // '{' (0x7b) ends a bare token
    OBL_CHECK_EQ(TokenLength("ab\x80"
                             "c",
                         4),
            2); // bytes >= 0x80 compare as negative chars
    OBL_CHECK_EQ(TokenLength("yz", 2), 1); // 'z' is the first byte outside the token range

    OBL_CHECK_EQ(SkipSeparators(" \t\r\nA", 5), 4);
    OBL_CHECK_EQ(SkipSeparators("\x80{ a", 4), 3);
    OBL_CHECK_EQ(SkipSeparators("   ", 3), 3);

    OBL_CHECK(HasKey("'a' 1", 5));
    OBL_CHECK(!HasKey("'a 1", 4));
    OBL_CHECK(!HasKey("'a' 1", 2)); // only one quote inside the window

    OBL_CHECK_EQ(SkipToKey("  'A'", 5), 2);
    OBL_CHECK_EQ(SkipToKey("x // it's\r\n 'A'", 15), 12); // quotes inside the comment are skipped
    OBL_CHECK_EQ(SkipToKey("//c\r\n'A'", 8), 5);
    OBL_CHECK_EQ(SkipToKey("abc", 3), 3);

    OBL_CHECK_EQ(ClassifyValue("12", 2), static_cast<int>(Number));
    OBL_CHECK_EQ(ClassifyValue("-x", 2), static_cast<int>(Number));
    OBL_CHECK_EQ(ClassifyValue(".x", 2), static_cast<int>(Number));
    OBL_CHECK_EQ(ClassifyValue("\"x\"", 3), static_cast<int>(String));
    OBL_CHECK_EQ(ClassifyValue("abc", 3), static_cast<int>(None));
    OBL_CHECK_EQ(ClassifyValue("\"abc", 4), static_cast<int>(None));
}

OBL_TEST_CASE("pis.numbers")
{
    OBL_CHECK_EQ(ParseNumber("42"), 42.0);
    OBL_CHECK_EQ(ParseNumber("-7"), -7.0);
    OBL_CHECK(std::fabs(ParseNumber("12.5") - 12.5) < 1e-6);
    OBL_CHECK(std::fabs(ParseNumber("0.25") - 0.25) < 1e-8);
    OBL_CHECK_EQ(ParseNumber(".0"), 0.0);
    OBL_CHECK_EQ(ParseNumber("1abc"), 1.0);
    OBL_CHECK(std::signbit(ParseNumber("-")));
    OBL_CHECK(ParseNumber("0.1") == static_cast<double>(0.1f));
    OBL_CHECK(ParseNumber("0.1") != 0.1);
}

OBL_TEST_CASE("pis.parses_entries")
{
    std::vector<Entry> entries = Parse("// comment 'with' quotes\r\n"
                                       "\t'NAME'\t\"Beach Life\"\r\n"
                                       "'COUNT' 12\r\n"
                                       "'RATE' -0.5\r\n"
                                       "'BARE' word\r\n"
                                       "'EMPTY' \"\"\r\n"
                                       "'LAST' \"unterminated");
    OBL_REQUIRE(entries.size() == 6);
    OBL_CHECK_EQ(Count("// c\r\n'A' 1\r\n'B' 2"), 2);

    OBL_CHECK_EQ(entries[0].key, std::string("NAME"));
    OBL_CHECK_EQ(entries[0].type, static_cast<int>(String));
    OBL_CHECK(entries[0].hasString);
    OBL_CHECK_EQ(entries[0].string, std::string("Beach Life"));
    OBL_CHECK_EQ(entries[1].type, static_cast<int>(Number));
    OBL_CHECK_EQ(entries[1].number, 12.0f);
    OBL_CHECK(std::fabs(entries[2].number + 0.5f) < 1e-6f);
    OBL_CHECK_EQ(entries[3].type, static_cast<int>(None)); // a bare word has no value
    OBL_CHECK(!entries[3].hasString && !entries[3].hasNumber);
    OBL_CHECK(entries[4].hasString);
    OBL_CHECK_EQ(entries[4].string, std::string(""));
    OBL_CHECK_EQ(entries[5].type, static_cast<int>(None)); // unterminated string
}

OBL_TEST_CASE("pis.count_matches_parse")
{
    for (const char* text : {"", "   ", "'A' 1", "'A' 1 'B' \"x\" // tail\r\n", "// only a comment\r\n", "'A'\r\n'B' 2"}) {
        OBL_CHECK_EQ(Count(text), static_cast<int>(Parse(text).size()));
    }
    // A comment in front of the first key and apostrophes inside values do not confuse the walk.
    OBL_CHECK_EQ(Count("'A' \"it's\" 'B' 2"), 2);
}

OBL_TEST_CASE("pis.lookup_uses_the_last_matching_key_and_type")
{
    float number = 5.0f;
    char name[] = "text";
    char key1[] = "VALUE";
    char key2[] = "value";
    char key3[] = "Other";
    RawEntry entries[3] = {
            {key1, Number, nullptr, &number},
            {key3, String, name, nullptr},
            {key2, String, name, nullptr}, // same key, later, other type: wins and hides the number
    };
    OBL_CHECK(GetNumber(entries, 3, "VALUE") == nullptr);
    OBL_CHECK(GetString(entries, 3, "Value") == name);
    OBL_CHECK(GetNumber(entries, 1, "value") == &number);
    OBL_CHECK(GetString(entries, 3, "other") == name);
    OBL_CHECK(GetNumber(entries, 3, "missing") == nullptr);
    OBL_CHECK(GetNumber(entries, 0, "VALUE") == nullptr);
    OBL_CHECK(obl::ascii::EqualsNoCase("AbC", "aBc"));
    OBL_CHECK(!obl::ascii::EqualsNoCase("ab", "abc"));
}

// Oracle: the shipped files list one `'KEY' value` per line, so a line scan finds the same keys and numbers.
OBL_TEST_CASE("data.pis_matches_line_oracle_on_shipped_files")
{
    const std::filesystem::path root = std::filesystem::path(OBL_GAME_DIR) / "Data";
    if (!std::filesystem::exists(root / "Script")) OBL_SKIP("run/ has no imported game data");
    int files = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        std::string ext = item.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!item.is_regular_file() || ext != ".pis") continue;
        std::ifstream in(item.path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        // Oracle: lines whose first non-blank character is a quote start one entry; the value is the next token.
        struct Line {
            std::string key, value;
        };
        std::vector<Line> lines;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            size_t first = text.find_first_not_of(" \t\r", at);
            if (first < end && text[first] == '\'') {
                size_t close = text.find('\'', first + 1);
                if (close < end) {
                    size_t valueAt = text.find_first_not_of(" \t", close + 1);
                    size_t valueEnd = text[valueAt] == '"' ? text.find('"', valueAt + 1) + 1 : text.find_first_of(" \t\r\n", valueAt);
                    lines.push_back({text.substr(first + 1, close - first - 1), text.substr(valueAt, std::min(valueEnd, end) - valueAt)});
                }
            }
            at = end + 1;
        }

        std::vector<Entry> entries = Parse(text);
        const std::string name = item.path().filename().string();
        OBL_CHECK_EQ(Count(text), static_cast<int>(entries.size()));
        OBL_CHECK_EQ(name + ": " + std::to_string(entries.size()), name + ": " + std::to_string(lines.size()));
        for (size_t i = 0; i < std::min(lines.size(), entries.size()); ++i) {
            OBL_CHECK_EQ(entries[i].key, lines[i].key);
            const std::string& value = lines[i].value;
            if (entries[i].type == Number) {
                double expected = std::strtod(value.c_str(), nullptr);
                OBL_CHECK(std::fabs(entries[i].number - expected) <= 1e-5 * std::max(1.0, std::fabs(expected)));
            } else if (entries[i].type == String) {
                OBL_CHECK_EQ("\"" + entries[i].string + "\"", value);
            }
        }
        ++files;
    }
    OBL_CHECK(files >= 20);
}
