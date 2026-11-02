// `.pis` parser edges (src/game/tables/pis.h) beyond pis_test.cpp: truncated windows, comment scanning at the end of the
// buffer, KeyText on garbage, and the Equal field diagnostics.

#include "unit_test.h"

#include "core/ascii_case.h"
#include "importer/src/readers/pis.h"

#include <string>
#include <vector>

using namespace obl::pis;

OBL_TEST_CASE("pis_edges.token_length_of_a_lone_quote_window")
{
    // A window of one byte that is a quote: the token is that byte only (no closing quote can follow).
    OBL_CHECK_EQ(TokenLength("\"abc", 1), 1);
    OBL_CHECK_EQ(TokenLength("\"abc", 0), 1);
    // Unterminated string inside the window: the whole window.
    OBL_CHECK_EQ(TokenLength("\"abc", 4), 4);
    OBL_CHECK_EQ(TokenLength("\"abc\"", 5), 5);
}

OBL_TEST_CASE("pis_edges.skip_to_key_gives_up_in_a_comment_at_the_end_of_the_window")
{
    // `/` after a byte starts a comment that runs to the next CR; without a CR inside the window the scan stops at its end.
    const char text[] = "x//never ends";
    OBL_CHECK_EQ(SkipToKey(text, static_cast<int>(sizeof(text)) - 1), static_cast<int>(sizeof(text)) - 1);
    const char closed[] = "x//c\r'k";
    OBL_CHECK_EQ(SkipToKey(closed, static_cast<int>(sizeof(closed)) - 1), 5);
    OBL_CHECK_EQ(SkipToKey("", 0), 0);
}

OBL_TEST_CASE("pis_edges.key_text_of_garbage_is_empty")
{
    OBL_CHECK(KeyText("'", 1).empty()); // length <= 1: end is 1
    OBL_CHECK(KeyText("''", 2).empty()); // empty key
    OBL_CHECK_EQ(KeyText("'ab' 1", 6), std::string("ab"));
    // No closing quote: the key runs to the end of the window.
    OBL_CHECK_EQ(KeyText("'abc", 4).size(), 2u);
}

OBL_TEST_CASE("pis_edges.value_entry_for_unterminated_and_empty_strings")
{
    Entry open = ValueEntry("\"abc", 4);
    OBL_CHECK_EQ(open.type, static_cast<int32_t>(None)); // needs two quotes in the window to be a string
    Entry empty = ValueEntry("\"\"", 2);
    OBL_CHECK_EQ(empty.type, static_cast<int32_t>(String));
    OBL_CHECK(empty.hasString);
    OBL_CHECK(empty.string.empty());
    Entry number = ValueEntry("-1.5", 4);
    OBL_CHECK_EQ(number.type, static_cast<int32_t>(Number));
    OBL_CHECK(number.hasNumber);
    OBL_CHECK_EQ(number.number, -1.5f);
}

OBL_TEST_CASE("pis_edges.equal_names_the_first_difference")
{
    Entry a;
    a.key = "k";
    a.type = String;
    a.hasString = true;
    a.string = "v";
    Entry b = a;
    std::string diff;
    OBL_CHECK(Equal(a, b, &diff));
    OBL_CHECK(Equal(a, b));
    b.key = "other";
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("key"));
    b = a;
    b.type = Number;
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("type"));
    b = a;
    b.string = "w";
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("string"));
    b = a;
    b.hasString = false;
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("string"));
    b = a;
    b.hasNumber = true;
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("number"));
    a.hasNumber = b.hasNumber = true;
    a.number = 1.0f;
    b.number = 2.0f;
    OBL_CHECK(!Equal(a, b, &diff));
    OBL_CHECK_EQ(diff, std::string("number"));
    OBL_CHECK(!Equal(a, b)); // no diff pointer: still false
}

OBL_TEST_CASE("pis_edges.lookup_skips_null_keys_and_compares_ignoring_case")
{
    float one = 1.0f;
    char text[] = "hi";
    RawEntry entries[3] = {{const_cast<char*>("Speed"), Number, nullptr, &one}, {nullptr, Number, nullptr, &one},
            {const_cast<char*>("NAME"), String, text, nullptr}};
    OBL_CHECK(GetNumber(entries, 3, "speed") == &one);
    OBL_CHECK(GetString(entries, 3, "name") == text);
    OBL_CHECK(GetNumber(entries, 3, "name") == nullptr);
    OBL_CHECK(GetString(entries, 3, "speed") == nullptr);
    OBL_CHECK(GetNumber(entries, 3, "missing") == nullptr);
    OBL_CHECK(GetNumber(entries, 0, "speed") == nullptr);
    OBL_CHECK(obl::ascii::EqualsNoCase("AbC", "aBc"));
    OBL_CHECK(!obl::ascii::EqualsNoCase("abc", "abd"));
    OBL_CHECK(!obl::ascii::EqualsNoCase("abc", "ab"));
}
