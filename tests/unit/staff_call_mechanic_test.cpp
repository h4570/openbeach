#include "unit_test.h"

#include "staff_call_fixture.h"
#include "game/objects/staff_call_mechanic.h"

using namespace staff_call_test;
namespace call = obl::staff::call;
using obl::object::Mechanic;

namespace {

struct Rig {
    std::unique_ptr<Mechanic> mechanic = Zeroed<Mechanic>();
    std::unique_ptr<obl::object::Base> target = Zeroed<obl::object::Base>();
    SpriteRig sprite;
    FakeEnv env;
    Rig() : sprite(*mechanic, 8, 0)
    {
        mechanic->m_id = 21;
        mechanic->m_DestObjectID = 40;
        env.objects[40] = target.get();
    }
    void Run(int32_t id)
    {
        env.log.clear();
        call::MechanicCallFunction(*mechanic, id, env);
    }
};

OBL_TEST_CASE("staff_call_mechanic.visibility_time_and_thought")
{
    Rig rig;
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.mechanic->m_visible, 1);
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.mechanic->m_visible, 0);
    rig.Run(0x74);
    OBL_CHECK_EQ(rig.mechanic->m_lastTaskTime, 12.5f);
    rig.Run(0x7b);
    OBL_CHECK_EQ(rig.env.log, std::string("thought(20000000) "));
}

OBL_TEST_CASE("staff_call_mechanic.a_repair_only_mechanic_looks_for_repairs_then_inspections")
{
    Rig rig;
    rig.mechanic->m_JobMask = 1;
    rig.env.repairJobs[2] = -1;
    rig.env.repairJobs[0] = 77;
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.env.log, std::string("find(2) find(0) "));
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 77);
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 1);
    OBL_CHECK_EQ(rig.mechanic->m_lastTaskTime, 12.5f);
    OBL_CHECK_EQ(rig.mechanic->m_repairType, 2);
}

OBL_TEST_CASE("staff_call_mechanic.a_vandal_only_mechanic_asks_for_kind_one")
{
    Rig rig;
    rig.mechanic->m_JobMask = 2;
    rig.env.repairJobs[1] = 8;
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.env.log, std::string("find(1) "));
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 8);
}

OBL_TEST_CASE("staff_call_mechanic.a_mixed_mask_picks_the_first_kind_by_the_roll_and_falls_back")
{
    Rig rig;
    rig.mechanic->m_JobMask = 3;
    rig.env.rand = 50; // < 51 -> kind 1 first
    rig.env.repairJobs[2] = 5;
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.env.log, std::string("find(1) find(2) "));
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 5);
    rig.env.rand = 51; // kind 2 first, found directly
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.env.log, std::string("find(2) "));
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 1);
}

OBL_TEST_CASE("staff_call_mechanic.nothing_found_clears_the_flag_and_skips_the_timestamp")
{
    Rig rig;
    rig.mechanic->m_JobMask = 3;
    rig.mechanic->m_foundRepair = 1;
    rig.mechanic->m_lastTaskTime = 1.0f;
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, -1);
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 0);
    OBL_CHECK_EQ(rig.mechanic->m_lastTaskTime, 1.0f);
    OBL_CHECK_EQ(rig.env.log, std::string("find(1) find(2) find(0) "));
}

OBL_TEST_CASE("staff_call_mechanic.a_watering_roll_above_eighty_tries_kind_three_first")
{
    Rig rig;
    rig.mechanic->m_JobMask = 4;
    rig.env.rand = 81;
    rig.env.repairJobs[3] = 9;
    rig.env.repairTypeAfterFind = 3; // the lookup reports a watering job
    rig.Run(0x99);
    OBL_CHECK_EQ(rig.env.log, std::string("find(3) "));
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 9);
    OBL_CHECK_EQ(rig.mechanic->m_repairType, 3);
    Rig quiet;
    quiet.mechanic->m_JobMask = 4;
    quiet.env.rand = 80; // not above 80
    quiet.Run(0x99);
    OBL_CHECK_EQ(quiet.env.log, std::string());
    OBL_CHECK_EQ(quiet.mechanic->m_foundRepair, 0);
}

OBL_TEST_CASE("staff_call_mechanic.a_better_job_replaces_the_destination")
{
    Rig rig;
    rig.Run(0x9a);
    OBL_CHECK_EQ(rig.mechanic->m_foundBetterJob, 0);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 40);
    rig.env.betterJob = 12;
    rig.Run(0x9a);
    OBL_CHECK_EQ(rig.mechanic->m_foundBetterJob, 1);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 12);
}

