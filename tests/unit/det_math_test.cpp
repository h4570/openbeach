#include "core/det_math.h"
#include "game/objects/fast_sqrt_table.h"
#include "unit_test.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace obl::det_math;

namespace {

uint64_t Bits(double x)
{
    return std::bit_cast<uint64_t>(x);
}

} // namespace

// The pinned values were recorded on Windows (MSVC, extended precision) and must match bit for bit in every Linux build (extended precision -O0 and SSE2
// -O2): a change here changes replay hashes (docs/testing/determinism.md).
OBL_TEST_CASE("deterministic acos matches pinned bits at the branch boundaries")
{
    OBL_CHECK_EQ(Bits(Acos(1.0)), 0x0000000000000000ull);
    OBL_CHECK_EQ(Bits(Acos(-1.0)), 0x400921fb54442d18ull);
    OBL_CHECK_EQ(Bits(Acos(0.0)), 0x3ff921fb54442d18ull);
    OBL_CHECK_EQ(Bits(Acos(-0.0)), 0x3ff921fb54442d18ull);
    OBL_CHECK_EQ(Bits(Acos(1e-20)), 0x3ff921fb54442d18ull);
    OBL_CHECK_EQ(Bits(Acos(0.5)), 0x3ff0c152382d7366ull);
    OBL_CHECK_EQ(Bits(Acos(-0.5)), 0x4000c152382d7366ull);
    OBL_CHECK_EQ(Bits(Acos(0.49999999999999994)), 0x3ff0c152382d7366ull);
    OBL_CHECK_EQ(Bits(Acos(0.75)), 0x3fe720a392c1d955ull);
    OBL_CHECK_EQ(Bits(Acos(-0.75)), 0x400359d26f93b6c3ull);
    OBL_CHECK_EQ(Bits(Acos(0.99999994039535522)), 0x3f36a09e6861f3abull);
    // The input where the MSVC and glibc CRTs disagree (math-probe.md); this is the correctly rounded value.
    OBL_CHECK_EQ(Bits(Acos(-0.998291015625)), 0x4008aa3b4e0e5a07ull);
    OBL_CHECK(std::isnan(Acos(1.0000001)));
    OBL_CHECK(std::isnan(Acos(std::nan(""))));
}

OBL_TEST_CASE("deterministic acos hash over every 4096th float in [-1, 1]")
{
    uint32_t hash = 2166136261u;
    for (uint32_t sign = 0; sign < 2; ++sign)
        for (uint32_t bits = 0; bits <= 0x3f800000u; bits += 0x1000) {
            const uint64_t u = Bits(Acos(std::bit_cast<float>(bits | (sign << 31))));
            for (int k = 0; k < 8; ++k) hash = (hash ^ static_cast<uint8_t>(u >> (8 * k))) * 16777619u;
        }
    OBL_CHECK_EQ(hash, 0x0945a804u);
}

OBL_TEST_CASE("deterministic sine matches pinned bits")
{
    OBL_CHECK_EQ(Bits(Sin(0.0)), 0x0000000000000000ull);
    OBL_CHECK_EQ(Bits(Sin(0.17289667768636718)), 0x3fc6054b3c5c282aull); // CRTs disagree here (math-probe.md)
    OBL_CHECK_EQ(Bits(Sin(0.78539816339744828)), 0x3fe6a09e667f3bccull);
    OBL_CHECK_EQ(Bits(Sin(1.0)), 0x3feaed548f090ceeull);
    OBL_CHECK_EQ(Bits(Sin(2.0)), 0x3fed18f6ead1b446ull);
    OBL_CHECK_EQ(Bits(Sin(3.1415926535897931)), 0x3ca1a62633145c07ull);
    OBL_CHECK_EQ(Bits(Sin(4.5)), 0xbfef47ed3dc74080ull);
    OBL_CHECK_EQ(Bits(Sin(6.2831853071795862)), 0xbcb1a62633145c07ull);
    OBL_CHECK_EQ(Bits(Sin(1000.0)), 0x3fea75cc150a206bull);
    OBL_CHECK(std::isnan(Sin(kSinMaxArgument)));
}

OBL_TEST_CASE("deterministic sine table hash")
{
    // Equal to the hardware sine table of the x86 build (all 11520 floats, checked 2026-10-10).
    std::vector<float> table(kSineTableSize);
    BuildSineTable(table.data());
    uint32_t hash = 2166136261u;
    for (float f : table) {
        const uint32_t u = std::bit_cast<uint32_t>(f);
        for (int k = 0; k < 4; ++k) hash = (hash ^ static_cast<uint8_t>(u >> (8 * k))) * 16777619u;
    }
    OBL_CHECK_EQ(hash, 0x003922e0u);
}

OBL_TEST_CASE("deterministic sqrt table holds the 23-bit mantissa of the float square root")
{
    std::vector<uint32_t> table(65536);
    obl::fast_sqrt::BuildTable(table.data());
    OBL_CHECK(table[0] == 0x3504f3u); // sqrt(2.0f) = 1.41421356f
    OBL_CHECK(table[32768] == 0u); // sqrt(1.0f) = 1.0f
    for (const uint32_t v : table) OBL_CHECK(v < 0x800000u);
    for (size_t i = 1; i < 32768; ++i) {
        OBL_CHECK(table[i] >= table[i - 1]);
        OBL_CHECK(table[i + 32768] >= table[i + 32767]);
    }
    OBL_CHECK(table[32767] > 0x7f0000u); // the top of the exponent-odd half approaches 2.0
}
