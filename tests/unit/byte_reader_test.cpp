// The little-endian byte reader (src/core/byte_reader.h) that replaces memcpy/reinterpret_cast reads in loaders.

#include "unit_test.h"

#include "core/byte_reader.h"

#include <array>
#include <cstdint>

using obl::ByteFits;
using obl::Read;

OBL_TEST_CASE("byte reader: integers are little-endian")
{
    const std::array<uint8_t, 8> bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x88};
    OBL_CHECK(Read<uint8_t>(bytes, 7) == 0x88);
    OBL_CHECK(Read<uint16_t>(bytes, 0) == 0x0201);
    OBL_CHECK(Read<uint32_t>(bytes, 0) == 0x04030201u);
    OBL_CHECK(Read<uint64_t>(bytes, 0) == 0x8807060504030201ull);
    OBL_CHECK(Read<int16_t>(bytes, 6) == static_cast<int16_t>(0x8807));
}

OBL_TEST_CASE("byte reader: misaligned offsets and floats")
{
    const std::array<uint8_t, 5> bytes{0xff, 0x00, 0x00, 0x80, 0x3f};
    OBL_CHECK(Read<uint32_t>(bytes, 1) == 0x3f800000u);
    OBL_CHECK(Read<float>(bytes, 1) == 1.0f);
}

OBL_TEST_CASE("byte reader: ByteFits checks the bounds")
{
    const std::array<uint8_t, 4> bytes{};
    OBL_CHECK(ByteFits<uint32_t>(bytes, 0));
    OBL_CHECK(!ByteFits<uint32_t>(bytes, 1));
    OBL_CHECK(ByteFits<uint16_t>(bytes, 2));
    OBL_CHECK(!ByteFits<uint8_t>(bytes, 5));
}

static_assert(Read<uint16_t>(std::array<uint8_t, 2>{0x34, 0x12}, 0) == 0x1234);