OBL_TEST_CASE("staff_call_mechanic.watering_needs_the_watering_bit_and_a_job")
{
    Rig rig;
    rig.env.wateringJob = 6;
    rig.Run(0x9b); // bit 2 missing
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 0);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 40);
    rig.mechanic->m_JobMask = 4;
    rig.Run(0x9b);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, 6);
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 1);
    OBL_CHECK_EQ(rig.mechanic->m_repairType, 3);
    OBL_CHECK_EQ(rig.mechanic->m_lastTaskTime, 12.5f);
    rig.env.wateringJob = -1;
    rig.Run(0x9b);
    OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, -1);
    OBL_CHECK_EQ(rig.mechanic->m_foundRepair, 0);
}

OBL_TEST_CASE("staff_call_mechanic.returning_a_job_depends_on_the_object_type")
{
    Rig rig;
    rig.target->m_type = ObjectType::Building;
    rig.Run(0x9d);
    OBL_CHECK_EQ(rig.env.log, std::string("object(40) return(40,2) "));
    rig.target->m_type = ObjectType::Scenery;
    rig.Run(0x9d);
    OBL_CHECK_EQ(rig.env.log, std::string("object(40) return(40,1) "));
}

OBL_TEST_CASE("staff_call_mechanic.finishing_a_job_pushes_the_thought_for_its_type_and_releases_it")
{
    Rig rig;
    const struct {
        int32_t type;
        const char* thought;
    } cases[] = {{0, "thought(4) "}, {1, "thought(10) "}, {2, "thought(8) "}, {3, "thought(20) "}};
    for (const auto& c : cases) {
        rig.mechanic->m_DestObjectID = 40;
        rig.mechanic->m_repairType = c.type;
        rig.Run(0x9e);
        OBL_CHECK_EQ(rig.env.log, std::string(c.thought) + "release(40) ");
        OBL_CHECK_EQ(rig.mechanic->m_DestObjectID, -1);
    }
    rig.mechanic->m_repairType = 9;
    rig.Run(0x9e);
    OBL_CHECK_EQ(rig.env.log, std::string("release(-1) "));
}

OBL_TEST_CASE("staff_call_mechanic.starting_a_job_bumps_the_daily_counter_of_its_type")
{
    Rig rig;
    for (int32_t type : {0, 1, 2, 2, 3, 7}) {
        rig.mechanic->m_repairType = type;
        rig.Run(0x9f);
        OBL_CHECK_EQ(rig.env.log, std::string("started(40) "));
    }
    OBL_CHECK_EQ(rig.mechanic->m_inspectionsToday, 1);
    OBL_CHECK_EQ(rig.mechanic->m_vandalsToday, 1);
    OBL_CHECK_EQ(rig.mechanic->m_repairsToday, 2);
    OBL_CHECK_EQ(rig.mechanic->m_wateredToday, 1);
}

OBL_TEST_CASE("staff_call_mechanic.the_watering_position_is_offset_from_the_target")
{
    Rig rig;
    rig.target->m_WorldXYZ[0] = 100;
    rig.target->m_WorldXYZ[1] = 50;
    rig.Run(0xa0);
    OBL_CHECK_EQ(rig.mechanic->m_WateringPos[0], 134.0f);
    OBL_CHECK_EQ(rig.mechanic->m_WateringPos[1], 28.0f);
}

OBL_TEST_CASE("staff_call_mechanic.the_watering_pose_faces_direction_five_or_the_last_one")
{
    Rig rig;
    rig.Run(0xa1);
    OBL_CHECK_EQ(rig.sprite.instance.m_direction, 5);
    rig.sprite.anim.m_directions = 4;
    rig.Run(0xa1);
    OBL_CHECK_EQ(rig.sprite.instance.m_direction, 3);
    rig.sprite.anim.m_directions = 6;
    rig.Run(0xa1);
    OBL_CHECK_EQ(rig.sprite.instance.m_direction, 5);
}

OBL_TEST_CASE("staff_call_mechanic.select_and_feedback")
{
    Rig rig;
    rig.mechanic->m_RequiredFeedback = 5;
    rig.mechanic->m_FeedbackStatus = 7;
    rig.Run(0xad);
    OBL_CHECK_EQ(rig.mechanic->m_FeedbackStatus, 5);
    OBL_CHECK_EQ(rig.env.log, std::string("base(173) "));
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.mechanic->m_waitingForDeath, 1);
    rig.Run(0xaf);
    OBL_CHECK_EQ(rig.env.log, std::string("blink(MechanicIcon,1) "));
    rig.mechanic->m_RequiredFeedback = 2;
    rig.Run(0xb0);
    OBL_CHECK_EQ(rig.env.log, std::string("select tab(MechanicDialog,2) thought(2) blink(MechanicIcon,0) "));
    rig.mechanic->m_RequiredFeedback = 8;
    rig.env.feedbackKey = "M";
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(8) feedback(M,21,9) thought(8) blink(MechanicIcon,0) "));
}

OBL_TEST_CASE("staff_call_mechanic.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0x9c, 0xa2, 0x76, 0}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
