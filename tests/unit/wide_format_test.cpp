// Microsoft-style wide formatting and parsing (src/core/wide_format.h) for the Linux obl-sim. The expectations are what
// the Windows C runtimes print; on Windows every case also runs against the UCRT (`vswprintf`, `wcstod`) and the
// game's msvcrt.dll (`swprintf` without a count).

#include "unit_test.h"

#include "core/wide_format.h"

#include <cstdarg>
#include <string>

#ifdef _WIN32
#include <cwchar>
#include <windows.h>
#endif

using namespace obl;

namespace {

std::u16string Ucrt(const char16_t* format, ...)
{
    char16_t buffer[256];
    va_list args;
    va_start(args, format);
    const int n = wide_format::Format(buffer, std::size(buffer), format, args);
    va_end(args);
    return n < 0 ? u"<overflow>" : std::u16string(buffer);
}

std::u16string Msvcrt(const char16_t* format, ...)
{
    char16_t buffer[256];
    va_list args;
    va_start(args, format);
    wide_format::FormatUnbounded(buffer, format, args);
    va_end(args);
    return buffer;
}

#ifdef _WIN32
std::u16string RealUcrt(const char16_t* format, ...)
{
    wchar_t buffer[256];
    va_list args;
    va_start(args, format);
    std::vswprintf(buffer, std::size(buffer), reinterpret_cast<const wchar_t*>(format), args);
    va_end(args);
    return reinterpret_cast<const char16_t*>(buffer);
}

std::u16string RealMsvcrt(const char16_t* format, ...)
{
    using Vswprintf = int(__cdecl*)(wchar_t*, const wchar_t*, va_list);
    static const auto vswprintf = reinterpret_cast<Vswprintf>(GetProcAddress(LoadLibraryW(L"msvcrt.dll"), "vswprintf"));
    wchar_t buffer[256];
    va_list args;
    va_start(args, format);
    vswprintf(buffer, reinterpret_cast<const wchar_t*>(format), args);
    va_end(args);
    return reinterpret_cast<const char16_t*>(buffer);
}
#define OBL_CHECK_BOTH(expected, format, ...)                                                                                              \
    do {                                                                                                                                   \
        OBL_CHECK(Ucrt(format, __VA_ARGS__) == (expected));                                                                                \
        OBL_CHECK(RealUcrt(format, __VA_ARGS__) == (expected));                                                                            \
    } while (0)
#define OBL_CHECK_MSVCRT(expected, format, ...)                                                                                            \
    do {                                                                                                                                   \
        OBL_CHECK(Msvcrt(format, __VA_ARGS__) == (expected));                                                                              \
        OBL_CHECK(RealMsvcrt(format, __VA_ARGS__) == (expected));                                                                          \
    } while (0)
#else
#define OBL_CHECK_BOTH(expected, format, ...) OBL_CHECK(Ucrt(format, __VA_ARGS__) == (expected))
#define OBL_CHECK_MSVCRT(expected, format, ...) OBL_CHECK(Msvcrt(format, __VA_ARGS__) == (expected))
#endif

} // namespace

OBL_TEST_CASE("wide_format integers and flags")
{
    OBL_CHECK_BOTH(u"42|   42|42   |-0042|+7", u"%d|%5d|%-5d|%05d|%+d", 42, 42, 42, -42, 7);
    OBL_CHECK_BOTH(u"ff FF 0000BEEF 17 4294967295", u"%x %X %08X %o %u", 255, 255, 0xBEEF, 15, -1);
    OBL_CHECK_BOTH(u"%7", u"%%%d", 7);
    OBL_CHECK_BOTH(u"[  12]", u"[%*d]", 4, 12);
    OBL_CHECK_BOTH(u"123456789012", u"%I64d", 123456789012LL);
    OBL_CHECK_MSVCRT(sizeof(void*) == 4 ? u"-5 00001234" : u"-5 0000000000001234", u"%d %p", -5, reinterpret_cast<void*>(0x1234));
}

