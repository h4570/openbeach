#include "unit_test.h"
#include "game/objects/guest_visit_opinions.h"

#include <cmath>
#include <limits>

using namespace obl;
using namespace obl::world_data;
using obl::object::Guest;
using namespace obl::tables;

OBL_TEST_CASE("guest_visit.record_quirks")
{
    Guest guest{};
    obl::tables::GuestDef def{};
    def.m_OpinionEffect[Cleanliness][1] = 0.125f;
    def.m_OpinionEffect[Safety][1] = 0.25f;
    def.m_OpinionEffect[Crowding][1] = 0.0625f;
    guest.m_worstScore = 0;
    guest_visit::SpotScores(guest, def, 1);
    OBL_CHECK_EQ(guest.m_worstScore, 0.125f);
    OBL_CHECK_EQ(guest.m_Worst, 1);
    guest.m_worstScore = -0.25f; // beaten by doubled safety, but not by doubled crowding
    guest_visit::SpotScores(guest, def, 10);
    OBL_CHECK_EQ(guest.m_worstScore, -0.125f);
    OBL_CHECK_EQ(guest.m_Worst, 10);
    OBL_CHECK_EQ(guest.m_WorstSpotOpinion, 1);
}

OBL_TEST_CASE("guest_visit.queue_penalty_precedes_cascade_and_clears_variety")
{
    Guest guest{};
    obl::tables::GuestDef def{};
    def.m_OpinionEffect[Crowding][1] = 0.25f;
    def.m_OpinionEffect[Variety][1] = 0.125f;
    guest.m_opinion[Crowding] = 0.125f;
    guest.m_opinion[Variety] = 0.75f;
    guest.m_curScore = 0.5f;
    guest_visit::QueuePenalty(guest, def);
    OBL_CHECK_EQ(guest.m_opinion[Crowding], -0.125f); // still negative at the message handler post routine
    OBL_CHECK_EQ(guest.m_opinion[Variety], 0.0f);
    OBL_CHECK_EQ(guest.m_curScore, 0.125f);
}

OBL_TEST_CASE("guest_visit.missed_purchase_retains_penalty_and_best_wins")
{
    Guest guest{};
    guest.m_missedPurchase = 1;
    guest.m_curScore = -0.5f;
    guest.m_bestScore = -1.0f;
    guest.m_worstScore = 1.0f;
    guest.m_CurID = 42;
    guest.m_CurTypeID = 7;
    guest.m_queueScore = 0.5f;
    guest_visit::FinishPurchase(guest);
    OBL_CHECK_EQ(guest.m_bestScore, -0.5f);
    OBL_CHECK_EQ(guest.m_Best, 42);
    OBL_CHECK_EQ(guest.m_BestType, 7);
    OBL_CHECK_EQ(guest.m_worstScore, 1.0f);
    OBL_CHECK_EQ(guest.m_opinion[Crowding], 0.0f);
    OBL_CHECK_EQ(guest.m_queueScore, 0.0f);
    OBL_CHECK_EQ(guest.m_missedPurchase, 0);
    OBL_CHECK_EQ(guest.m_curScore, 0.0f);
}

OBL_TEST_CASE("guest_visit.purchase_has_no_lower_clamp_and_activity_keeps_records")
{
    Guest guest{};
    guest.m_activity = 1;
    guest.m_queueScore = -0.5f;
    guest.m_safetyScore = 2.0f;
    guest.m_curScore = 1.5f;
    guest_visit::FinishPurchase(guest);
    OBL_CHECK_EQ(guest.m_opinion[Crowding], -0.5f);
    OBL_CHECK_EQ(guest.m_opinion[Safety], 1.0f);
    OBL_CHECK_EQ(guest.m_bestScore, 0.0f);
    OBL_CHECK_EQ(guest.m_worstScore, 0.0f);
}

OBL_TEST_CASE("guest_visit.ratios_and_safety_keep_double_precision")
{
    OBL_CHECK_EQ(guest_visit::PriceRatio(1.0f, 3.0f), 1.0 / 3.0);
    OBL_CHECK(guest_visit::PriceRatio(1.0f, 3.0f) < static_cast<double>(1.0f / 3.0f));
    OBL_CHECK_EQ(guest_visit::PriceRatio(2.0f, 0.0f), 3.0);
    OBL_CHECK_EQ(guest_visit::PriceRatio(2.0f, -1.0f), 3.0);
    OBL_CHECK_EQ(guest_visit::SafetyQuality(0), 3.0 * static_cast<double>(0.33f));
    OBL_CHECK_EQ(guest_visit::SafetyQuality(4), -static_cast<double>(0.33f));
    float value = std::numeric_limits<float>::quiet_NaN();
    guest_visit::Clamp(value);
    OBL_CHECK_EQ(value, 0.0f);
}
