#include "unit_test.h"
#include "game/player/player_state.h"
#include <cstring>
#include <string>

using obl::Char16;
using obl::player::Player;

namespace {
uint32_t BitsOf(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
} // namespace

OBL_TEST_CASE("player.construct_keeps_the_name_and_clears_cash")
{
    const Char16 name[] = {0x017b, 0x6d77, 0};
    Player player(name);
    OBL_CHECK(player.m_name == std::basic_string<Char16>(name));
    OBL_CHECK_EQ(BitsOf(player.m_cash), 0u);
}
