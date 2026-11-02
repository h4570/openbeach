#include "unit_test.h"
#include "game/world/world_snapshot.h"

#include <bit>

using obl::WorldSnapshot;

OBL_TEST_CASE("world_snapshot.names_hash_in_their_key_spelling")
{
    auto hash = [](const char* name) {
        WorldSnapshot s;
        s.Class("ObjectManager");
        s.String("base.name", name);
        return s.Hash();
    };
    OBL_CHECK_EQ(hash("Palm 4s"), hash("palm_4s"));
    OBL_CHECK(hash("Palm 4s") != hash("palm_5s"));
}

OBL_TEST_CASE("world_snapshot.v2_uninitialized_fields_and_rng")
{
    auto fixture = [](uint32_t garbage, uint64_t calls, uint32_t job) {
        WorldSnapshot s;
        s.Class("RepairManager");
        s.Add("distance", 3, &garbage, 4);
        s.Add("ObjectID", 4, &job, 4);
        s.Class("ObjectManager");
        s.Add("staff.hours_worked", 4, &garbage, 4);
        s.Add("vehicle.ride_time", 3, &garbage, 4);
        s.Add("staff.wage", 3, &job, 4);
        // Same names in unrelated sections remain observable.
        s.Class("Other");
        s.Add("distance", 3, &job, 4);
        s.Add("staff.hours_worked", 4, &job, 4);
        s.Normalize();
        s.Class("RNG");
        s.Add("rand-calls", 14, &calls, 8);
        return s;
    };
    auto a = fixture(0xdeadbeef, 0x100000002ull, 7);
    OBL_CHECK_EQ(a.Hash(), fixture(0xbaadf00d, 0x100000002ull, 7).Hash());
    OBL_CHECK(a.Hash() != fixture(0xdeadbeef, 2, 7).Hash());
    OBL_CHECK(a.Hash() != fixture(0xdeadbeef, 0x100000002ull, 8).Hash());
    OBL_CHECK(a.Text().starts_with("OBL-world-v3\n"));
    OBL_CHECK(a.Text().find("RepairManager\tfDistance") == std::string::npos);
    OBL_CHECK(a.Text().find("ObjectManager\tstaff.hours_worked") == std::string::npos);
    OBL_CHECK(a.Text().find("Other\tdistance") != std::string::npos);
    OBL_CHECK(a.Text().find("Other\tstaff.hours_worked") != std::string::npos);
    OBL_CHECK(a.Text().find("RNG\trand-calls\t14\t0200000001000000") != std::string::npos);
}

OBL_TEST_CASE("world_snapshot.mechanic_action_state_left_out")
{
    // Only the script writes the mechanic's action state, so a fresh mechanic holds heap garbage: its record is not part of the snapshot.
    auto fixture = [](const char* name, uint32_t state) {
        WorldSnapshot s;
        s.Class("ObjectManager");
        s.Add(name, 4, &state, 4);
        s.Normalize();
        return s;
    };
    OBL_CHECK_EQ(fixture("mechanic.action_state", 0x0d3b2245).Hash(), fixture("mechanic.action_state", 0).Hash());
    OBL_CHECK(fixture("mechanic.action_state", 0).Text().find("action_state") == std::string::npos);
    // A guest's action state stays observable.
    OBL_CHECK(fixture("guest.action_state", 5).Hash() != fixture("guest.action_state", 0).Hash());
}

