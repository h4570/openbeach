#pragma once
#include "core/float_to_int.h"
#include "game/objects/script_program_exec.h"
#include <bit>

// The value conversions every script interpreter shares (object, guest, staff and world scripts).
namespace obl::script_value {
inline int32_t ToInt(int32_t type, uint32_t bits)
{
    if (type == 0) return bits & 255;
    if (type == 2) return std::bit_cast<int32_t>(bits);
    return type == 3 ? FloatToIntWrap(std::bit_cast<float>(bits)) : 0;
}
inline double ToFloat(int32_t type, uint32_t bits)
{
    return script_program::exec::ToFloatWide({type, std::bit_cast<int32_t>(bits)});
}
} // namespace obl::script_value
