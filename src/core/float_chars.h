#pragma once

// std::from_chars for float and double. Apple's libc++ (Xcode 16, and libc++ before 20 everywhere) deletes these
// overloads, so the macOS build (docs/build/releases.md) parses with strtof/strtod instead: both round correctly, and no
// code of ours changes the C locale, so a valid number gives the same bits as std::from_chars. The fallback refuses what
// from_chars refuses and strto* would take: leading white space, a '+' and hexadecimal digits after "0x".

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>

namespace obl {

namespace float_chars_detail {
template<typename T, typename Strto> std::from_chars_result Parse(const char* first, const char* last, T& value, Strto parse)
{
    if (first == last || *first == '+' || *first == ' ' || (*first >= '\t' && *first <= '\r')) return {first, std::errc::invalid_argument};
    const std::string text(first, last); // strto* needs a terminator
    const char* digits = text.c_str() + (text[0] == '-');
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        // from_chars reads the "0" and stops at the 'x'.
        value = text[0] == '-' ? -T(0) : T(0);
        return {first + (digits - text.c_str()) + 1, {}};
    }
    char* end = nullptr;
    errno = 0;
    const T parsed = parse(text.c_str(), &end);
    if (end == text.c_str()) return {first, std::errc::invalid_argument};
    const char* stop = first + (end - text.c_str());
    // strto* also flags a subnormal result; from_chars refuses only a number that rounds to zero or infinity.
    if (errno == ERANGE && (parsed == T(0) || parsed - parsed != T(0))) return {stop, std::errc::result_out_of_range};
    value = parsed;
    return {stop, {}};
}
} // namespace float_chars_detail

// The fallback, compiled everywhere so the unit tests compare it with std::from_chars (float_chars_test.cpp).
inline std::from_chars_result StrtoFromChars(const char* first, const char* last, float& value)
{
    return float_chars_detail::Parse(first, last, value, [](const char* s, char** e) { return std::strtof(s, e); });
}

inline std::from_chars_result StrtoFromChars(const char* first, const char* last, double& value)
{
    return float_chars_detail::Parse(first, last, value, [](const char* s, char** e) { return std::strtod(s, e); });
}

template<typename T> std::from_chars_result FloatFromChars(const char* first, const char* last, T& value)
{
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 200000
    return StrtoFromChars(first, last, value);
#else
    return std::from_chars(first, last, value);
#endif
}

} // namespace obl
