#include "game/ui/gui_script.h"
#include "unit_test.h"

#include <string_view>

using namespace obl::gui_script;

namespace {
Cursor Input(std::string_view text)
{
    return {text.data(), 0, text.size()};
}
Bounds Rectangle(std::string_view text, int width = 800, int height = 600, const Bounds* parent = nullptr)
{
    Cursor cursor = Input(text);
    Bounds bounds;
    OBL_CHECK(ReadBounds(cursor, bounds, width, height, parent) == BoundsResult::Ok);
    OBL_CHECK_EQ(cursor.Peek(), ')');
    return bounds;
}
} // namespace

OBL_TEST_CASE("gui_script.token_delimiters_and_literal_bytes")
{
    for (char delimiter : {',', ')', '(', '{', '\r'}) {
        std::string text = " \t\r\nname \t/\n";
        text += delimiter;
        text += "NEXT)";
        Cursor cursor = Input(text);
        std::string token;
        OBL_REQUIRE(ReadToken(cursor, token));
        OBL_CHECK_EQ(token, "name \t/\n");
        OBL_CHECK_EQ(cursor.at, delimiter == '\r' || delimiter == ',' || delimiter == ')' ? 13u : 12u);
    }
}

OBL_TEST_CASE("gui_script.empty_tokens_and_crlf_skip")
{
    Cursor cursor = Input("  ,  )\r\n{abc)");
    std::string token;
    OBL_REQUIRE(ReadToken(cursor, token));
    OBL_CHECK(token.empty());
    OBL_CHECK_EQ(cursor.at, 3u);
    OBL_REQUIRE(ReadToken(cursor, token));
    OBL_CHECK(token.empty());
    OBL_CHECK_EQ(cursor.at, 6u);
    OBL_REQUIRE(ReadToken(cursor, token));
    OBL_CHECK(token.empty());
    OBL_CHECK_EQ(cursor.Peek(), '{');
    cursor = Input("token\r\n\t  ,NEXT)");
    OBL_REQUIRE(ReadToken(cursor, token));
    OBL_CHECK_EQ(token, "token");
    OBL_CHECK_EQ(cursor.Peek(), 'N');
}

OBL_TEST_CASE("gui_script.integer_byte_arithmetic_and_delimiter_mode")
{
    for (bool consume : {false, true}) {
        Cursor cursor = Input(" \t123 \r\n,\t NEXT)");
        int32_t value;
        OBL_REQUIRE(ReadInt(cursor, value, consume));
        OBL_CHECK_EQ(value, 123);
        OBL_CHECK_EQ(cursor.Peek(), consume ? 'N' : ',');
    }
    for (auto [text, expected] : {std::pair{"-12,", -288}, {"12\t3,", 813}, {"4294967295,", -1}, {"4294967296,", 0}, {"1a,", 59},
                 {"\x80"
                  "1,",
                         -1759},
                 {",", 0}}) {
        Cursor cursor = Input(text);
        int32_t value;
        OBL_REQUIRE(ReadInt(cursor, value, false));
        OBL_CHECK_EQ(value, expected);
        OBL_CHECK_EQ(cursor.Peek(), ',');
    }
}

OBL_TEST_CASE("gui_script.float_conversion_and_space_before_comma")
{
    for (auto [text, expected] : {std::pair{"-1.25e2,\t X)", -125.0f}, {"3.5 , X)", 3.5f}, {"abc,", 0.0f}, {"12\t34,", 12.0f}, {",", 0.0f},
                 {"1e40,", std::numeric_limits<float>::infinity()}}) {
        Cursor cursor = Input(text);
        float value;
        OBL_REQUIRE(ReadFloat(cursor, value));
        OBL_CHECK_EQ(value, expected);
        OBL_CHECK_EQ(cursor.Peek(),
                std::string_view(text).find(" ,") != std::string_view::npos          ? ','
                        : std::string_view(text).find('X') != std::string_view::npos ? 'X'
                                                                                     : '\0');
    }
}