OBL_TEST_CASE("world_snapshot.order_names_types_and_boundaries")
{
    // Schema 3 hashes order, types, bytes and boundaries but not the names: a renamed record keeps the hash.
    uint32_t v = 0x12345678;
    uint32_t w = 0x9abcdef0;
    WorldSnapshot a, same, renamed, retyped, reordered;
    for (auto* s : {&a, &same, &renamed, &retyped, &reordered}) s->Class("Class name : World");
    a.Add("a", 4, &v, 4);
    a.Add("b", 4, &w, 4);
    same.Add("a", 4, &v, 4);
    same.Add("b", 4, &w, 4);
    renamed.Add("x", 4, &v, 4);
    renamed.Add("b", 4, &w, 4);
    retyped.Add("a", 3, &v, 4);
    retyped.Add("b", 4, &w, 4);
    reordered.Add("b", 4, &w, 4);
    reordered.Add("a", 4, &v, 4);
    OBL_CHECK_EQ(a.Hash(), same.Hash());
    OBL_CHECK_EQ(a.Hash(), renamed.Hash());
    OBL_CHECK(a.Hash() != retyped.Hash());
    OBL_CHECK(a.Hash() != reordered.Hash());
    OBL_CHECK(a.Text().find("World\ta\t4\t78563412\n") != std::string::npos);
    WorldSnapshot empty;
    empty.Add("empty", 1, nullptr, 0);
    OBL_CHECK_EQ(empty.Count(), size_t(1));
}

OBL_TEST_CASE("world_snapshot.floats_are_bit_exact")
{
    // Even one ULP and the sign of zero are observable; there is no tolerance/rounding in this checksum.
    for (uint32_t bits : {0u, 0x3f800000u}) {
        WorldSnapshot a, b;
        float x = std::bit_cast<float>(bits), y = std::bit_cast<float>(bits + 1);
        a.Add("x", 3, &x, 4);
        b.Add("x", 3, &y, 4);
        OBL_CHECK(a.Hash() != b.Hash());
    }
    WorldSnapshot positive, negative;
    float p = 0.0f, n = -0.0f;
    positive.Add("zero", 3, &p, 4);
    negative.Add("zero", 3, &n, 4);
    OBL_CHECK(positive.Hash() != negative.Hash());
}

OBL_TEST_CASE("world_snapshot.exclusions_are_scoped")
{
    uint32_t v = 42;
    WorldSnapshot s;
    s.Class("Class name : World");
    s.Add("profit_history", 11, &v, 4);
    s.Add("income_history", 11, &v, 4);
    s.Add("cost_history", 11, &v, 4);
    s.Add("game_time", 3, &v, 4);
    s.Class("Class name : ZEngine");
    s.Add("camera", 3, &v, 4);
    s.Class("Class name : Player");
    s.Add("cost_history", 3, &v, 4);
    s.Normalize();
    OBL_CHECK_EQ(s.Count(), size_t(4));
    OBL_CHECK(s.Text().find("World\tgame_time") != std::string::npos);
    OBL_CHECK(s.Text().find("Player\tcost_history") != std::string::npos);
    OBL_CHECK(s.Text().find("ZEngine") == std::string::npos);
}

OBL_TEST_CASE("world_snapshot.daily_ferry_day_only")
{
    uint32_t day = 0xdeadbeef, daily = 1, once = 0;
    WorldSnapshot s;
    s.Class("Class name : FerryManager");
    s.Add("day", 4, &day, 4);
    s.Add("time_flags", 4, &daily, 4);
    s.Add("day", 4, &day, 4);
    s.Add("time_flags", 4, &once, 4);
    s.Normalize();
    OBL_CHECK(s.Text().find("FerryManager\tday\t4\t00000000\n") != std::string::npos);
    OBL_CHECK(s.Text().find("FerryManager\tday\t4\tefbeadde\n") != std::string::npos);
    auto onceNormalized = s.Hash();
    s.Normalize();
    OBL_CHECK_EQ(s.Hash(), onceNormalized);
}

