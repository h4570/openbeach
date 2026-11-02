// The portable ASCII case-insensitive compare (src/core/ascii_case.h) that replaces the game's `_stricmp` copies.

#include "core/char16.h"
#include "unit_test.h"

#include "core/ascii_case.h"

#include <string>

using obl::ascii::EqualsNoCase;
using obl::ascii::Lower;

OBL_TEST_CASE("ascii: Lower folds only A-Z")
{
    OBL_CHECK(Lower('A') == 'a');
    OBL_CHECK(Lower('Z') == 'z');
    OBL_CHECK(Lower('a') == 'a');
    OBL_CHECK(Lower('@') == '@');
    OBL_CHECK(Lower('[') == '[');
    OBL_CHECK(Lower('\xC4') == '\xC4');
}

OBL_TEST_CASE("ascii: EqualsNoCase ignores ASCII case")
{
    OBL_CHECK(EqualsNoCase("Beer", "BEER"));
    OBL_CHECK(EqualsNoCase("fat_bloke", "Fat_Bloke"));
    OBL_CHECK(EqualsNoCase("", ""));
    OBL_CHECK(EqualsNoCase(std::string("Shaz"), std::string("sHAZ")));
}

OBL_TEST_CASE("ascii: ContainsNoCase finds a needle in any spelling")
{
    OBL_CHECK(obl::ascii::ContainsNoCase("HeadChef", "CHEF"));
    OBL_CHECK(obl::ascii::ContainsNoCase("dj", "DJ"));
    OBL_CHECK(obl::ascii::ContainsNoCase("anything", ""));
    OBL_CHECK(!obl::ascii::ContainsNoCase("BARMAN", "CHEF"));
    OBL_CHECK(!obl::ascii::ContainsNoCase("CH", "CHEF"));
}

OBL_TEST_CASE("ascii: LessUpper sorts every spelling alike with '_' after the letters")
{
    OBL_CHECK(obl::ascii::LessUpper("barman", "BAR_F"));
    OBL_CHECK(obl::ascii::LessUpper("BARMAN", "bar_f"));
    OBL_CHECK(!obl::ascii::LessUpper("bar_f", "BARMAN"));
    OBL_CHECK(obl::ascii::LessUpper("bar", "BARMAN"));
    OBL_CHECK(!obl::ascii::LessUpper("Barman", "bARMAN"));
}

OBL_TEST_CASE("ascii: EqualsNoCase needs equal lengths and equal bytes")
{
    OBL_CHECK(!EqualsNoCase("Beer", "Beers"));
    OBL_CHECK(!EqualsNoCase("Beers", "Beer"));
    OBL_CHECK(!EqualsNoCase("", "x"));
    OBL_CHECK(!EqualsNoCase("Bench", "Benches"));
    OBL_CHECK(!EqualsNoCase("Beer", "Bear"));
    OBL_CHECK(!EqualsNoCase("Gail_funpool", "Gail-funpool")); // only letters fold: '_' and '-' stay different
}

OBL_TEST_CASE("ascii: EqualsNoCase compares an embedded NUL as a byte")
{
    const std::string withNul("ab\0c", 4);
    OBL_CHECK(!EqualsNoCase(withNul, std::string("ab")));
    OBL_CHECK(EqualsNoCase(withNul, std::string("AB\0C", 4)));
}

OBL_TEST_CASE("ascii: EqualsNoCase leaves non-ASCII bytes alone")
{
    OBL_CHECK(!EqualsNoCase("\xC4", "\xE4"));
    OBL_CHECK(EqualsNoCase("\xC4x", "\xC4X"));
}

OBL_TEST_CASE("ascii: Upper preserves the OS byte sort fold")
{
    for (int byte = 0; byte < 256; ++byte) {
        const char c = static_cast<char>(byte);
        OBL_CHECK(obl::ascii::Upper(c) == (c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c));
    }
}

OBL_TEST_CASE("ascii: bounded byte comparisons include NUL and exclude following bytes")
{
    const char a[] = {'A', '\0', 'B', 'x'};
    const char b[] = {'a', '\0', 'b', 'y'};
    OBL_CHECK(EqualsNoCase(std::string_view(a, 3), std::string_view(b, 3)));
    OBL_CHECK(!EqualsNoCase(std::string_view(a, 4), std::string_view(b, 4)));
    OBL_CHECK(EqualsNoCase(std::string_view(a, a), std::string_view(b, b)));
}

OBL_TEST_CASE("ascii: wide Lower folds only ASCII units")
{
    for (unsigned int unit = 0; unit <= 0xffff; ++unit) {
        const obl::Char16 c = static_cast<obl::Char16>(unit);
        OBL_CHECK(Lower(c) == (c >= u'A' && c <= u'Z' ? static_cast<obl::Char16>(c - u'A' + u'a') : c));
    }
    OBL_CHECK(Lower(static_cast<obl::Char16>(0x1ffff)) == static_cast<obl::Char16>(0x1ffff));
}

OBL_TEST_CASE("ascii: wide comparisons preserve suffix equality and non-ASCII units")
{
    OBL_CHECK(EqualsNoCase(u".BoT", u".bot"));
    OBL_CHECK(EqualsNoCase(u".ChAoS", u".chaos"));
    OBL_CHECK(EqualsNoCase(u"", u""));
    OBL_CHECK(!EqualsNoCase(u".bots", u".bot"));
    OBL_CHECK(!EqualsNoCase(u".bot", u".bots"));
    OBL_CHECK(!EqualsNoCase(u".bat", u".bot"));
    OBL_CHECK(!EqualsNoCase(u"\u00c4", u"\u00e4"));
    OBL_CHECK(EqualsNoCase(u"\u00c4X", u"\u00c4x"));
    OBL_CHECK(EqualsNoCase(obl::Text16View(u"A\0B", 3), obl::Text16View(u"a\0b", 3)));
    OBL_CHECK(!EqualsNoCase(obl::Text16View(u"A\0B", 3), obl::Text16View(u"a\0c", 3)));
}
