#include "unit_test.h"
#include "game/objects/building_events.h"
#include "game/objects/building_events_state.h"

#include <cmath>

using namespace obl;

OBL_TEST_CASE("building_events.strict_start_and_inclusive_active_end")
{
    object::EventSlot slot{};
    slot.m_start = 13.0f;
    slot.m_duration = 2.5f;
    OBL_CHECK(!building_events::CanStart(slot, 13.0f));
    OBL_CHECK(building_events::CanStart(slot, 13.1f));
    OBL_CHECK(!building_events::CanStart(slot, 15.5f));
    OBL_CHECK(!building_events::PastEnd(slot, 15.5f));
    OBL_CHECK(building_events::PastEnd(slot, 15.6f));
    // 13.1f exceeds 13 + 0.1f in double precision; rounding the end to float would hide the expiry.
    slot.m_start = 13.0f;
    slot.m_duration = 0.1f;
    OBL_CHECK(building_events::PastEnd(slot, 13.1f));
    OBL_CHECK(!building_events::CanStart(slot, 13.1f));
}

OBL_TEST_CASE("building_events.loaded_cursor_uses_lists_instead_of_counters")
{
    object::BuildingEvents events{};
    for (int i = 0; i < 3; ++i) {
        events.m_slots[i].m_start = 9.0f + i * 4.0f;
        events.m_slots[i].m_duration = 2.5f;
        events.m_slots[i].m_NumActors = 999;
    }
    OBL_CHECK_EQ(building_events::ChooseSlot(events, 13.0f), 1);
    OBL_CHECK_EQ(building_events::ChooseSlot(events, 23.0f), 0);
    events.m_slots[2].m_start = 13.0f;
    events.m_slots[2].m_guests = {1};
    events.m_eventActive = 1;
    OBL_CHECK_EQ(building_events::ChooseSlot(events, 13.0f), 2);
    events.m_eventActive = 0;
    OBL_CHECK_EQ(building_events::ChooseSlot(events, 13.0f), 1);
}

OBL_TEST_CASE("building_events.cast_and_guest_jitter")
{
    const char* keys[]{"WEIGHTLIFTER", "SINGING", "CATWALK", "CABARET", "WETTSHIRT", "AEROBICS"};
    const int types[]{16, 15, 17, 14, 18, 20};
    const int counts[]{3, 1, 3, 1, 4, 1};
    for (int i = 0; i < 6; ++i) {
        auto cast = building_events::GetCast(keys[i]);
        OBL_CHECK_EQ(cast.type, types[i]);
        OBL_CHECK_EQ(cast.count, counts[i]);
        OBL_CHECK(cast.randomVariant == (i == 3 || i == 5));
    }
    OBL_CHECK_EQ(building_events::GetCast("unrecognized").count, 0);
    OBL_CHECK(std::abs(building_events::GuestDelay(0, 2.0f) - 1.7f) < 0.000001f);
    OBL_CHECK(std::abs(building_events::GuestDelay(32767, 2.0f) - 2.3f) < 0.000001f);
}