// Weather's saved fields appear under the ConstructionManager section; three of them are heap garbage until the game writes them.
OBL_TEST_CASE("world_snapshot.weather_fields_count_once_meaningful")
{
    struct Weather {
        uint8_t flash;
        uint32_t flashTime, strikeTimeBits, point, area;
        uint8_t strikes;
    };
    auto fixture = [](const Weather& w) {
        uint32_t index = 3, zero = 0;
        WorldSnapshot s;
        s.Class("Class name : ConstructionManager");
        s.Add("light_index", 6, &index, 4);
        s.Add("light_flash", 2, &w.flash, 1);
        s.Add("flash_time", 3, &w.flashTime, 4);
        s.Add("strike_time", 3, &w.strikeTimeBits, 4);
        s.Add("strike_area.top", 6, &w.area, 4);
        s.Add("strike_point.x", 3, &w.point, 4);
        s.Add("strike_point.z", 3, &zero, 4);
        s.Add("strikes", 2, &w.strikes, 1);
        s.Add("current_day_offset", 4, &zero, 4);
        s.Normalize();
        return s;
    };
    // Nothing flashed, armed or striking: all three are garbage and must not matter.
    OBL_CHECK_EQ(fixture({0, 0x08000020, 0, 0xdeadbeef, 0x1234, 0}).Hash(), fixture({0, 0, 0, 0, 0, 0}).Hash());
    // A flash in progress compares its time, an armed strike its point, a striking weather its area.
    OBL_CHECK(fixture({1, 5, 0, 0, 0, 0}).Hash() != fixture({1, 6, 0, 0, 0, 0}).Hash());
    OBL_CHECK(fixture({0, 0, 0x43e10000, 5, 0, 0}).Hash() != fixture({0, 0, 0x43e10000, 6, 0, 0}).Hash());
    OBL_CHECK(fixture({0, 0, 0, 0, 5, 1}).Hash() != fixture({0, 0, 0, 0, 6, 1}).Hash());
    // ...and only those: a flash does not make the point count.
    OBL_CHECK_EQ(fixture({1, 5, 0, 7, 0, 0}).Hash(), fixture({1, 5, 0, 8, 0, 0}).Hash());
    // The same names in another section are untouched.
    uint32_t garbage = 0xdeadbeef, other = 0xcafebabe;
    WorldSnapshot a, b;
    a.Class("Class name : Other");
    a.Add("flash_time", 3, &garbage, 4);
    a.Normalize();
    b.Class("Class name : Other");
    b.Add("flash_time", 3, &other, 4);
    b.Normalize();
    OBL_CHECK(a.Hash() != b.Hash());
    auto once = fixture({0, 1, 0, 1, 1, 0}).Hash();
    auto again = fixture({0, 1, 0, 1, 1, 0});
    again.Normalize();
    OBL_CHECK_EQ(again.Hash(), once);
}

OBL_TEST_CASE("world_snapshot.weather_time_counts_once_scheduled")
{
    // Only an activated schedule entry writes m_WeatherTime; before the first one it is the constructor's heap garbage.
    auto fixture = [](uint32_t timeBits, int32_t nextEvent) {
        uint32_t index = 3;
        WorldSnapshot s;
        s.Class("Class name : ConstructionManager");
        s.Add("light_index", 6, &index, 4);
        s.Add("time", 3, &timeBits, 4);
        s.Add("next_event", 4, &nextEvent, 4);
        s.Normalize();
        return s.Hash();
    };
    OBL_CHECK_EQ(fixture(0x00010000, 0), fixture(0, 0));
    OBL_CHECK(fixture(0x41400000, 1) != fixture(0x41500000, 1));
}

OBL_TEST_CASE("world_snapshot.strike_point_z_never_counts")
{
    // The weather strike routine writes x and y only; z stays the constructor's garbage even with a strike armed.
    auto fixture = [](uint32_t zBits) {
        uint32_t index = 3, armed = 0x43e10000, x = 5;
        WorldSnapshot s;
        s.Class("Class name : ConstructionManager");
        s.Add("light_index", 6, &index, 4);
        s.Add("strike_time", 3, &armed, 4);
        s.Add("strike_point.x", 3, &x, 4);
        s.Add("strike_point.z", 3, &zBits, 4);
        s.Normalize();
        return s.Hash();
    };
    OBL_CHECK_EQ(fixture(0x80000001), fixture(0));
}

