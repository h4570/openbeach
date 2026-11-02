#pragma once

// Force-included into every source of the Linux obl-sim (src/host_linux/CMakeLists.txt), never part of the Win32 build.
// Each entry stands in for the MSVC/msvcrt behavior the shared sources are written against
// (docs/architecture/linux-host.md, "Toolchain and ABI").

#include <stddef.h>
#include <string.h>
#include <strings.h>

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _ReturnAddress() __builtin_return_address(0)

// Microsoft _O_* values for source compatibility; native file I/O uses core/os.h.
#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#define _O_CREAT 0x0100
#define _O_TRUNC 0x0200
#define _O_EXCL 0x0400
#define _O_TEXT 0x4000
#define _O_BINARY 0x8000

// msvcrt's in-place ASCII upper-casing ("C" locale).
static inline char* _strupr(char* text)
{
    for (char* p = text; *p; ++p)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    return text;
}

#if defined(__cplusplus)
#include <cstdio>
#include <cstring>

// msvcrt's bounded copies; the game code passes buffers that fit, so they behave like the checked variants' success path
// and truncate instead of invoking the invalid-parameter handler.
inline int strcpy_s(char* destination, size_t size, const char* source)
{
    if (!destination || size == 0) return 22; // EINVAL
    std::snprintf(destination, size, "%s", source ? source : "");
    return 0;
}
template<size_t N> int strcpy_s(char (&destination)[N], const char* source)
{
    return strcpy_s(destination, N, source);
}
#endif
