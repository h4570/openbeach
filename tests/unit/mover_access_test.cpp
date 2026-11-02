#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_access.h"

#include <array>
#include <bit>
#include <cstring>
#include <vector>

using namespace obl;
namespace {
struct Holder {
    alignas(object::Mover) unsigned char bytes[sizeof(object::Mover)];
    Holder()
    {
        std::memset(bytes, 0xa5, sizeof(bytes));
        obl::unit::StampVtable<object::Mover>(bytes);
    }
    object::Mover& Object() { return *reinterpret_cast<object::Mover*>(bytes); }
};
} // namespace

OBL_TEST_CASE("mover.setters_write_only_their_pair")
{
    Holder h;
    Holder untouched;
    moving::SetMovePosition(h.Object(), 0x3f800000u, 0x40000000u);
    OBL_CHECK(h.Object().m_vPosition[0] == 1.0f);
    OBL_CHECK(h.Object().m_vPosition[1] == 2.0f);
    moving::SetVelocity(h.Object(), 0x40400000u, 0x40800000u);
    moving::SetMoveTarget(h.Object(), 0x40a00000u, 0x40c00000u);
    OBL_CHECK(h.Object().m_vVelocity[0] == 3.0f);
    OBL_CHECK(h.Object().m_vTarget[1] == 6.0f);
    // Everything outside the three written pairs keeps the filler byte.
    for (size_t i = 0; i < sizeof(h.bytes); ++i) {
        const auto inPair = [i](size_t offset) { return i >= offset && i < offset + 2 * sizeof(float); };
        const bool written = inPair(offsetof(object::Mover, m_vPosition)) || inPair(offsetof(object::Mover, m_vVelocity)) ||
                inPair(offsetof(object::Mover, m_vTarget));
        if (!written) OBL_CHECK_EQ(h.bytes[i], untouched.bytes[i]);
    }
}

OBL_TEST_CASE("mover.words_keep_nan_payloads")
{
    Holder h;
    moving::SetMovePosition(h.Object(), 0x7fa12345u, 0xffc00001u);
    uint32_t bits[2];
    std::memcpy(bits, h.Object().m_vPosition, sizeof(bits));
    OBL_CHECK_EQ(bits[0], 0x7fa12345u);
    OBL_CHECK_EQ(bits[1], 0xffc00001u);
    float out[2];
    OBL_CHECK(moving::GetMovePosition(h.Object(), out) == out);
    std::memcpy(bits, out, sizeof(bits));
    OBL_CHECK_EQ(bits[0], 0x7fa12345u);
    OBL_CHECK_EQ(bits[1], 0xffc00001u);
}

OBL_TEST_CASE("mover.getters_return_their_buffer")
{
    Holder h;
    moving::SetVelocity(h.Object(), std::bit_cast<uint32_t>(1.5f), std::bit_cast<uint32_t>(-2.5f));
    float out[2] = {0, 0};
    OBL_CHECK(moving::GetVelocity(h.Object(), out) == out);
    OBL_CHECK(out[0] == 1.5f);
    OBL_CHECK(out[1] == -2.5f);
}

OBL_TEST_CASE("mover.set_moving_stores_the_low_byte_unnormalised")
{
    Holder h;
    moving::SetMoving(h.Object(), 1);
    OBL_CHECK_EQ(h.Object().m_moving, 1);
    moving::SetMoving(h.Object(), 0x1234u);
    OBL_CHECK_EQ(h.Object().m_moving, 0x34);
    moving::SetMoving(h.Object(), 0x100u);
    OBL_CHECK_EQ(h.Object().m_moving, 0);
}

OBL_TEST_CASE("mover.cursor_tile_reads_the_current_step")
{
    std::vector<std::array<int, 2>> tiles = {{1, 2}, {3, 4}, {5, 6}};
    path::RouteCursor cursor{};
    cursor.m_route = std::make_shared<path::Route>(path::Route{.m_steps = {{1, 2}, {3, 4}, {5, 6}}});
    uint32_t out[2];
    for (int step = 0; step < 3; ++step) {
        cursor.m_currentStep = step;
        OBL_CHECK(moving::CursorTile(cursor, out) == out);
        OBL_CHECK_EQ(out[0], static_cast<uint32_t>(tiles[step][0]));
        OBL_CHECK_EQ(out[1], static_cast<uint32_t>(tiles[step][1]));
    }
}