OBL_TEST_CASE("world_snapshot.simulation_projection")
{
    uint32_t v = 42;
    WorldSnapshot s;
    s.Class("Class name : ObjectManager");
    for (const char* name : {"base.id", "base.state", "base.position.x", "moving.velocity.y", "guest.desires", "building.power_consumption",
                 "building.queue.guests[]", "building.queue.commodities[]"})
        s.Add(name, 4, &v, 4);
    for (const char* name : {"m_GroupID", "m_Tables", "m_iCurrFrame", "m_fBuildingAnimTime", "not_yet_understood"}) s.Add(name, 4, &v, 4);
    s.Class("Class name : MessageHandler");
    s.Add("time", 3, &v, 4);
    s.Add("extra", 4, &v, 4);
    s.Add("float_data", 3, &v, 4);
    s.Normalize();
    OBL_CHECK_EQ(s.Count(), size_t(11));
    OBL_CHECK(s.Text().find("base.id") != std::string::npos);
    OBL_CHECK(s.Text().find("m_iCurrFrame") == std::string::npos);
    OBL_CHECK(s.Text().find("MessageHandler\ttime\t3") != std::string::npos);
    OBL_CHECK(s.Text().find("MessageHandler\textra") == std::string::npos);
}

OBL_TEST_CASE("world_snapshot.staff_arrays_use_active_prefix")
{
    WorldSnapshot s;
    uint8_t type = 2;
    uint32_t male = 1, female = 1, other = 1;
    uint32_t ids[]{11, 12, 0xdeadbeef};
    uint8_t busy[]{1, 0, 99};
    s.Class("Class name : ObjectManager");
    s.Add("object.type", 9, &type, 1);
    s.Add("building.staff", 7, ids, sizeof(ids));
    s.Add("building.male_staff", 4, &male, 4);
    s.Add("building.female_staff", 4, &female, 4);
    s.Add("building.non_serve_staff_count", 4, &other, 4);
    s.Add("building.staff_busy", 10, busy, sizeof(busy));
    s.Add("building.non_serve_staff", 7, ids, sizeof(ids));
    s.Normalize();
    OBL_CHECK(s.Text().find("building.staff\t7\t0b0000000c000000\n") != std::string::npos);
    OBL_CHECK(s.Text().find("building.staff_busy\t10\t0100\n") != std::string::npos);
    OBL_CHECK(s.Text().find("building.non_serve_staff\t7\t0b000000\n") != std::string::npos);
    // Another building with no staff must not inherit the previous object's counts.
    s.Add("object.type", 9, &type, 1);
    male = female = other = 0;
    s.Add("building.male_staff", 4, &male, 4);
    s.Add("building.female_staff", 4, &female, 4);
    s.Add("building.non_serve_staff_count", 4, &other, 4);
    s.Add("building.staff", 7, ids, sizeof(ids));
    s.Normalize();
    OBL_CHECK(s.Text().find("building.staff\t7\t\n") != std::string::npos);
}

OBL_TEST_CASE("world_snapshot.vehicle_links_require_a_vehicle")
{
    auto fixture = [](uint32_t link) {
        WorldSnapshot s;
        uint8_t vehicle = 4, building = 2, guest = 3;
        uint32_t vehicleId = 17, buildingId = 18;
        s.Class("Class name : ObjectManager");
        s.Add("object.type", 9, &vehicle, 1);
        s.Add("base.id", 4, &vehicleId, 4);
        s.Add("object.type", 9, &building, 1);
        s.Add("base.id", 4, &buildingId, 4);
        s.Add("object.type", 9, &guest, 1);
        s.Add("guest.vehicle", 4, &link, 4);
        s.Normalize();
        return s;
    };
    auto absent = fixture(0xffffffff), garbage = fixture(0xdeadbeef), wrongType = fixture(18), assigned = fixture(17);
    OBL_CHECK_EQ(absent.Hash(), garbage.Hash());
    OBL_CHECK_EQ(absent.Hash(), wrongType.Hash());
    OBL_CHECK(absent.Hash() != assigned.Hash());
    OBL_CHECK(assigned.Text().find("guest.vehicle\t4\t11000000\n") != std::string::npos);
}

