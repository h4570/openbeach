#pragma once

// Force-included into every unit-test source on Linux (tests/unit/linux/CMakeLists.txt). The sources under test spell
// the 32-bit Windows calling conventions the way the game's functions use them; GCC and Clang know them as attributes.

#if !defined(_MSC_VER)
#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
#ifndef __fastcall
#define __fastcall __attribute__((fastcall))
#endif
#ifndef __stdcall
#define __stdcall __attribute__((stdcall))
#endif
// A keyword in Clang, unrecognized by GCC (the object virtuals, src/game/objects/object_base.h).
#if !defined(__clang__) && !defined(__thiscall)
#define __thiscall __attribute__((thiscall))
#endif
#endif
