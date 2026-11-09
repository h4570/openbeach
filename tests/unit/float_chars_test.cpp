// FloatFromChars (src/core/float_chars.h): the strtof/strtod fallback of the macOS build must parse like std::from_chars.

#include "unit_test.h"

#include "core/float_chars.h"

#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace {

template<typename T> void CheckLikeFromChars(const char* text)
{
    const char* last = text + std::strlen(text);
    T expected = T(7), actual = T(7);
    const auto want = std::from_chars(text, last, expected);
    const auto got = obl::StrtoFromChars(text, last, actual);
    OBL_CHECK_EQ(static_cast<int>(got.ec), static_cast<int>(want.ec));
    OBL_CHECK_EQ(got.ptr - text, want.ptr - text);
    if (want.ec == std::errc{} && expected == expected) // NaN payloads aside
        OBL_CHECK(std::memcmp(&expected, &actual, sizeof(T)) == 0);
}

const char* const kInputs[] = {
        "0",
        "-0",
        "1",
        "-1.5",
        "0.1",
        "3.14159",
        "1e10",
        "1E-5",
        "123456789",
        "0.30000001",
        "16777217",
        "2.5e-3x",
        "1.",
        ".5",
        "inf",
        "-inf",
        "infinity",
        "nan",
        "1e",
        "1e+",
        "12abc",
        "0x10",
        "-0x1p3",
        "+1",
        " 1",
        "\t2",
        "",
        "-",
        "abc",
        "4.9406564584124654e-324",
        "3.4028235e38",
        "1.17549435e-38",
        "0.000001",
        "1234.5678e-2",
};

} // namespace

OBL_TEST_CASE("float_chars: the strto fallback parses floats like std::from_chars")
{
    for (const char* text : kInputs) CheckLikeFromChars<float>(text);
}

OBL_TEST_CASE("float_chars: the strto fallback parses doubles like std::from_chars")
{
    for (const char* text : kInputs) CheckLikeFromChars<double>(text);
}

OBL_TEST_CASE("float_chars: shortest round trip of every to_chars output")
{
    // The game formats floats with std::to_chars and parses them back (command.cpp): the fallback must restore the bits.
    for (uint32_t bits = 0x3f800000u; bits < 0x3f800000u + 4096; bits += 7) {
        const float value = std::bit_cast<float>(bits);
        char buffer[32];
        const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
        float parsed = 0;
        OBL_CHECK(obl::StrtoFromChars(buffer, end, parsed).ec == std::errc{});
        OBL_CHECK_EQ(std::bit_cast<uint32_t>(parsed), bits);
    }
}
