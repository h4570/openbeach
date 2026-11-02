#include "core/float_to_int.h"
#include "game/objects/script_value_convert.h"
#include "unit_test.h"

#include <cmath>
#include <limits>

OBL_TEST_CASE("FloatToIntWrap truncates toward zero like a chopping conversion")
{
    OBL_CHECK_EQ(obl::FloatToIntWrap(0.0), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(1.9), 1);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-1.9), -1);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-0.5), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(2147483647.0), 2147483647);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-2147483648.0), -2147483647 - 1);
}

OBL_TEST_CASE("FloatToIntWrap wraps beyond int instead of saturating")
{
    OBL_CHECK_EQ(obl::FloatToIntWrap(2147483648.0), -2147483647 - 1);
    OBL_CHECK_EQ(obl::FloatToIntWrap(4294967296.0), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(4294967297.0), 1);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-4294967297.0), -1);
}

OBL_TEST_CASE("FloatToIntWrap gives integer-indefinite 0 for NaN and 64-bit overflow")
{
    OBL_CHECK_EQ(obl::FloatToIntWrap(std::nan("")), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(std::numeric_limits<double>::infinity()), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-std::numeric_limits<double>::infinity()), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(9223372036854775808.0), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(-9223372036854775808.0), 0);
    OBL_CHECK_EQ(obl::FloatToIntWrap(1e30), 0);
}

OBL_TEST_CASE("Script float to int wraps the low 32 bits and handles nonfinite values")
{
    OBL_CHECK_EQ(obl::script_value::ToInt(3, 0x4f800000), 0);
    OBL_CHECK_EQ(obl::script_value::ToInt(3, 0x4f000000), INT32_MIN);
    OBL_CHECK_EQ(obl::script_value::ToInt(3, 0x7fc00000), 0);
    OBL_CHECK_EQ(obl::script_value::ToInt(0, 0xffffffff), 255);
    OBL_CHECK_EQ(obl::script_value::ToFloat(2, 0xffffffff), 4294967295.0);
}
