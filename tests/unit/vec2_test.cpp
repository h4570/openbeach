
#include "unit_test.h"

#include "core/vec2.h"

#include <cmath>

using namespace obl::vec2;

OBL_TEST_CASE("vec2.add_and_multiply")
{
    Vec2 s = Add({1.5f, -2.0f}, {0.25f, 4.0f});
    OBL_CHECK_EQ(s.x, 1.75f);
    OBL_CHECK_EQ(s.y, 2.0f);
    Vec2 m = Multiply({3.0f, -1.5f}, 2.0f);
    OBL_CHECK_EQ(m.x, 6.0f);
    OBL_CHECK_EQ(m.y, -3.0f);
}

OBL_TEST_CASE("vec2.divide_uses_reciprocal_estimate")
{
    // 0x7f000000 - bits(1.0f) = 0x3f800000 = 1.0f, so the estimate is exact for 1; refined = (2 - 1) * 1.
    OBL_CHECK_EQ(ReciprocalEstimate(1.0f), 1.0f);
    // The estimate is rough but close for typical divisors (the game divides by speeds and frame times).
    for (float d : {0.5f, 0.8f, 1.5f, 3.0f, 10.0f}) {
        float r = ReciprocalEstimate(d);
        OBL_CHECK(std::fabs(r * d - 1.0f) < 0.1f);
    }
    Vec2 q = Divide({2.0f, 4.0f}, 2.0f);
    float r = ReciprocalEstimate(2.0f);
    OBL_CHECK_EQ(q.x, r * 2.0f);
    OBL_CHECK_EQ(q.y, r * 4.0f);
}
