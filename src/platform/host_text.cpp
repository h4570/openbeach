#include "core/char16.h"
#include "game/game/host_text.h"

#include "platform/runtime.h"

#include <cstdarg>

namespace obl::ui::host_text {
int Print(Char16* out, const Char16* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = platform::PrintTextV(out, format, args);
    va_end(args);
    return result;
}
int PrintNarrow(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = platform::PrintNarrowV(out, format, args);
    va_end(args);
    return result;
}
double ParseNumber(const Char16* text, Char16** end)
{
    return platform::ParseNumber(text, end);
}
} // namespace obl::ui::host_text

namespace obl::ui::host {
void RequestQuit(int code)
{
    platform::RequestQuit(code);
}
} // namespace obl::ui::host
