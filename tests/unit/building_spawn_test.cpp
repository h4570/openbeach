#include "unit_test.h"

#include "game/objects/building_spawn.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace obl;

namespace {

// A grid where direction 6 is +1 in X and direction 0 is +1 in Y; every call is logged.
struct FakeEnv {
    Tile origin{10, 20};
    std::vector<std::string> log;
    std::vector<PlaceCommand> placed;

    Tile BuildingTile() { return origin; }
    Tile Neighbour(Tile t, int direction)
    {
        log.push_back("n" + std::to_string(direction));
        if (direction == 6)
            ++t.x;
        else if (direction == 0)
            ++t.y;
        return t;
    }
    Point TileCenter(Tile t) { return {static_cast<float>(t.x) + 0.5f, static_cast<float>(t.y) + 0.25f}; }
    Point FootprintPoint(Tile t, int w, int h, float ox, float oy, uint32_t rot)
    {
        return {static_cast<float>(t.x * 100 + w * 10 + rot) + ox, static_cast<float>(t.y * 100 + h * 10) + oy};
    }
    const char* VehicleKey(int id) { return id == 7 ? "BOAT" : "CAR"; }
    const char* TableKey(int id) { return id == 3 ? "TABLE3" : "TABLE0"; }
    void Place(const PlaceCommand& c) { placed.push_back(c); }
    std::vector<int32_t> females;
    std::vector<int32_t> left;
    bool StaffIsFemale(int32_t id)
    {
        log.push_back("lookup" + std::to_string(id));
        return std::find(females.begin(), females.end(), id) != females.end();
    }
    void Leave(int32_t id) { left.push_back(id); }
    // staff definitions by id: the type and key; builder numbers by staff id; the rand values in order
    std::vector<StaffInfo> definitions = {{"SERVE", "SRV"}, {"LIFEGUARD", "LG"}, {"MECHANIC", "MECH"}, {"REP", "REPKEY"},
            {"BUILDER", "BLD"}, {"CLEANER", "CLN"}, {"SECURITYGUARD", "SEC"}, {"DJ", "DJKEY"}};
    std::vector<int> builderNumber = std::vector<int>(100, 0);
    std::vector<int> randoms;
    size_t nextRandom = 0;
    StaffInfo StaffDefinition(int id) { return definitions[id]; }
    int BuilderSlot(int32_t id) { return builderNumber[id]; }
    int Rand() { return randoms[nextRandom++]; }
};

SpawnSite VehicleSite(uint32_t rotation, int ox, int oy)
{
    SpawnSite site;
    site.owner = 55;
    site.rotation = rotation;
    site.width = 4;
    site.height = 3;
    site.numVehicles = 1;
    site.vehicle[0] = 7;
    site.vehicleOffset[0][0] = ox;
    site.vehicleOffset[0][1] = oy;
    site.vehicleDir[0] = 3;
    return site;
}

} // namespace

OBL_TEST_CASE("building_spawn.vehicle_tile_by_rotation")
{
    {
        FakeEnv env;
        const Tile t = VehicleTile(env, VehicleSite(0, 2, 1), 0);
        OBL_CHECK_EQ(t.x, 12u);
        OBL_CHECK_EQ(t.y, 21u);
    }
    {
        FakeEnv env; // rotation 1: (4-1)-1 = 2 steps in Y (direction 0), then oy = 2 steps in X (direction 6)
        const Tile t = VehicleTile(env, VehicleSite(1, 1, 2), 0);
        OBL_CHECK_EQ(t.x, 12u);
        OBL_CHECK_EQ(t.y, 22u);
    }
    {
        FakeEnv env; // rotation 2: (4-1)-1 = 2 in X, (3-1)-1 = 1 in Y
        const Tile t = VehicleTile(env, VehicleSite(2, 1, 1), 0);
        OBL_CHECK_EQ(t.x, 12u);
        OBL_CHECK_EQ(t.y, 21u);
    }
    {
        FakeEnv env; // rotation 3: ox = 2 in Y (direction 0), (3-1)-1 = 1 in X (direction 6)
        const Tile t = VehicleTile(env, VehicleSite(3, 2, 1), 0);
        OBL_CHECK_EQ(t.x, 11u);
        OBL_CHECK_EQ(t.y, 22u);
    }
}

