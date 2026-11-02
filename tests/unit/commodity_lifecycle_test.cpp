#include "unit_test.h"
#include "game/tables/commodity_providers.h"

OBL_TEST_CASE("commodity.a_new_provider_set_is_empty_and_unsigned")
{
    obl::commodity::Providers p;
    OBL_CHECK_EQ(p.buildingCount, 0);
    OBL_CHECK(!p.hasSignature);
    for (int family = 0; family < obl::commodity::kFamilies; ++family) {
        OBL_CHECK_EQ(p.Count(family), 0);
        OBL_CHECK_EQ(p.complaints[family], 0u);
    }
}

OBL_TEST_CASE("commodity.count_follows_the_records_and_invalidate_forces_a_rebuild")
{
    obl::commodity::Providers p;
    p.records[3].push_back({7, 1, 10, 20});
    p.records[3].push_back({9, 0, 11, 21});
    OBL_CHECK_EQ(p.Count(3), 2);
    OBL_CHECK_EQ(p.Count(4), 0);
    p.buildingCount = 12;
    p.Invalidate();
    OBL_CHECK_EQ(p.buildingCount, -1);
}
