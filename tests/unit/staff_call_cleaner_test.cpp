#include "unit_test.h"

#include "game/objects/object_building.h"
#include "game/objects/staff_call_cleaner.h"
#include "staff_call_fixture.h"

using namespace staff_call_test;
namespace call = obl::staff::call;
using obl::object::Building;

namespace {

struct Rig {
    std::unique_ptr<Cleaner> cleaner = Zeroed<Cleaner>();
    std::unique_ptr<Building> target = Zeroed<Building>();
    FakeEnv env;
    Rig()
    {
        cleaner->m_id = 31;
        cleaner->m_DestObjectID = 40;
        target->m_id = 40;
        env.objects[40] = target.get();
    }
    void Run(int32_t id)
    {
        env.log.clear();
        call::CleanerCallFunction(*cleaner, id, env);
    }
};

OBL_TEST_CASE("staff_call_cleaner.visibility_time_and_forwarded_helpers")
{
    Rig rig;
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.cleaner->m_visible, 1);
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.cleaner->m_visible, 0);
    rig.Run(0x74);
    OBL_CHECK_EQ(rig.cleaner->m_lastTaskTime, 12.5f);
    rig.cleaner->m_BinPos[0] = 3;
    rig.cleaner->m_BinPos[1] = 4;
    const struct {
        int32_t id;
        const char* log;
    } cases[] = {
            {0x70, "tobin(40,3,4) "},
            {0x71, "same(40) "},
            {0x75, "ifpresent(40) "},
            {0x77, "didnot(40) "},
            {0x78, "startedclean(40) "},
            {0x7b, "thought(20000000) "},
    };
    for (const auto& c : cases) {
        rig.Run(c.id);
        OBL_CHECK_EQ(rig.env.log, std::string(c.log));
    }
}

OBL_TEST_CASE("staff_call_cleaner.an_urgent_job_is_remembered_only_when_found")
{
    Rig rig;
    rig.env.findJobResult = 9;
    rig.env.findJobType = 2;
    rig.Run(0x6e);
    OBL_CHECK_EQ(rig.env.log, std::string("findjob(urgent) "));
    OBL_CHECK_EQ(rig.cleaner->m_DestObjectID, 9);
    OBL_CHECK_EQ(rig.cleaner->m_urgentClean, 1);
    OBL_CHECK_EQ(rig.cleaner->m_UrgentType, 2);
    Rig none;
    none.Run(0x6e);
    OBL_CHECK_EQ(none.cleaner->m_DestObjectID, -1);
    OBL_CHECK_EQ(none.cleaner->m_urgentClean, 0);
}

OBL_TEST_CASE("staff_call_cleaner.abandoned_litter_is_cancelled_and_marked_for_removal")
{
    Rig rig;
    rig.Run(0x6f);
    OBL_CHECK_EQ(rig.env.log, std::string("cancel(40) object(40) post moving(0) called(0) thought(40) "));
    OBL_CHECK_EQ(rig.env.posted.sender, 40);
    OBL_CHECK_EQ(rig.env.posted.receiver, 40);
    OBL_CHECK_EQ(rig.env.posted.type, 3);
    OBL_CHECK_EQ(rig.env.posted.data, 3);
    OBL_CHECK_EQ(rig.target->m_waitingForDeath, 1);
    OBL_CHECK_EQ(rig.cleaner->m_LitterToday, 1);
    rig.cleaner->m_DestObjectID = 99; // already gone: only the bookkeeping remains
    rig.Run(0x6f);
    OBL_CHECK_EQ(rig.env.log, std::string("cancel(99) object(99) thought(40) "));
    OBL_CHECK_EQ(rig.cleaner->m_LitterToday, 2);
}

OBL_TEST_CASE("staff_call_cleaner.an_emptied_bin_is_released")
{
    Rig rig;
    rig.Run(0x72);
    OBL_CHECK_EQ(rig.env.log, std::string("object(40) claimed(0) moving(0) called(0) binfill(0) thought(8) cancel(40) "));
    OBL_CHECK_EQ(rig.cleaner->m_BinsToday, 1);
}