OBL_TEST_CASE("building_spawn.vehicle_step_counts_never_negative")
{
    FakeEnv env;
    const Tile t = VehicleTile(env, VehicleSite(2, 9, 7), 0);
    OBL_CHECK_EQ(t.x, 10u);
    OBL_CHECK_EQ(t.y, 20u);
    OBL_CHECK(env.log.empty());
    FakeEnv one; // n == 1 is the same
    VehicleTile(one, VehicleSite(1, 3, 0), 0);
    OBL_CHECK(one.log.empty());
    FakeEnv bad; // rotation outside 0..3: stays on the building's tile
    const Tile u = VehicleTile(bad, VehicleSite(4, 1, 1), 0);
    OBL_CHECK_EQ(u.x, 10u);
    OBL_CHECK_EQ(u.y, 20u);
}

OBL_TEST_CASE("building_spawn.vehicle_rotation_is_signed_remainder")
{
    // (dir + (4 - rotation) * 2) % 8 with C semantics: negative sums keep their sign.
    SpawnSite site = VehicleSite(0, 0, 0);
    site.vehicleDir[0] = 3;
    OBL_CHECK_EQ(VehicleRotation(site, 0), (3 + 8) % 8);
    site.rotation = 3;
    OBL_CHECK_EQ(VehicleRotation(site, 0), 5);
    site.vehicleDir[0] = -3;
    site.rotation = 3;
    OBL_CHECK_EQ(VehicleRotation(site, 0), -1);
    site.vehicleDir[0] = -8;
    site.rotation = 4;
    OBL_CHECK_EQ(VehicleRotation(site, 0), 0);
    site.vehicleDir[0] = 7;
    site.rotation = 1;
    OBL_CHECK_EQ(VehicleRotation(site, 0), 5); // 7 + 6 = 13 % 8
}

OBL_TEST_CASE("building_spawn.spawn_vehicles_posts_one_command_each")
{
    FakeEnv env;
    SpawnSite site = VehicleSite(0, 1, 0);
    site.numVehicles = 2;
    site.vehicle[1] = 1;
    site.vehicleOffset[1][0] = 0;
    site.vehicleOffset[1][1] = 2;
    SpawnVehicles(env, site);
    OBL_REQUIRE(env.placed.size() == 2);
    OBL_CHECK_EQ(env.placed[0].kind, kPlaceVehicle);
    OBL_CHECK_EQ(std::string(env.placed[0].key), "BOAT");
    OBL_CHECK_EQ(env.placed[0].x, 11.5f);
    OBL_CHECK_EQ(env.placed[0].y, 20.25f);
    OBL_CHECK_EQ(env.placed[0].owner, 55);
    OBL_CHECK(!env.placed[0].zWritten);
    OBL_CHECK_EQ(std::string(env.placed[1].key), "CAR");
    OBL_CHECK_EQ(env.placed[1].y, 22.25f);
}

OBL_TEST_CASE("building_spawn.no_vehicles_no_commands")
{
    FakeEnv env;
    SpawnSite site;
    SpawnVehicles(env, site);
    SpawnTables(env, site);
    OBL_CHECK(env.placed.empty());
}

OBL_TEST_CASE("building_spawn.spawn_tables")
{
    FakeEnv env;
    SpawnSite site;
    site.owner = 9;
    site.rotation = 2;
    site.width = 3;
    site.height = 2;
    site.numTables = 2;
    site.table[0] = 3;
    site.table[1] = 0;
    site.tableOffset[0][0] = 0.5f;
    site.tableOffset[0][1] = 1.5f;
    site.tableOffset[1][0] = 2.0f;
    site.tableOffset[1][1] = 3.0f;
    SpawnTables(env, site);
    OBL_REQUIRE(env.placed.size() == 2);
    OBL_CHECK_EQ(env.placed[0].kind, kPlaceTable);
    OBL_CHECK_EQ(std::string(env.placed[0].key), "TABLE3");
    OBL_CHECK_EQ(env.placed[0].x, 1000 + 30 + 2 + 0.5f);
    OBL_CHECK_EQ(env.placed[0].y, 2000 + 20 + 1.5f);
    OBL_CHECK_EQ(env.placed[0].z, 0.0f);
    OBL_CHECK(env.placed[0].zWritten);
    OBL_CHECK_EQ(env.placed[0].owner, 9);
    OBL_CHECK_EQ(env.placed[0].rotation, 2);
    OBL_CHECK_EQ(std::string(env.placed[1].key), "TABLE0");
    OBL_CHECK_EQ(env.placed[1].x, 1032 + 2.0f);
}

