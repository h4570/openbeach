// CleaningManager life cycle (src/game/cleaning/cleaning_lifecycle.h) on the native manager.

#include "unit_test.h"

#include "game/cleaning/cleaning_lifecycle.h"

using namespace obl::cleaning;

OBL_TEST_CASE("cleaning_lifecycle.a_new_manager_has_two_empty_lists")
{
    Manager manager;
    OBL_CHECK(manager.jobs.empty());
    OBL_CHECK(manager.activeJobs.empty());
    OBL_CHECK_EQ(manager.cleaningCount, 0);
    OBL_CHECK_EQ(manager.activeCount, 0);
}
