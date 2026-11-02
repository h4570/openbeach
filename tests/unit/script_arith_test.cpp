// Scenario script arithmetic (src/game/objects/script_arith.h): the kind of the left value decides int or float math.

#include "unit_test.h"

#include "game/objects/script_arith.h"

#include <cmath>
#include <limits>

using namespace obl::script;
using obl::script::ScriptValue;

namespace {

int32_t Trunc(int32_t floatBits)
{
    return static_cast<int32_t>(BitsToFloat(floatBits));
}

ScriptValue Int(int32_t value)
{
    return {kTypeInt, value};
}
ScriptValue Float(float value)
{
    return {kTypeFloat, FloatToBits(value)};
}

ScriptValue Run(Arith op, ScriptValue left, ScriptValue right)
{
    return Arithmetic(op, left, right, Trunc);
}

} // namespace

OBL_TEST_CASE("script_arith.int_left_wraps_and_stays_int")
{
    OBL_CHECK_EQ(Run(Arith::Add, Int(7), Int(5)).type, kTypeInt);
    OBL_CHECK_EQ(Run(Arith::Add, Int(7), Int(5)).bits, 12);
    OBL_CHECK_EQ(Run(Arith::Add, Int(0x7fffffff), Int(1)).bits, static_cast<int32_t>(0x80000000u));
    OBL_CHECK_EQ(Run(Arith::Minus, Int(3), Int(5)).bits, -2);
    OBL_CHECK_EQ(Run(Arith::Multiply, Int(0x10000), Int(0x10000)).bits, 0);
    OBL_CHECK_EQ(Run(Arith::Multiply, Int(-3), Int(4)).bits, -12);
}

OBL_TEST_CASE("script_arith.int_divide_is_unsigned")
{
    OBL_CHECK_EQ(Run(Arith::Divide, Int(10), Int(3)).bits, 3);
    // -1 is 0xffffffff: unsigned division, not -1 / 2 == 0.
    OBL_CHECK_EQ(Run(Arith::Divide, Int(-1), Int(2)).bits, 0x7fffffff);
    OBL_CHECK_EQ(Run(Arith::Divide, Int(5), Int(-1)).bits, 0);
    OBL_CHECK_EQ(Run(Arith::Divide, Int(5), Int(-1)).type, kTypeInt);
}

OBL_TEST_CASE("script_arith.int_left_truncates_a_float_right_side")
{
    OBL_CHECK_EQ(Run(Arith::Add, Int(10), Float(2.9f)).bits, 12);
    OBL_CHECK_EQ(Run(Arith::Minus, Int(10), Float(-2.9f)).bits, 12);
    OBL_CHECK_EQ(Run(Arith::Multiply, Int(10), Float(0.5f)).bits, 0);
    // Other kinds contribute their raw bits (a byte, a string pointer ...), whatever they are.
    OBL_CHECK_EQ(Run(Arith::Add, Int(1), ScriptValue{0, 0x1ff}).bits, 0x200);
    OBL_CHECK_EQ(Run(Arith::Add, Int(1), ScriptValue{1, 100}).bits, 101);
}

OBL_TEST_CASE("script_arith.float_left_gives_float")
{
    ScriptValue sum = Run(Arith::Add, Float(1.5f), Int(2));
    OBL_CHECK_EQ(sum.type, kTypeFloat);
    OBL_CHECK_EQ(BitsToFloat(sum.bits), 3.5f);
    OBL_CHECK_EQ(BitsToFloat(Run(Arith::Minus, Float(1.5f), Float(0.25f)).bits), 1.25f);
    OBL_CHECK_EQ(BitsToFloat(Run(Arith::Multiply, Float(1.5f), Int(4)).bits), 6.0f);
    OBL_CHECK_EQ(BitsToFloat(Run(Arith::Divide, Float(1.0f), Float(4.0f)).bits), 0.25f);
    // The left side counts as a float whatever its kind: a byte result keeps its bits.
    ScriptValue fromByte = Run(Arith::Add, ScriptValue{0, FloatToBits(2.0f)}, Float(1.0f));
    OBL_CHECK_EQ(fromByte.type, kTypeFloat);
    OBL_CHECK_EQ(BitsToFloat(fromByte.bits), 3.0f);
}

OBL_TEST_CASE("script_arith.float_path_reads_an_int_right_side_as_unsigned")
{
    // -1 is 4294967295 as an unsigned number, which rounds to 2^32 as a float.
    OBL_CHECK_EQ(BitsToFloat(Run(Arith::Add, Float(0.0f), Int(-1)).bits), 4294967296.0f);
    OBL_CHECK_EQ(BitsToFloat(Run(Arith::Multiply, Float(2.0f), Int(-2)).bits), 8589934592.0f);
}

OBL_TEST_CASE("script_arith.float_division_by_zero_is_inf_or_nan")
{
    OBL_CHECK(std::isinf(BitsToFloat(Run(Arith::Divide, Float(1.0f), Int(0)).bits)));
    OBL_CHECK(std::isinf(BitsToFloat(Run(Arith::Divide, Float(-1.0f), Float(0.0f)).bits)));
    OBL_CHECK(std::isnan(BitsToFloat(Run(Arith::Divide, Float(0.0f), Float(0.0f)).bits)));
}

OBL_TEST_CASE("script_arith.float_ops_equal_single_precision_ops")
{
    // For float operands the double computation rounded to float is the correctly rounded float operation.
    uint32_t seed = 12345;
    auto next = [&seed]() { return seed = seed * 1664525u + 1013904223u; };
    for (int i = 0; i < 20000; ++i) {
        float a = BitsToFloat(static_cast<int32_t>((next() & 0x7fffffffu) % 0x7f000000u));
        float b = BitsToFloat(static_cast<int32_t>((next() & 0x7fffffffu) % 0x7f000000u));
        if ((i & 1) != 0) b = -b;
        OBL_CHECK_EQ(Run(Arith::Add, Float(a), Float(b)).bits, FloatToBits(a + b));
        OBL_CHECK_EQ(Run(Arith::Minus, Float(a), Float(b)).bits, FloatToBits(a - b));
        OBL_CHECK_EQ(Run(Arith::Multiply, Float(a), Float(b)).bits, FloatToBits(a * b));
        if (b != 0.0f) OBL_CHECK_EQ(Run(Arith::Divide, Float(a), Float(b)).bits, FloatToBits(a / b));
    }
}