OBL_TEST_CASE("gui_script.bounds_anchors_and_logical_center_shifts")
{
    OBL_CHECK((Rectangle("10,20,110,80)") == Bounds{10, 20, 110, 80}));
    OBL_CHECK((Rectangle("TL10,20,110,80)") == Bounds{10, 20, 110, 80}));
    OBL_CHECK((Rectangle("BR110,80,10,20)") == Bounds{690, 520, 790, 580}));
    OBL_CHECK((Rectangle("BS10,80,10,20)") == Bounds{10, 520, 790, 580}));
    OBL_CHECK((Rectangle("CY10,20,111,81)", 801, 601) == Bounds{350, 270, 451, 331}));
    OBL_CHECK((Rectangle("RS10,20,110,80)") == Bounds{790, 20, 110, 80}));
    OBL_CHECK((Rectangle("C10,0,0,1)") == Bounds{-2147483243, 0, -2147483253, 1}));
    // Space after an anchor is not skipped: x0 is empty, and the other coordinates shift by one.
    Cursor cursor = Input("R 10,20,110,80)");
    Bounds bounds;
    OBL_REQUIRE(ReadBounds(cursor, bounds, 800, 600) == BoundsResult::Ok);
    OBL_CHECK((bounds == Bounds{800, 10, 780, 110}));
    OBL_CHECK_EQ(cursor.Peek(), '8');
}

OBL_TEST_CASE("gui_script.bounds_parent_translation_and_rejection")
{
    Bounds parent{17, 23, 117, 123};
    OBL_CHECK((Rectangle("0,0,100,100)", 800, 600, &parent) == parent));
    OBL_CHECK((Rectangle("20,30,0,0)", 800, 600, &parent) == Bounds{37, 53, 17, 23}));
    for (Bounds degenerate : {Bounds{17, 23, 17, 123}, Bounds{17, 23, 117, 23}})
        OBL_CHECK((Rectangle("0,0,800,600)", 800, 600, &degenerate) == Bounds{0, 0, 800, 600}));
    for (const char* text : {"0,0,101,100)", "0,0,100,101)"}) {
        Cursor cursor = Input(text);
        Bounds bounds;
        OBL_CHECK(ReadBounds(cursor, bounds, 800, 600, &parent) == BoundsResult::OutsideParent);
        OBL_CHECK_EQ(cursor.Peek(), ')');
    }
    // There is no left/top check, even for reversed/signed-byte-generated coordinates.
    OBL_CHECK((Rectangle("-1,0,10,10)", 800, 600, &parent) == Bounds{-12, 23, 27, 33}));
}

OBL_TEST_CASE("gui_script.bounded_truncated_inputs")
{
    for (std::string_view text : {"", " \t\r\n", "unterminated", "12", "BR"}) {
        Cursor cursor = Input(text);
        std::string token;
        OBL_CHECK(!ReadToken(cursor, token));
        OBL_CHECK(cursor.at <= text.size());
        cursor = Input(text);
        Bounds bounds;
        OBL_CHECK(ReadBounds(cursor, bounds, 800, 600) == BoundsResult::Truncated);
        OBL_CHECK(cursor.at <= text.size());
    }
}

OBL_TEST_CASE("gui_script.looks_like_bounds_offline_walk")
{
    for (const char* text : {"0,0,800,600,x)", " \tRB10, 20 ,30,40)", "CY1,2,3,4,", "S0,0,1,1)"}) OBL_CHECK(LooksLikeBounds(text, 0));
    for (const char* text : {"", "0,0,800", "0,0,800,x)", "a,0,1,1)", "0 0,1,1)", "0,0,1,1", "R,0,1,1)"})
        OBL_CHECK(!LooksLikeBounds(text, 0));
    OBL_CHECK(LooksLikeBounds("Tga(0,0,1,1)", 4));
}
