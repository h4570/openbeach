#include "core/text16.h"

#include "core/wide_format.h"

#include <cwchar>

namespace obl::text16 {

// On Windows Char16 has the size of wchar_t, so the C runtime takes the text as it is.
int FormatV(Char16* out, size_t capacity, const Char16* format, va_list args)
{
#ifdef _WIN32
    return std::vswprintf(reinterpret_cast<wchar_t*>(out), capacity, reinterpret_cast<const wchar_t*>(format), args);
#else
    return wide_format::Format(out, capacity, format, args);
#endif
}

int Format(Char16* out, size_t capacity, const Char16* format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = FormatV(out, capacity, format, args);
    va_end(args);
    return written;
}

double ParseDouble(const Char16* text, Char16** end)
{
#ifdef _WIN32
    return std::wcstod(reinterpret_cast<const wchar_t*>(text), reinterpret_cast<wchar_t**>(end));
#else
    const Char16* stop = nullptr;
    const double value = wide_format::ParseDouble(text, &stop);
    if (end) *end = const_cast<Char16*>(stop);
    return value;
#endif
}

long ParseLong(const Char16* text, Char16** end, int base)
{
#ifdef _WIN32
    return std::wcstol(reinterpret_cast<const wchar_t*>(text), reinterpret_cast<wchar_t**>(end), base);
#else
    const Char16* stop = nullptr;
    const long value = wide_format::ParseLong(text, &stop, base);
    if (end) *end = const_cast<Char16*>(stop);
    return value;
#endif
}

} // namespace obl::text16
