// Injected with -include by tools/windows_syntax_linux.sh only (never part of the Win32 build).
// Fills the gaps between the MSVC/Windows SDK headers the game is built with and the
// MinGW-w64 headers clang uses on Linux; each entry names the MSVC behavior it stands in for.
#pragma once

#ifdef __cplusplus
#include <setjmp.h>

// MSVC's x86 _setjmp takes only the buffer; MinGW declares _setjmp(jmp_buf, void* frame).
inline int _setjmp(jmp_buf buffer)
{
    return _setjmp(buffer, nullptr);
}
#endif
