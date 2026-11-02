// The game's narrow-string code page and the UTF conversions (src/core/code_page.h).

#include "unit_test.h"

#include "core/code_page.h"

#include <string>

using namespace obl;

namespace {

// Non-ASCII units are spelled as numbers: the sources are compiled without a fixed source character set.
constexpr char16_t kZDot = 0x017c; // Polish z with dot above, not in Windows-1252
constexpr char16_t kEAcute = 0x00e9;
constexpr char16_t kEuro = 0x20ac;
constexpr char16_t kReplacement = 0xfffd;

} // namespace

OBL_TEST_CASE("code page: every byte round-trips through UTF-16")
{
    for (int byte = 0; byte < 256; ++byte) {
        bool mapped = false;
        const char16_t unit = code_page::WidenByte(static_cast<unsigned char>(byte));
        OBL_CHECK_EQ(static_cast<unsigned char>(code_page::NarrowUnit(unit, &mapped)), static_cast<unsigned char>(byte));
        OBL_CHECK(mapped);
    }
}

OBL_TEST_CASE("code page: Windows-1252 values")
{
    OBL_CHECK_EQ(static_cast<int>(code_page::WidenByte(0x80)), 0x20ac); // euro sign
    OBL_CHECK_EQ(static_cast<int>(code_page::WidenByte(0x9f)), 0x0178);
    OBL_CHECK_EQ(static_cast<int>(code_page::WidenByte(0x81)), 0x0081); // unassigned: C1 code point
    OBL_CHECK_EQ(static_cast<int>(code_page::WidenByte(0xe9)), 0x00e9);
    bool mapped = true;
    OBL_CHECK_EQ(code_page::NarrowUnit(kZDot, &mapped), '?');
    OBL_CHECK(!mapped);
    OBL_CHECK(code_page::Narrow(std::u16string(u"Pla") + kZDot + u"a") == "Pla?a");
    OBL_CHECK(code_page::Widen("caf\xe9") == std::u16string(u"caf") + kEAcute);
}

OBL_TEST_CASE("code page: buffer forms terminate and report truncation")
{
    char16_t wide[8];
    OBL_CHECK_EQ(code_page::Widen("abc", wide, 8), size_t{4});
    OBL_CHECK(std::u16string(wide) == u"abc");
    OBL_CHECK_EQ(code_page::Widen("abcdef", wide, 4), size_t{0});
    OBL_CHECK(std::u16string(wide) == u"abc");
    char narrow[8];
    OBL_CHECK_EQ(code_page::Narrow(u"xyz", narrow, 8), size_t{4});
    OBL_CHECK(std::string(narrow) == "xyz");
    OBL_CHECK_EQ(code_page::Narrow(u"xyzw", narrow, 2), size_t{0});
    OBL_CHECK(std::string(narrow) == "x");
}

OBL_TEST_CASE("code page: UTF-8 and UTF-16")
{
    std::u16string text = std::u16string(u"Pla") + kZDot + u"a " + kEuro + u" ";
    text += static_cast<char16_t>(0xd83c); // U+1F334 as a surrogate pair
    text += static_cast<char16_t>(0xdf34);
    const std::string utf8 = code_page::Utf16ToUtf8(text);
    OBL_CHECK(utf8 ==
            "Pla\xc5\xbc"
            "a \xe2\x82\xac \xf0\x9f\x8c\xb4");
    OBL_CHECK(code_page::Utf8ToUtf16(utf8) == text);
    OBL_CHECK(code_page::Utf8ToUtf16("a\xff"
                                     "b") == std::u16string(u"a") + kReplacement + u"b"); // invalid byte
    OBL_CHECK(code_page::Utf8ToUtf16("\xc0\xaf") == std::u16string(2, kReplacement)); // overlong
    OBL_CHECK(code_page::Utf16ToUtf8(std::u16string(1, char16_t(0xd800))) == "\xef\xbf\xbd"); // lone surrogate
}
