// The remaining branches of src/game/objects/guest_visit_opinions.h (guest_visit_opinions_test.cpp covers the quirks): every SpotScores
// kind, the worst-record branch of FinishPurchase and Clamp's upper bound.

#include "unit_test.h"
#include "game/objects/guest_visit_opinions.h"

using namespace obl;
using namespace obl::world_data;
using obl::object::Guest;
using namespace obl::tables;

namespace {

obl::tables::GuestDef Def()
{
    obl::tables::GuestDef def{};
    def.m_OpinionEffect[Cleanliness][1] = 0.125f;
    def.m_OpinionEffect[Beauty][1] = 0.25f;
    def.m_OpinionEffect[Safety][1] = 0.0625f;
    def.m_OpinionEffect[Crowding][1] = 0.5f;
    def.m_OpinionEffect[Totty][0] = 0.375f;
    return def;
}

void MakeGuest(Guest& guest)
{
    for (float& o : guest.m_opinion) o = 0.5f;
    guest.m_worstScore = 0.0f;
    guest.m_bestScore = 0.0f;
    guest.m_Worst = -1;
    guest.m_Best = -1;
}

} // namespace

OBL_TEST_CASE("guest_visit.spot_kind_0_litter_stores_negative_cleanliness")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 0);
    OBL_CHECK_EQ(guest.m_opinion[Cleanliness], 0.375f);
    OBL_CHECK_EQ(guest.m_worstScore, -0.125f);
    OBL_CHECK_EQ(guest.m_Worst, 0);
    OBL_CHECK_EQ(guest.m_WorstSpotOpinion, 1);
}

OBL_TEST_CASE("guest_visit.spot_kind_2_sums_cleanliness_beauty_and_safety")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 2);
    OBL_CHECK_EQ(guest.m_opinion[Cleanliness], 0.375f);
    OBL_CHECK_EQ(guest.m_opinion[Beauty], 0.25f);
    OBL_CHECK_EQ(guest.m_opinion[Safety], 0.4375f);
    OBL_CHECK_EQ(guest.m_worstScore, -0.4375f);
    OBL_CHECK_EQ(guest.m_Worst, 2);
    // A second, equally bad spot does not replace the record (strictly below only).
    guest_visit::SpotScores(guest, Def(), 2);
    OBL_CHECK_EQ(guest.m_Worst, 2);
    OBL_CHECK_EQ(guest.m_opinion[Beauty], 0.0f); // no clamp here: 0.25 - 0.25
}

OBL_TEST_CASE("guest_visit.spot_kind_3_safety_only")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 3);
    OBL_CHECK_EQ(guest.m_opinion[Safety], 0.4375f);
    OBL_CHECK_EQ(guest.m_opinion[Cleanliness], 0.5f);
    OBL_CHECK_EQ(guest.m_worstScore, -0.0625f);
    OBL_CHECK_EQ(guest.m_Worst, 3);
}

OBL_TEST_CASE("guest_visit.spot_kinds_4_5_raise_totty_and_take_the_best_record")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 5);
    OBL_CHECK_EQ(guest.m_opinion[Totty], 0.875f);
    OBL_CHECK_EQ(guest.m_bestScore, 0.375f);
    OBL_CHECK_EQ(guest.m_Best, 5);
    OBL_CHECK_EQ(guest.m_BestSpotOpinion, 1);
    OBL_CHECK_EQ(guest.m_Worst, -1); // never a worst candidate

    guest_visit::SpotScores(guest, Def(), 4); // not better than the stored best: only the opinion moves, without an upper clamp
    OBL_CHECK_EQ(guest.m_opinion[Totty], 1.25f);
    OBL_CHECK_EQ(guest.m_Best, 5);
}

OBL_TEST_CASE("guest_visit.spot_kinds_6_7_crowding")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 7);
    OBL_CHECK_EQ(guest.m_opinion[Crowding], 0.0f);
    OBL_CHECK_EQ(guest.m_worstScore, -0.5f);
    OBL_CHECK_EQ(guest.m_Worst, 7);
    guest_visit::SpotScores(guest, Def(), 6); // goes negative: SpotScores itself never clamps
    OBL_CHECK_EQ(guest.m_opinion[Crowding], -0.5f);
    OBL_CHECK_EQ(guest.m_Worst, 7);
}

OBL_TEST_CASE("guest_visit.spot_unrecognized_kind_changes_nothing")
{
    Guest guest{};
    MakeGuest(guest);
    guest_visit::SpotScores(guest, Def(), 11);
    for (float o : guest.m_opinion) OBL_CHECK_EQ(o, 0.5f);
    OBL_CHECK_EQ(guest.m_Worst, -1);
    OBL_CHECK_EQ(guest.m_Best, -1);
}

OBL_TEST_CASE("guest_visit.finish_purchase_takes_worst_record_when_not_best")
{
    Guest guest{};
    guest.m_curScore = -0.25f;
    guest.m_bestScore = 0.5f;
    guest.m_worstScore = 0.0f;
    guest.m_CurID = 17;
    guest.m_CurTypeID = 3;
    guest.m_WorstSpotOpinion = 1;
    guest.m_queueScore = 0.75f;
    guest.m_opinion[Crowding] = 0.5f; // 0.5 + 0.75 clamps to 1
    guest.m_safetyScore = 0.25f;
    guest.m_opinion[Safety] = 0.5f;
    guest.m_priceScore = 0.5f;
    guest.m_cleanScore = 0.5f;
    guest_visit::FinishPurchase(guest);
    OBL_CHECK_EQ(guest.m_opinion[Crowding], 1.0f);
    OBL_CHECK_EQ(guest.m_opinion[Safety], 0.75f);
    OBL_CHECK_EQ(guest.m_worstScore, -0.25f);
    OBL_CHECK_EQ(guest.m_Worst, 17);
    OBL_CHECK_EQ(guest.m_WorstType, 3);
    OBL_CHECK_EQ(guest.m_WorstSpotOpinion, 0);
    OBL_CHECK_EQ(guest.m_bestScore, 0.5f);
    OBL_CHECK_EQ(guest.m_priceScore, 0.0f);
    OBL_CHECK_EQ(guest.m_cleanScore, 0.0f);
    OBL_CHECK_EQ(guest.m_curScore, 0.0f);

    // Neither better than the best nor below the worst: no record changes.
    guest.m_curScore = 0.25f;
    guest.m_CurID = 18;
    guest_visit::FinishPurchase(guest);
    OBL_CHECK_EQ(guest.m_Worst, 17);
    OBL_CHECK_EQ(guest.m_bestScore, 0.5f);
}

OBL_TEST_CASE("guest_visit.clamp_bounds_and_subtract_in_double")
{
    float high = 1.5f, low = -0.25f, mid = 0.5f, one = 1.0f;
    guest_visit::Clamp(high);
    guest_visit::Clamp(low);
    guest_visit::Clamp(mid);
    guest_visit::Clamp(one);
    OBL_CHECK_EQ(high, 1.0f);
    OBL_CHECK_EQ(low, 0.0f);
    OBL_CHECK_EQ(mid, 0.5f);
    OBL_CHECK_EQ(one, 1.0f);

    float value = 1.0f;
    guest_visit::Subtract(value, 1.0 / 3.0); // rounded to float once, from the double difference
    OBL_CHECK_EQ(value, static_cast<float>(1.0 - 1.0 / 3.0));
}
