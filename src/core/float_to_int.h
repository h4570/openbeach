#pragma once

#include <cstdint>

namespace obl {

// Float to int: a 64-bit truncation of which only the low 32 bits are returned, so a value beyond int wraps instead of
// saturating, and NaN or anything beyond 64 bits gives the "integer indefinite" 0x8000000000000000, i.e. 0. The argument is a double, which
// a product of two floats or of a float and a small integer fits exactly in a double.
inline int FloatToIntWrap(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

} // namespace obl