OBL_TEST_CASE("building_spawn.long_key_is_terminated")
{
    struct LongEnv : FakeEnv {
        std::string longKey = std::string(200, 'k');
        const char* TableKey(int) { return longKey.c_str(); }
    } env;
    SpawnSite site;
    site.numTables = 1;
    SpawnTables(env, site);
    OBL_REQUIRE(env.placed.size() == 1);
    OBL_CHECK_EQ(std::string(env.placed[0].key).size(), 63u);
}

OBL_TEST_CASE("building_spawn.dismiss_moves_the_rest_down")
{
    FakeEnv env;
    int32_t staff[10] = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};
    int32_t males = 3, females = 2;
    const DismissResult r = DismissStaff(env, staff, males, females, 2);
    OBL_CHECK(r.found);
    OBL_CHECK(!r.female);
    OBL_CHECK_EQ(males, 2);
    OBL_CHECK_EQ(females, 2);
    OBL_CHECK_EQ(staff[0], 1);
    OBL_CHECK_EQ(staff[1], 3);
    OBL_CHECK_EQ(staff[2], 4);
    OBL_CHECK_EQ(staff[3], 5);
    OBL_CHECK_EQ(staff[4], 5);
    OBL_CHECK(env.left == std::vector<int32_t>{2});
}

OBL_TEST_CASE("building_spawn.dismiss_female_and_last")
{
    FakeEnv env;
    env.females = {5};
    int32_t staff[10] = {1, 2, 3, 4, 5, 0, 0, 0, 0, 0};
    int32_t males = 3, females = 2;
    const DismissResult r = DismissStaff(env, staff, males, females, 5);
    OBL_CHECK(r.found);
    OBL_CHECK(r.female);
    OBL_CHECK_EQ(females, 1);
    OBL_CHECK_EQ(males, 3);
    OBL_CHECK_EQ(staff[4], 5);
    OBL_CHECK(env.left == std::vector<int32_t>{5});
}

OBL_TEST_CASE("building_spawn.dismiss_unrecognized_changes_nothing")
{
    FakeEnv env;
    int32_t staff[10] = {1, 2, 3, 0, 0, 0, 0, 0, 0, 9};
    int32_t males = 2, females = 1;
    OBL_CHECK(!DismissStaff(env, staff, males, females, 9).found); // beyond the count
    OBL_CHECK_EQ(males, 2);
    OBL_CHECK_EQ(females, 1);
    OBL_CHECK_EQ(staff[0], 1);
    int32_t none[10] = {};
    int32_t zero = 0, zero2 = 0;
    OBL_CHECK(!DismissStaff(env, none, zero, zero2, 0).found);
    OBL_CHECK(env.log.empty()); // the object is not looked up and not told to leave
    OBL_CHECK(env.left.empty());
}

namespace {

StaffSite StaffSiteAt()
{
    StaffSite site;
    site.owner = 77;
    site.rotation = 2;
    site.width = 3;
    site.height = 2;
    site.servePoint[0] = 100.0f;
    site.servePoint[1] = 200.0f;
    site.servePoint[2] = 5.0f;
    site.maxStaff = 4;
    site.staffOffset[0] = 2;
    site.staffOffset[1] = -1;
    std::strcpy(site.maleKey, "MALE");
    std::strcpy(site.femaleKey, "FEMALE");
    return site;
}

} // namespace

