#include "core/wide_format.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

namespace obl::wide_format {
namespace {

// Collects the output; units past the capacity are counted but dropped.
template<class Unit> struct Sink {
    Unit* out;
    size_t capacity; // units including the terminator; SIZE_MAX = unbounded
    size_t length = 0;

    void Put(char16_t unit)
    {
        if (length + 1 < capacity) out[length] = static_cast<Unit>(unit);
        ++length;
    }
    void Repeat(char16_t unit, int count)
    {
        for (int i = 0; i < count; ++i) Put(unit);
    }
    int Finish()
    {
        if (capacity == 0) return length == 0 ? 0 : -1;
        const bool fits = length + 1 <= capacity;
        out[fits ? length : capacity - 1] = 0;
        return fits ? static_cast<int>(length) : -1;
    }
};

struct Spec {
    bool left = false;
    bool zero = false;
    std::string flags;
    int width = 0;
    int precision = -1;
    enum class Size { Default, Char, Short, Long, LongLong, Narrow, Wide } size = Size::Default;
};

template<class Unit> size_t Length(const Unit* text, int limit)
{
    size_t n = 0;
    while ((limit < 0 || n < static_cast<size_t>(limit)) && text[n]) ++n;
    return n;
}

template<class Out, class Text> void PutText(Sink<Out>& sink, const Spec& spec, const Text* text)
{
    static const char kNull[] = "(null)";
    if (!text) {
        const size_t n = Length(kNull, spec.precision);
        if (!spec.left) sink.Repeat(u' ', spec.width - static_cast<int>(n));
        for (size_t i = 0; i < n; ++i) sink.Put(static_cast<char16_t>(kNull[i]));
        if (spec.left) sink.Repeat(u' ', spec.width - static_cast<int>(n));
        return;
    }
    const size_t n = Length(text, spec.precision);
    if (!spec.left) sink.Repeat(u' ', spec.width - static_cast<int>(n));
    for (size_t i = 0; i < n; ++i) {
        // Byte-transparent: narrow text widens unit by unit, wide text narrows to its low byte.
        const auto unit = static_cast<std::make_unsigned_t<Text>>(text[i]);
        sink.Put(sizeof(Out) == 1 ? static_cast<char16_t>(unit & 0xFF) : static_cast<char16_t>(unit));
    }
    if (spec.left) sink.Repeat(u' ', spec.width - static_cast<int>(n));
}

// Widens msvcrt's exponent to at least three digits ("1e+05" -> "1e+005").
std::string ThreeDigitExponent(std::string text)
{
    const size_t e = text.find_first_of("eE");
    if (e == std::string::npos || e + 1 >= text.size() || (text[e + 1] != '+' && text[e + 1] != '-')) return text;
    const size_t digits = text.size() - (e + 2);
    if (digits < 3) text.insert(e + 2, 3 - digits, '0');
    return text;
}

// Pads a formatted number to the width the way printf does: spaces before, after (`-`), or zeros after the sign/prefix.
std::string Pad(std::string text, const Spec& spec)
{
    if (static_cast<int>(text.size()) >= spec.width) return text;
    const size_t missing = static_cast<size_t>(spec.width) - text.size();
    if (spec.left) return text + std::string(missing, ' ');
    if (spec.zero) {
        size_t at = (!text.empty() && (text[0] == '-' || text[0] == '+' || text[0] == ' ')) ? 1 : 0;
        if (text.size() > at + 1 && text[at] == '0' && (text[at + 1] == 'x' || text[at + 1] == 'X')) at += 2;
        text.insert(at, missing, '0');
        return text;
    }
    return std::string(missing, ' ') + text;
}

template<class Format, class Out> int Run(Sink<Out>& sink, const Format* format, va_list args, Flavor flavor)
{
    constexpr bool kWideFormat = sizeof(Format) == 2;
    while (*format) {
        if (*format != '%') {
            sink.Put(static_cast<char16_t>(static_cast<std::make_unsigned_t<Format>>(*format++)));
            continue;
        }
        ++format;
        if (*format == '%') {
            sink.Put(u'%');
            ++format;
            continue;
        }
        Spec spec;
        for (;; ++format) {
            if (*format == '-')
                spec.left = true;
            else if (*format == '0')
                spec.zero = true;
            else if (*format == '+' || *format == ' ' || *format == '#')
                spec.flags += static_cast<char>(*format);
            else
                break;
        }
        if (*format == '*') {
            spec.width = va_arg(args, int);
            if (spec.width < 0) spec.left = true, spec.width = -spec.width;
            ++format;
        } else {
            while (*format >= '0' && *format <= '9') spec.width = spec.width * 10 + (*format++ - '0');
        }
        if (*format == '.') {
            ++format;
            spec.precision = 0;
            if (*format == '*') {
                spec.precision = va_arg(args, int);
                ++format;
            } else {
                while (*format >= '0' && *format <= '9') spec.precision = spec.precision * 10 + (*format++ - '0');
            }
        }
        if (spec.left) spec.zero = false;
        switch (*format) {
        case 'h':
            ++format;
            spec.size = Spec::Size::Short;
            if (*format == 'h') ++format, spec.size = Spec::Size::Char;
            break;
        case 'l':
            ++format;
            spec.size = Spec::Size::Long;
            if (*format == 'l') ++format, spec.size = Spec::Size::LongLong;
            break;
        case 'w': ++format, spec.size = Spec::Size::Wide; break;
        case 'L': ++format; break; // long double is double in MSVC
        case 'j': ++format, spec.size = Spec::Size::LongLong; break;
        case 'z':
        case 't': ++format; break; // 32-bit
        case 'I':
            if (format[1] == '6' && format[2] == '4')
                format += 3, spec.size = Spec::Size::LongLong;
            else if (format[1] == '3' && format[2] == '2')
                format += 3;
            else
                ++format;
            break;
        default: break;
        }
        const char conversion = static_cast<char>(*format);
        if (!conversion) break;
        ++format;

        std::string narrowSpec = "%" + spec.flags + (spec.zero ? "0" : "");
        if (spec.precision >= 0) narrowSpec += "." + std::to_string(spec.precision);
        char buffer[512];
        switch (conversion) {
        case 'd':
        case 'i': {
            const long long value = spec.size == Spec::Size::LongLong ? va_arg(args, long long)
                    : spec.size == Spec::Size::Short                  ? static_cast<short>(va_arg(args, int))
                    : spec.size == Spec::Size::Char                   ? static_cast<signed char>(va_arg(args, int))
                                                                      : va_arg(args, int);
            std::snprintf(buffer, sizeof(buffer), (narrowSpec + "lld").c_str(), value);
            PutText(sink, Spec{}, Pad(buffer, spec).c_str());
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            const unsigned long long value = spec.size == Spec::Size::LongLong ? va_arg(args, unsigned long long)
                    : spec.size == Spec::Size::Short                           ? static_cast<unsigned short>(va_arg(args, unsigned))
                    : spec.size == Spec::Size::Char                            ? static_cast<unsigned char>(va_arg(args, unsigned))
                                                                               : va_arg(args, unsigned);
            std::snprintf(buffer, sizeof(buffer), (narrowSpec + "ll" + conversion).c_str(), value);
            PutText(sink, Spec{}, Pad(buffer, spec).c_str());
            break;
        }
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            const double value = va_arg(args, double);
            std::snprintf(buffer, sizeof(buffer), (narrowSpec + conversion).c_str(), value);
            std::string text = buffer;
            if (flavor == Flavor::Msvcrt) text = ThreeDigitExponent(text);
            PutText(sink, Spec{}, Pad(text, spec).c_str());
            break;
        }
        case 'p': {
            // msvcrt prints every pointer digit: 8 in a 32-bit process, 16 in a 64-bit one.
            std::snprintf(buffer, sizeof(buffer), "%0*llX", static_cast<int>(sizeof(void*) * 2),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(va_arg(args, void*))));
            PutText(sink, Spec{}, Pad(buffer, spec).c_str());
            break;
        }
        case 'n': *va_arg(args, int*) = static_cast<int>(sink.length); break;
        case 'c':
        case 'C': {
            // The promoted argument is an int either way; a narrow char keeps its low byte.
            const bool wide = spec.size == Spec::Size::Long || spec.size == Spec::Size::Wide ||
                    (spec.size != Spec::Size::Short && ((conversion == 'c') == kWideFormat));
            const int value = va_arg(args, int);
            const char16_t unit[2] = {static_cast<char16_t>(wide ? value & 0xFFFF : value & 0xFF), 0};
            Spec text = spec;
            text.precision = 1;
            if (!unit[0]) {
                if (!spec.left) sink.Repeat(u' ', spec.width - 1);
                sink.Put(0);
                if (spec.left) sink.Repeat(u' ', spec.width - 1);
            } else {
                PutText(sink, text, unit);
            }
            break;
        }
        case 's':
        case 'S': {
            const bool wide = spec.size == Spec::Size::Long || spec.size == Spec::Size::Wide ||
                    (spec.size != Spec::Size::Short && ((conversion == 's') == kWideFormat));
            if (wide)
                PutText(sink, spec, va_arg(args, const char16_t*));
            else
                PutText(sink, spec, va_arg(args, const char*));
            break;
        }
        default: break; // msvcrt stops on an unknown conversion; nothing in the game uses one
        }
    }
    return sink.Finish();
}

