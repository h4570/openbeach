#pragma once

namespace obl {

// Records that hold pointers keep the game's 32-bit sizes only on 32-bit builds. Their size pins check `!kPointer32 || ...`,
// so the x64 configure (docs/build/building.md) builds them with natural 8-byte pointers.
inline constexpr bool kPointer32 = sizeof(void*) == 4;

} // namespace obl