OBL_TEST_CASE("building_spawn.staff_offsets")
{
    const float expected[10][2] = {{0, 0}, {30, 0}, {0, 30}, {-30, 0}, {30, 30}, {-30, -30}, {-15, -25}, {-20, 25}, {25, 45}, {-50, -15}};
    for (int i = 0; i < 10; ++i) {
        float x = 0, y = 0;
        ApplyStaffOffset(i, x, y);
        OBL_CHECK_EQ(x, expected[i][0]);
        OBL_CHECK_EQ(y, expected[i][1]);
    }
    float x = 1, y = 1;
    ApplyStaffOffset(10, x, y); // outside the table
    ApplyStaffOffset(-1, x, y);
    OBL_CHECK_EQ(x, 1.0f);
    OBL_CHECK_EQ(y, 1.0f);
}

OBL_TEST_CASE("building_spawn.hire_refuses_when_full_or_unrecognized")
{
    FakeEnv env;
    StaffSite site = StaffSiteAt();
    site.initial[0] = 5;
    site.males = 3;
    site.females = 1; // 4 == the maximum
    OBL_CHECK(!HireStaff(env, site));
    site.females = 0;
    site.initial[0] = 0; // SERVE is not hireable
    OBL_CHECK(!HireStaff(env, site));
    site.initial[0] = 7; // unrecognized type
    OBL_CHECK(!HireStaff(env, site));
    OBL_CHECK(env.placed.empty());
    site.males = 5; // over the maximum: only an exact match refuses
    site.initial[0] = 5;
    OBL_CHECK(HireStaff(env, site));
}

OBL_TEST_CASE("building_spawn.hire_kinds_and_keys")
{
    const int kinds[][2] = {{1, kPlaceLifeguard}, {2, kPlaceMechanic}, {3, kPlaceRepairman}, {5, kPlaceCleaner}, {6, kPlaceSecurityGuard}};
    for (const auto& entry : kinds) {
        FakeEnv env;
        StaffSite site = StaffSiteAt();
        site.initial[0] = entry[0];
        site.males = 2;
        site.females = 1; // more men than women: the male key
        OBL_CHECK(HireStaff(env, site));
        OBL_CHECK_EQ(env.placed.size(), static_cast<size_t>(1));
        const PlaceCommand& command = env.placed[0];
        OBL_CHECK_EQ(command.kind, entry[1]);
        OBL_CHECK(std::string(command.key) == "MALE");
        OBL_CHECK_EQ(command.x, 100.0f);
        OBL_CHECK_EQ(command.y, 200.0f);
        OBL_CHECK_EQ(command.z, 5.0f);
        OBL_CHECK_EQ(command.owner, 77);
        OBL_CHECK_EQ(command.rotation, 3); // the count: nobody looked at builder numbers
        site.males = 1;
        site.females = 1; // equal: the female key
        FakeEnv equal;
        OBL_CHECK(HireStaff(equal, site));
        OBL_CHECK(std::string(equal.placed[0].key) == "FEMALE");
    }
}

OBL_TEST_CASE("building_spawn.hire_builder_takes_the_lowest_free_number")
{
    FakeEnv env;
    StaffSite site = StaffSiteAt();
    site.initial[0] = 4;
    site.maxStaff = 10;
    site.males = 3;
    site.staff[0] = 11;
    site.staff[1] = 12;
    site.staff[2] = 13;
    env.builderNumber[11] = 0;
    env.builderNumber[12] = 2;
    env.builderNumber[13] = 3;
    OBL_CHECK(HireStaff(env, site));
    OBL_CHECK_EQ(env.placed[0].kind, kPlaceBuilder);
    OBL_CHECK_EQ(env.placed[0].rotation, 1);
    OBL_CHECK_EQ(env.placed[0].x, 130.0f); // offset 1: +30 in X
    OBL_CHECK_EQ(env.placed[0].y, 200.0f);
    // all of 0..2 taken: the count (3) is the number, offset 3: -30 in X
    FakeEnv full;
    full.builderNumber[11] = 0;
    full.builderNumber[12] = 1;
    full.builderNumber[13] = 2;
    OBL_CHECK(HireStaff(full, site));
    OBL_CHECK_EQ(full.placed[0].rotation, 3);
    OBL_CHECK_EQ(full.placed[0].x, 70.0f);
    // a builder number outside the list is ignored
    FakeEnv far;
    far.builderNumber[11] = 77;
    far.builderNumber[12] = -4;
    far.builderNumber[13] = 50;
    OBL_CHECK(HireStaff(far, site));
    OBL_CHECK_EQ(far.placed[0].rotation, 0);
    // the first builder of a building
    FakeEnv none;
    StaffSite empty = StaffSiteAt();
    empty.initial[0] = 4;
    OBL_CHECK(HireStaff(none, empty));
    OBL_CHECK_EQ(none.placed[0].rotation, 0);
    OBL_CHECK_EQ(none.placed[0].x, 100.0f);
}

