#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_update_opinions.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using obl::object::Guest;
using namespace obl::tables;

namespace {

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();
    uint32_t counts[6] = {2, 3, 5, 7, 11, 13};
    GuestOpinionWorld world{0.5f, 10, counts, 41};
    int draws = 0;
    int nextDraw = 0;

    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        *def = GuestDef{};
        guest->m_state = 1;
        for (float& value : guest->m_opinion) value = 0.5f;
        for (int i = 0; i < 7; ++i) {
            guest->m_Tolerance[2 * i] = 0.25f;
            guest->m_Tolerance[2 * i + 1] = 0.75f;
            def->m_OpinionEffect[i][0] = 0.125f;
            def->m_OpinionEffect[i][1] = 0.0625f;
        }
        def->m_AttractivePref = 0.5f;
        std::strcpy(guest->m_WalkAnimation, "d_walk_normal");
        std::strcpy(guest->m_StandAnimation, "d_stand_normal");
    }
    void Run()
    {
        UpdateGuestOpinions(*guest, *def, world, [&] {
            ++draws;
            return nextDraw;
        });
    }
};

} // namespace

OBL_TEST_CASE("guest_opinions.skipped_states_change_nothing")
{
    for (int state : {0x28, 0x29, 0x26, 0x25}) {
        Fixture f;
        f.guest->m_state = state;
        f.world.scenicValue = 5.0f;
        f.Run();
        OBL_CHECK_EQ(f.guest->m_opinion[Beauty], 0.5f);
        OBL_CHECK_EQ(f.guest->m_happiness, 0.0f);
        OBL_CHECK_EQ(f.draws, 0);
    }
}

OBL_TEST_CASE("guest_opinions.beauty_and_litter_bands")
{
    Fixture f;
    f.world.scenicValue = 0.9f; // above the band: beauty up by effect[0]
    f.world.litter = 50; // above the band (0.75): cleanliness down by effect[1]
    f.Run();
    OBL_CHECK_EQ(f.guest->m_opinion[Beauty], 0.625f);
    OBL_CHECK_EQ(f.guest->m_opinion[Cleanliness], 0.4375f);
    Fixture g;
    g.world.scenicValue = 0.1f; // below: down by effect[1]
    g.world.litter = 0; // below the lower bound: up by effect[0]
    g.Run();
    OBL_CHECK_EQ(g.guest->m_opinion[Beauty], 0.4375f);
    OBL_CHECK_EQ(g.guest->m_opinion[Cleanliness], 0.625f);
    Fixture h;
    h.world.scenicValue = 0.5f; // inside the band: unchanged
    h.world.litter = 0;
    h.guest->m_Tolerance[10] = 0.0f; // lower bound 0: litter 0 is not below it
    h.Run();
    OBL_CHECK_EQ(h.guest->m_opinion[Beauty], 0.5f);
    OBL_CHECK_EQ(h.guest->m_opinion[Cleanliness], 0.5f);
}

OBL_TEST_CASE("guest_opinions.clamps")
{
    Fixture f;
    f.world.scenicValue = 2.0f;
    f.guest->m_opinion[Beauty] = 0.95f;
    f.world.litter = 99;
    f.guest->m_opinion[Cleanliness] = 0.03f;
    f.Run();
    OBL_CHECK_EQ(f.guest->m_opinion[Beauty], 1.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[Cleanliness], 0.0f);
}

OBL_TEST_CASE("guest_opinions.totty_ratio_uses_the_opposite_gender_counts")
{
    // preference 0.5 = band 1; gender 0 reads entry 3 + 1 = 4 (11 of 41 guests = 0.268), gender 1 reads entry 1 (3 of 41 = 0.073)
    Fixture f;
    f.guest->m_Tolerance[4] = 0.1f;
    f.guest->m_Tolerance[5] = 0.2f;
    f.def->m_Gender = 0;
    f.Run();
    OBL_CHECK_EQ(f.guest->m_opinion[Totty], 0.625f); // 0.268 > 0.2: up
    Fixture g;
    g.guest->m_Tolerance[4] = 0.1f;
    g.guest->m_Tolerance[5] = 0.2f;
    g.def->m_Gender = 1;
    g.Run();
    OBL_CHECK_EQ(g.guest->m_opinion[Totty], 0.4375f); // 0.073 < 0.1: down
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.25f), 0);
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.26f), 0); // the float 0.26 is below the double 0.26
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.5f), 1);
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.74f), 2); // 0.74f = 0.7400000095 is above the double 0.74
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.7399f), 1);
    OBL_CHECK_EQ(guest_opinions::AttractivenessBand(0.75f), 2);
}