OBL_TEST_CASE("staff_call_cleaner.a_toilet_busy_flag_is_set_from_the_third_cleaning")
{
    Rig rig;
    rig.target->m_VisitsSinceCleaned = 5;
    rig.cleaner->m_RequiredFeedback = 6;
    rig.cleaner->m_FeedbackStatus = 7;
    rig.Run(0x73);
    OBL_CHECK_EQ(rig.target->m_NumTimesCleanedToday, 1);
    OBL_CHECK_EQ(rig.target->m_VisitsSinceCleaned, 0);
    OBL_CHECK_EQ(rig.cleaner->m_BusyToilet, 0);
    OBL_CHECK_EQ(rig.cleaner->m_RequiredFeedback, 6);
    OBL_CHECK_EQ(rig.env.log, std::string("object(40) claimed(0) moving(0) called(0) thought(10) cancel(40) "));
    rig.Run(0x73);
    OBL_CHECK_EQ(rig.cleaner->m_BusyToilet, 0);
    rig.Run(0x73);
    OBL_CHECK_EQ(rig.target->m_NumTimesCleanedToday, 3);
    OBL_CHECK_EQ(rig.cleaner->m_BusyToilet, 40);
    OBL_CHECK_EQ(rig.cleaner->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.cleaner->m_FeedbackStatus, 5);
    OBL_CHECK_EQ(rig.cleaner->m_ToiletsToday, 3);
}

OBL_TEST_CASE("staff_call_cleaner.the_cleaning_delay_follows_the_visits_clamped_to_two_to_fifteen")
{
    Rig rig;
    const struct {
        int32_t visits;
        float delay;
    } cases[] = {{0, 2}, {5, 2}, {6, 3}, {20, 10}, {30, 15}, {1000, 15}};
    for (const auto& c : cases) {
        rig.target->m_VisitsSinceCleaned = c.visits;
        rig.Run(0x7c);
        OBL_CHECK_EQ(rig.env.postedDelay, c.delay);
    }
    OBL_CHECK_EQ(rig.env.posted.sender, 31);
    OBL_CHECK_EQ(rig.env.posted.receiver, 31);
    OBL_CHECK_EQ(rig.env.posted.type, 0x39);
    OBL_CHECK_EQ(rig.env.posted.extra, 0x73);
}

OBL_TEST_CASE("staff_call_cleaner.the_job_search_sets_one_found_flag_by_type")
{
    Rig rig;
    rig.cleaner->m_litterFound = rig.cleaner->m_binFound = rig.cleaner->m_toiletFound = 1;
    rig.env.findJobResult = -1;
    rig.Run(0x7d);
    OBL_CHECK_EQ(rig.env.log, std::string("findjob(any) "));
    OBL_CHECK_EQ(rig.cleaner->m_litterFound + rig.cleaner->m_binFound + rig.cleaner->m_toiletFound, 0);
    OBL_CHECK_EQ(rig.cleaner->m_DestObjectID, -1);
    rig.env.findJobResult = 7;
    rig.env.findJobType = 1;
    rig.Run(0x7d);
    OBL_CHECK_EQ(rig.cleaner->m_binFound, 1);
    OBL_CHECK_EQ(rig.cleaner->m_litterFound, 0);
    OBL_CHECK_EQ(rig.cleaner->m_lastTaskTime, 12.5f);
    rig.env.findJobType = 2;
    rig.Run(0x7d);
    OBL_CHECK_EQ(rig.cleaner->m_toiletFound, 1);
    OBL_CHECK_EQ(rig.cleaner->m_binFound, 0);
    rig.env.findJobType = 0;
    rig.Run(0x7d);
    OBL_CHECK_EQ(rig.cleaner->m_litterFound, 1);
    rig.env.findJobType = 5; // unknown type: job kept, no flag
    rig.Run(0x7d);
    OBL_CHECK_EQ(rig.cleaner->m_litterFound + rig.cleaner->m_binFound + rig.cleaner->m_toiletFound, 0);
    OBL_CHECK_EQ(rig.cleaner->m_DestObjectID, 7);
}

OBL_TEST_CASE("staff_call_cleaner.select_and_feedback")
{
    Rig rig;
    rig.cleaner->m_RequiredFeedback = 5;
    rig.cleaner->m_FeedbackStatus = 7;
    rig.Run(0xad);
    OBL_CHECK_EQ(rig.cleaner->m_FeedbackStatus, 3);
    OBL_CHECK_EQ(rig.env.log, std::string("base(173) "));
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.cleaner->m_waitingForDeath, 1);
    rig.Run(0xaf);
    OBL_CHECK_EQ(rig.env.log, std::string("blink(CleanerIcon,1) "));
    rig.cleaner->m_RequiredFeedback = 2;
    rig.Run(0xb0);
    OBL_CHECK_EQ(rig.env.log, std::string("select tab(CleanerDialog,2) thought(2) blink(CleanerIcon,0) "));
    rig.cleaner->m_RequiredFeedback = 8;
    rig.env.feedbackKey = "C";
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(8) feedback(C,31,12) thought(8) blink(CleanerIcon,0) "));
}

OBL_TEST_CASE("staff_call_cleaner.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0x76, 0x79, 0xa8, 0}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