OBL_TEST_CASE("building_spawn.initial_staff_by_type")
{
    FakeEnv env;
    env.randoms = {4, 3};
    StaffSite site = StaffSiteAt();
    site.numInitial = 8;
    for (int i = 0; i < 8; ++i) site.initial[i] = i;
    SpawnInitialStaff(env, site);
    // SERVE, LIFEGUARD (even -> female key), MECHANIC, REP (odd -> male key), BUILDER (i = 4), CLEANER, SECURITYGUARD; DJ (7) is skipped
    OBL_CHECK_EQ(env.placed.size(), static_cast<size_t>(7));
    OBL_CHECK_EQ(env.nextRandom, static_cast<size_t>(2));
    OBL_CHECK_EQ(env.placed[0].kind, kPlaceServe);
    OBL_CHECK(std::string(env.placed[0].key) == "SRV");
    OBL_CHECK_EQ(
            env.placed[0].x, static_cast<float>(10 * 100 + 3 * 10 + 2) + 2.0f); // FootprintPoint of the fake: tile, size, rotation, offset
    OBL_CHECK_EQ(env.placed[0].y, static_cast<float>(20 * 100 + 2 * 10) - 1.0f);
    OBL_CHECK(!env.placed[0].zWritten);
    OBL_CHECK(!env.placed[0].rotationWritten);
    OBL_CHECK_EQ(env.placed[1].kind, kPlaceLifeguard);
    OBL_CHECK(std::string(env.placed[1].key) == "FEMALE");
    OBL_CHECK_EQ(env.placed[1].x, 100.0f);
    OBL_CHECK_EQ(env.placed[1].z, 5.0f);
    OBL_CHECK_EQ(env.placed[2].kind, kPlaceMechanic);
    OBL_CHECK(std::string(env.placed[2].key) == "MECH");
    OBL_CHECK_EQ(env.placed[3].kind, kPlaceRepairman);
    OBL_CHECK(std::string(env.placed[3].key) == "MALE");
    OBL_CHECK(!env.placed[1].rotationWritten);
    OBL_CHECK_EQ(env.placed[4].kind, kPlaceBuilder);
    OBL_CHECK(std::string(env.placed[4].key) == "BLD");
    OBL_CHECK_EQ(env.placed[4].rotation, 4); // the position in the definition's list
    OBL_CHECK(env.placed[4].rotationWritten);
    OBL_CHECK_EQ(env.placed[4].x, 130.0f);
    OBL_CHECK_EQ(env.placed[4].y, 230.0f); // offset 4: +30 in both
    OBL_CHECK_EQ(env.placed[5].kind, kPlaceCleaner);
    OBL_CHECK(std::string(env.placed[5].key) == "CLN");
    OBL_CHECK_EQ(env.placed[6].kind, kPlaceSecurityGuard);
    OBL_CHECK(std::string(env.placed[6].key) == "SEC");
    for (const PlaceCommand& command : env.placed) OBL_CHECK_EQ(command.owner, 77);
}

OBL_TEST_CASE("building_spawn.initial_staff_count_is_capped")
{
    FakeEnv env;
    StaffSite site = StaffSiteAt();
    site.numInitial = 25; // the definition's list holds ten
    for (int& id : site.initial) id = 4; // BUILDER: the position in the list is visible
    SpawnInitialStaff(env, site);
    OBL_CHECK_EQ(env.placed.size(), static_cast<size_t>(10));
    OBL_CHECK_EQ(env.placed[9].rotation, 9);
    OBL_CHECK_EQ(env.placed[9].x, 50.0f); // offset 9: -50 in X
    FakeEnv none;
    site.numInitial = 0;
    SpawnInitialStaff(none, site);
    OBL_CHECK(none.placed.empty());
}