OBL_TEST_CASE("guest_opinions.no_variety_fades_everything")
{
    Fixture f;
    f.guest->m_opinion[Variety] = 0.0f;
    f.guest->m_opinion[Safety] = 0.001f;
    f.world.scenicValue = 5.0f; // the bands are not looked at
    f.Run();
    OBL_CHECK_EQ(f.guest->m_opinion[Beauty], 0.5f - 0.0625f * 0.083333336f);
    OBL_CHECK_EQ(f.guest->m_opinion[Crowding], 0.5f - 0.0625f * 0.04f);
    OBL_CHECK_EQ(f.guest->m_opinion[Safety], 0.0f); // clamped
}

OBL_TEST_CASE("guest_opinions.happiness_is_the_mean_of_the_seven")
{
    Fixture f;
    f.guest->m_Tolerance[10] = 0.0f; // litter 10 inside the band
    f.guest->m_Tolerance[11] = 100.0f;
    f.Run();
    OBL_CHECK(std::abs(f.guest->m_happiness - 0.5f) < 1e-6f);
}

OBL_TEST_CASE("guest_opinions.walk_animation_follows_the_mood")
{
    { // sad: happiness below 0.4 and sober; one draw, 30 + (draw % 7 - 3)
        Fixture f;
        for (float& value : f.guest->m_opinion) value = 0.1f;
        f.guest->m_opinion[Variety] = 0.0f; // fading keeps them low
        f.nextDraw = 100; // 100 % 7 = 2
        f.Run();
        OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("d_walk_sad"));
        OBL_CHECK_EQ(std::string(f.guest->m_StandAnimation), std::string("d_stand_sad"));
        OBL_CHECK_EQ(f.draws, 1);
        OBL_CHECK_EQ(f.guest->m_maxVel, 29.0f);
        OBL_CHECK_EQ(f.guest->m_maxForce, static_cast<float>(29.0 * static_cast<double>(0.33333334f)));
    }
    { // drunk (0.5 or more) overrides sad
        Fixture f;
        f.guest->m_drunk = 0.5f;
        f.nextDraw = 6;
        f.Run();
        OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("d_walk_drunk"));
        OBL_CHECK_EQ(f.guest->m_maxVel, 33.0f);
    }
    { // normal after sad: 60 + (draw % 15 - 7), the stand name is copied with its terminator over the old bytes
        Fixture f;
        std::strcpy(f.guest->m_WalkAnimation, "d_walk_sad");
        std::memset(f.guest->m_StandAnimation, 'x', sizeof(f.guest->m_StandAnimation));
        f.nextDraw = 7;
        f.Run();
        OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("d_walk_normal"));
        OBL_CHECK_EQ(std::string(f.guest->m_StandAnimation), std::string("d_stand_normal"));
        OBL_CHECK_EQ(f.guest->m_StandAnimation[15], 'x'); // only strlen + 1 bytes are written
        OBL_CHECK_EQ(f.guest->m_maxVel, 60.0f);
    }
    { // already walking that way: nothing drawn, speed untouched
        Fixture f;
        f.guest->m_maxVel = 12.0f;
        f.Run();
        OBL_CHECK_EQ(f.draws, 0);
        OBL_CHECK_EQ(f.guest->m_maxVel, 12.0f);
    }
    { // a desperate walk stays
        Fixture f;
        std::strcpy(f.guest->m_WalkAnimation, "D_WALK_DESPERATE");
        f.Run();
        OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("D_WALK_DESPERATE"));
        OBL_CHECK_EQ(f.draws, 0);
    }
}