OBL_TEST_CASE("wide_format text: %s is wide in a wide format")
{
    OBL_CHECK_BOTH(u"wide and narrow", u"%s and %S", u"wide", "narrow");
    OBL_CHECK_BOTH(u"a b c", u"%hs %ls %ws", "a", u"b", u"c");
    OBL_CHECK_BOTH(u"   ab|cd   |", u"%5.2s|%-5s|", u"abcdef", u"cd");
    OBL_CHECK_BOTH(u"Zy", u"%c%C", u'Z', 'y');
    OBL_CHECK_BOTH(u"\xe9", u"%S", "\xe9"); // narrow bytes widen as code units 0-255
    OBL_CHECK_MSVCRT(u"Scenario3/x.pis", u"%s%d/%s", u"Scenario", 3, u"x.pis");
}

OBL_TEST_CASE("wide_format floating point: msvcrt has three exponent digits")
{
    OBL_CHECK_BOTH(u"3.14 1.234568e+04 0.0001 1e+20", u"%.2f %e %g %g", 3.14159, 12345.678, 0.0001, 1e20);
    OBL_CHECK_MSVCRT(u"3.14 1.234568e+004 0.0001 1e+020", u"%.2f %e %g %g", 3.14159, 12345.678, 0.0001, 1e20);
    OBL_CHECK_BOTH(u"  1.50|-2.0", u"%6.2f|%.1f", 1.5, -2.0);
}

OBL_TEST_CASE("wide_format truncates and reports overflow")
{
    char16_t buffer[4];
    char16_t text[] = u"abcdef";
    const auto format = [&](size_t capacity, ...) {
        va_list args;
        va_start(args, capacity);
        const int n = wide_format::Format(buffer, capacity, u"%s", args);
        va_end(args);
        return n;
    };
    OBL_CHECK_EQ(format(std::size(buffer), text), -1);
    OBL_CHECK(std::u16string(buffer) == u"abc");
    text[3] = 0;
    OBL_CHECK_EQ(format(std::size(buffer), text), 3);
    OBL_CHECK(std::u16string(buffer) == u"abc");
}

OBL_TEST_CASE("wide_format narrow msvcrt sprintf")
{
    char buffer[64];
    const auto print = [&](const char* format, ...) {
        va_list args;
        va_start(args, format);
        wide_format::FormatUnboundedNarrow(buffer, format, args);
        va_end(args);
        return std::string(buffer);
    };
    OBL_CHECK_EQ(print("%s=%d %S", "x", 5, u"w"), std::string("x=5 w"));
    OBL_CHECK_EQ(print("%e", 1.0), std::string("1.000000e+000"));
}

OBL_TEST_CASE("wide_format parses numbers like wcstod and wcstol")
{
    const char16_t* end = nullptr;
    const char16_t text[] = u"  -1.5e2xyz";
    OBL_CHECK_EQ(wide_format::ParseDouble(text, &end), -150.0);
    OBL_CHECK_EQ(end - text, 8);
    const char16_t none[] = u"abc";
    OBL_CHECK_EQ(wide_format::ParseDouble(none, &end), 0.0);
    OBL_CHECK(end == none);
    const char16_t hex[] = u" 0x1F";
    OBL_CHECK_EQ(wide_format::ParseLong(hex, &end, 16), 31L);
    OBL_CHECK_EQ(end - hex, 5);
    const char16_t decimal[] = u"12z";
    OBL_CHECK_EQ(wide_format::ParseLong(decimal, &end, 10), 12L);
    OBL_CHECK_EQ(end - decimal, 2);
#ifdef _WIN32
    wchar_t* wideEnd = nullptr;
    OBL_CHECK_EQ(std::wcstod(reinterpret_cast<const wchar_t*>(text), &wideEnd), -150.0);
    OBL_CHECK_EQ(reinterpret_cast<const char16_t*>(wideEnd) - text, 8);
#endif
}