// The construction job constructor zeroes only the direction of its ten work points; CollectWorkPoints fills the used ones with a non-zero direction.
OBL_TEST_CASE("world_snapshot.unused_construction_work_points_left_out")
{
    struct Point {
        uint32_t x, y;
        int32_t dir;
    };
    auto fixture = [](const Point& used, const Point& unused) {
        uint32_t id = 17;
        WorldSnapshot s;
        s.Class("Class name : ConstructionManager");
        s.Add("BuildID", 6, &id, 4);
        for (const Point* p : {&used, &unused}) {
            s.Add("work_points[].x", 6, &p->x, 4);
            s.Add("work_points[].y", 6, &p->y, 4);
            s.Add("work_points[].dir", 4, &p->dir, 4);
        }
        s.Normalize();
        return s;
    };
    // An unused point's garbage coordinates do not matter...
    OBL_CHECK_EQ(fixture({100, 200, 3}, {0xbaadf00d, 0xdeadbeef, 0}).Hash(), fixture({100, 200, 3}, {0, 0, 0}).Hash());
    OBL_CHECK(fixture({100, 200, 3}, {0xbaadf00d, 0xdeadbeef, 0}).Text().find("work_points[].x\t6\t00000000\n") != std::string::npos);
    // ...a used point's do, and so does any direction.
    OBL_CHECK(fixture({100, 200, 3}, {0, 0, 0}).Hash() != fixture({101, 200, 3}, {0, 0, 0}).Hash());
    OBL_CHECK(fixture({100, 200, 3}, {0, 0, 0}).Hash() != fixture({100, 201, 3}, {0, 0, 0}).Hash());
    OBL_CHECK(fixture({100, 200, 3}, {5, 6, 1}).Hash() != fixture({100, 200, 3}, {5, 7, 1}).Hash());
    OBL_CHECK(fixture({100, 200, 3}, {0, 0, 0}).Hash() != fixture({100, 200, 5}, {0, 0, 0}).Hash());
    // The same names in another section stay observable.
    uint32_t a = 1, b = 2, zero = 0;
    auto other = [&](uint32_t x) {
        WorldSnapshot s;
        s.Class("Class name : Other");
        s.Add("work_points[].x", 6, &x, 4);
        s.Add("work_points[].y", 6, &zero, 4);
        s.Add("work_points[].dir", 4, &zero, 4);
        s.Normalize();
        return s.Hash();
    };
    OBL_CHECK(other(a) != other(b));
}

OBL_TEST_CASE("world_snapshot.text_format_and_hash_are_pinned")
{
    // Golden checkpoints and archived checkpoint-N.tsv files depend on this exact rendering and on the FNV-1a hash of it.
    WorldSnapshot s;
    s.Class("World");
    const uint8_t a[]{0x01, 0x00, 0xab};
    const uint8_t c = 0xff;
    s.Add("m_a", 12, a, sizeof(a));
    s.Add("m_b", 255, nullptr, 0);
    s.Add("m_c", 0, &c, 1);
    const std::string expected =
            "OBL-world-v3\nWorld\tclass\t0\t576f726c64\nWorld\tm_a\t12\t0100ab\nWorld\tm_b\t255\t\nWorld\tm_c\t0\tff\n";
    OBL_CHECK_EQ(s.Text(), expected);
    OBL_CHECK_EQ(s.Hash(), "615c03e1f672e9c0");
    OBL_CHECK_EQ(WorldSnapshot::HashOf(expected), s.Hash());
    // Schema 3 hashes the header, types and bytes, not the section and field names: a renamed record keeps the hash, a changed
    // type or byte does not.
    OBL_CHECK_EQ(WorldSnapshot::HashOf("OBL-world-v3\nWorld\tclass\t0\t576f726c64\nState\tfirst\t12\t0100ab\nState\tsecond\t255\t\n"
                                       "State\tthird\t0\tff\n"),
            s.Hash());
    OBL_CHECK(WorldSnapshot::HashOf(
                      "OBL-world-v3\nWorld\tclass\t0\t576f726c64\nWorld\tm_a\t12\t0100ab\nWorld\tm_b\t255\t\nWorld\tm_c\t1\tff\n") !=
            s.Hash());
    OBL_CHECK(WorldSnapshot::HashOf(
                      "OBL-world-v3\nWorld\tclass\t0\t576f726c64\nWorld\tm_a\t12\t0100ac\nWorld\tm_b\t255\t\nWorld\tm_c\t0\tff\n") !=
            s.Hash());
}
