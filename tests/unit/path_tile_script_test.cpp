
#include "unit_test.h"

#include "game/map/path_tile_script.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace obl::path_tile_script;

OBL_TEST_CASE("path_tile_script.skip_comments")
{
    OBL_CHECK_EQ(SkipComments("12 // x\n3"), 0); // no comment at the start
    OBL_CHECK_EQ(SkipComments(""), 0);
    OBL_CHECK_EQ(SkipComments("//\n7"), 3); // comment line including its LF
    OBL_CHECK_EQ(SkipComments("// abc\n7"), 7);
    OBL_CHECK_EQ(SkipComments("// abc\r\n  \t7"), 11); // CR stays in the line, whitespace after the LF is eaten
    OBL_CHECK_EQ(SkipComments("//a\n//b\n\n// c\n 9"), 15);
    OBL_CHECK_EQ(SkipComments("/\n5"), 2); // a single `/` starts a comment, LF right behind it
    OBL_CHECK_EQ(SkipComments("/x y\n5"), 5);
    OBL_CHECK_EQ(SkipComments("//\n/\n//\nz"), 8);

    std::string many;
    for (int i = 0; i < 50000; ++i) many += "//\n";
    many += "1";
    OBL_CHECK_EQ(SkipComments(many.c_str()), 150000);
}

OBL_TEST_CASE("path_tile_script.read_token")
{
    std::string token;
    OBL_CHECK_EQ(ReadToken("12 34", token), 2);
    OBL_CHECK_EQ(token, "12");
    OBL_CHECK_EQ(ReadToken("  \t\r\n-1 x", token), 7); // leading whitespace counts
    OBL_CHECK_EQ(token, "-1");
    OBL_CHECK_EQ(ReadToken(";x", token), 1); // a lone `;` is a token
    OBL_CHECK_EQ(token, ";");
    OBL_CHECK_EQ(ReadToken(" ; ", token), 2);
    OBL_CHECK_EQ(token, ";");
    OBL_CHECK_EQ(ReadToken("12;3 ", token), 2); // `;` ends a token and stays unread
    OBL_CHECK_EQ(token, "12");
    OBL_CHECK_EQ(ReadToken("a;", token), 1);
    OBL_CHECK_EQ(token, "a");
    OBL_CHECK_EQ(ReadToken("// c\n 5 6", token), 7); // comments before the token
    OBL_CHECK_EQ(token, "5");
    OBL_CHECK_EQ(ReadToken("  // c\n// d\n;", token), 13); // ... also before `;`
    OBL_CHECK_EQ(token, ";");
    OBL_CHECK_EQ(ReadToken("5// c\n", token), 3); // inside a token `/` is an ordinary byte, a space ends it
    OBL_CHECK_EQ(token, "5//");
    OBL_CHECK_EQ(ReadToken("ab\n", token), 2);
    OBL_CHECK_EQ(token, "ab");
    OBL_CHECK_EQ(ReadToken("\x80\xff ", token), 2); // bytes >= 0x80 are ordinary token bytes
    OBL_CHECK_EQ(token, std::string("\x80\xff"));
}

// Oracle: strip comments (`/` to LF) and split on whitespace, with `;` as a token of its own.
static std::vector<std::string> SplitTokens(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&] {
        if (!current.empty()) tokens.push_back(current);
        current.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '/' && current.empty()) {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (c == ';') {
            flush();
            tokens.push_back(";");
        } else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return tokens;
}

OBL_TEST_CASE("data.path_tile_script_matches_splitter_on_pathtiles")
{
    const std::filesystem::path path = std::filesystem::path(OBL_GAME_DIR) / "Data" / "pathtiles.ble";
    if (!std::filesystem::exists(path)) OBL_SKIP("run/ has no imported game data");
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char& c : text) c = static_cast<char>(~c); // the file encoding: every byte inverted
    std::vector<std::string> expected = SplitTokens(text);
    text += '.';
    std::vector<std::string> actual;
    std::string token;
    const char* begin = text.c_str();
    const char* end = begin + text.size() - 1; // the terminator
    for (const char* at = begin;;) {
        while (IsSpace(*at)) ++at;
        if (at >= end) break;
        at += ReadToken(at, token);
        actual.push_back(token);
    }
    OBL_CHECK_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size() && i < expected.size(); ++i) OBL_CHECK_EQ(actual[i], expected[i]);

    // 256 masks of nine numbers each.
    OBL_CHECK_EQ(actual.size(), size_t{256 * 9});
    int masks[256] = {};
    for (size_t record = 0; record < 256 && record * 9 + 8 < actual.size(); ++record) {
        int mask = 0;
        for (int bit = 0; bit < 8; ++bit) mask += std::atoi(actual[record * 9 + bit].c_str()) << bit;
        OBL_CHECK(mask >= 0 && mask < 256);
        if (mask >= 0 && mask < 256) ++masks[mask];
    }
    for (int count : masks) OBL_CHECK_EQ(count, 1);
}
