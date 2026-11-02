// The pure decisions of Scenery::OnEnterState, _Update and _ReceiveMessage (src/game/objects/scenery_logic.h, docs/specs/scenery.md).

#include "unit_test.h"

#include "game/objects/scenery_logic.h"

#include <bit>
#include <cstdint>

using namespace obl::scenery;

OBL_TEST_CASE("scenery_logic.seat_sender_excludes_self_and_system")
{
    OBL_CHECK(IsSeatSender(7, 9));
    OBL_CHECK(!IsSeatSender(9, 9));
    OBL_CHECK(!IsSeatSender(0xfffffffeu, 9));
    OBL_CHECK(!IsSeatSender(0xfffffffdu, 9));
    OBL_CHECK(IsSeatSender(0xffffffffu, 9));
}

OBL_TEST_CASE("scenery_logic.find_seat_first_match_or_max")
{
    const int32_t seats[2] = {5, -1};
    OBL_CHECK(FindSeat(seats, 2, -1) == 1);
    OBL_CHECK(FindSeat(seats, 2, 5) == 0);
    OBL_CHECK(FindSeat(seats, 2, 6) == 2);
    OBL_CHECK(FindSeat(seats, 1, -1) == 1);
    const int32_t both[2] = {-1, -1};
    OBL_CHECK(FindSeat(both, 2, -1) == 0);
}

OBL_TEST_CASE("scenery_logic.repair_state_steps_down")
{
    OBL_CHECK(NextRepairState(0) == 0);
    OBL_CHECK(NextRepairState(1) == 0);
    OBL_CHECK(NextRepairState(2) == 1);
    OBL_CHECK(NextRepairState(3) == 2);
    OBL_CHECK(NextRepairState(0xffffffffu) == 2);
    OBL_CHECK(RepairReplyType(0) == 92);
    OBL_CHECK(RepairReplyType(2) == 91);
    OBL_CHECK(NuisanceLevel(true) == 2);
    OBL_CHECK(NuisanceLevel(false) == 1);
}

OBL_TEST_CASE("scenery_logic.enter_state_and_lounger_direction")
{
    OBL_CHECK(EnterStateActs(1));
    OBL_CHECK(EnterStateActs(3));
    OBL_CHECK(!EnterStateActs(0));
    OBL_CHECK(!EnterStateActs(2));
    OBL_CHECK(!EnterStateActs(4));
    OBL_CHECK(LoungerDirection(0, 8) == 1);
    OBL_CHECK(LoungerDirection(7, 8) == 0);
    OBL_CHECK(LoungerDirection(3, 4) == 3);
    OBL_CHECK(LoungerDirection(4, 4) == 3);
    OBL_CHECK(LoungerDirection(6, 1) == 0);
}

OBL_TEST_CASE("scenery_logic.litter_jitter_and_snap")
{
    OBL_CHECK(LitterJitterX(0) == -30.0f);
    OBL_CHECK(LitterJitterX(59) == 29.0f);
    OBL_CHECK(LitterJitterX(60) == -30.0f);
    OBL_CHECK(LitterJitterY(0) == -14.0f);
    OBL_CHECK(LitterJitterY(27) == 13.0f);
    OBL_CHECK(SnapPoint(true) == 1);
    OBL_CHECK(SnapPoint(false) == 4);
}

OBL_TEST_CASE("scenery_logic.sell_refund_is_twenty_percent_through_double")
{
    OBL_CHECK(SellRefund(100.0f, 50) == 110.0f);
    // 0.2f widened to double is 0.20000000298..., so the cost picks up the float error before the final rounding.
    const double exact = 3.0 * static_cast<double>(0.2f) + 0.1f;
    OBL_CHECK(std::bit_cast<uint32_t>(SellRefund(0.1f, 3)) == std::bit_cast<uint32_t>(static_cast<float>(exact)));
    OBL_CHECK(SellRefund(0.0f, 0xffffffffu) > 8.5e8f);
}

OBL_TEST_CASE("scenery_logic.cell_flag_and_dirty_bit")
{
    OBL_CHECK(ApplyCellFlag(0x80000004u, 0x8000u, true) == 0x00008004u);
    OBL_CHECK(ApplyCellFlag(0x00008044u, 0x8000u, false) == 0x80000044u);
    OBL_CHECK(ApplyCellFlag(0x00000040u, 0x40u, false) == 0x80000000u);
}

OBL_TEST_CASE("scenery_logic.repair_update")
{
    OBL_CHECK(RepairUpdate(false, true, false, false, true) == RepairAction::None);
    OBL_CHECK(RepairUpdate(true, false, false, false, true) == RepairAction::None);
    OBL_CHECK(RepairUpdate(true, true, false, false, true) == RepairAction::CallMechanic);
    OBL_CHECK(RepairUpdate(true, true, true, false, true) == RepairAction::Flicker);
    OBL_CHECK(RepairUpdate(true, true, false, true, true) == RepairAction::Flicker);
    OBL_CHECK(RepairUpdate(true, true, true, true, false) == RepairAction::None);
    OBL_CHECK(FlashLight(0));
    OBL_CHECK(FlashLight(40));
    OBL_CHECK(!FlashLight(19));
}

OBL_TEST_CASE("scenery_logic.seated_guest_states")
{
    for (int s : {24, 29, 51, 53}) OBL_CHECK(IsSeatedGuestState(s));
    for (int s : {0, 23, 52, 54, -1}) OBL_CHECK(!IsSeatedGuestState(s));
}