template<class Number, class Convert> Number Parse(const char16_t* text, const char16_t** end, const char* allowed, Convert convert)
{
    const char16_t* p = text;
    while (*p == u' ' || (*p >= u'\t' && *p <= u'\r')) ++p;
    std::string narrow;
    for (const char16_t* q = p; *q < 0x80 && *q && std::strchr(allowed, static_cast<char>(*q)); ++q) narrow += static_cast<char>(*q);
    char* stop = nullptr;
    const Number value = convert(narrow.c_str(), &stop);
    const size_t used = static_cast<size_t>(stop - narrow.c_str());
    if (end) *end = used ? p + used : text;
    return value;
}

} // namespace

int Format(char16_t* out, size_t capacity, const char16_t* format, va_list args, Flavor flavor)
{
    Sink<char16_t> sink{out, capacity};
    va_list copy;
    va_copy(copy, args);
    const int result = Run(sink, format, copy, flavor);
    va_end(copy);
    return result;
}

int FormatUnbounded(char16_t* out, const char16_t* format, va_list args)
{
    Sink<char16_t> sink{out, SIZE_MAX};
    va_list copy;
    va_copy(copy, args);
    const int result = Run(sink, format, copy, Flavor::Msvcrt);
    va_end(copy);
    return result;
}

int FormatUnboundedNarrow(char* out, const char* format, va_list args)
{
    Sink<char> sink{out, SIZE_MAX};
    va_list copy;
    va_copy(copy, args);
    const int result = Run(sink, format, copy, Flavor::Msvcrt);
    va_end(copy);
    return result;
}

double ParseDouble(const char16_t* text, const char16_t** end)
{
    return Parse<double>(text, end, "0123456789+-.eE", [](const char* s, char** stop) { return std::strtod(s, stop); });
}

long ParseLong(const char16_t* text, const char16_t** end, int base)
{
    return Parse<long>(text, end, "0123456789+-xXabcdefABCDEF", [base](const char* s, char** stop) { return std::strtol(s, stop, base); });
}

} // namespace obl::wide_format
