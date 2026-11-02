#pragma once

#include "script_types.h"

#include <cstdint>
#include <cstring>

// The arithmetic of the scenario interpreter (`Add`/`Minus`/`Divide`/`Multiply` of scenario command execution, opcodes 6..9):
// the left value decides the kind. An int left side gives an int result (a float right side is truncated first), any other
// left side gives a float result (the right side converted to float, an int as an unsigned 32-bit number). Pure logic,
// covered by unit tests.
namespace obl::script {

enum class Arith { Add, Minus, Divide, Multiply };

constexpr int32_t kTypeInt = 2, kTypeFloat = 3;

inline float BitsToFloat(int32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline int32_t FloatToBits(float value)
{
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// `truncate(float bits)` applies the shared script float-to-integer conversion.
//
// int path: 32-bit wraparound; Divide is an **unsigned** division and a zero divisor is a hardware fault (so
//   callers must never pass a zero divisor). The result is {int, bits}.
// float path: the left bits are read as a float whatever its kind (a byte, a string pointer ...), the right side is a float
//   when its kind says so and otherwise the unsigned value of its bits; the operation is done in double and rounded to float.
//   For two float operands that is bit-identical to a float operation (53 bits leave no double rounding for
//   + - * /); for an int operand 64 bits are kept, so the two could only differ when the operands are ~30 binary orders
//   of magnitude apart (never in scenario values; the call-drive checks the practical range). Dividing by zero gives inf/NaN.
//   The result is {float, bits}.
template<class Truncate> ScriptValue Arithmetic(Arith op, ScriptValue left, ScriptValue right, Truncate truncate)
{
    if (left.type == kTypeInt) {
        const uint32_t l = static_cast<uint32_t>(left.bits);
        const uint32_t r = static_cast<uint32_t>(right.type == kTypeFloat ? truncate(right.bits) : right.bits);
        uint32_t result = 0;
        switch (op) {
        case Arith::Add: result = l + r; break;
        case Arith::Minus: result = l - r; break;
        case Arith::Divide: result = l / r; break;
        case Arith::Multiply: result = l * r; break;
        }
        return {kTypeInt, static_cast<int32_t>(result)};
    }
    const double l = BitsToFloat(left.bits);
    const double r = right.type == kTypeFloat ? static_cast<double>(BitsToFloat(right.bits))
                                              : static_cast<double>(static_cast<uint32_t>(right.bits));
    double result = 0.0;
    switch (op) {
    case Arith::Add: result = l + r; break;
    case Arith::Minus: result = l - r; break;
    case Arith::Divide: result = l / r; break;
    case Arith::Multiply: result = l * r; break;
    }
    return {kTypeFloat, FloatToBits(static_cast<float>(result))};
}

} // namespace obl::script
