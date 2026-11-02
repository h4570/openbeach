#include "game/weather/weather_state.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "unit_test.h"
#include "game/world/world_snapshot_model.h"

#include "game/map/grid_state.h"
#include "game/map/path_data.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using obl::WorldSnapshot;
namespace ws = obl::world_snapshot;
namespace world_data = obl::world_data;
namespace object = obl::object;

OBL_TEST_CASE("world_snapshot_model.typed_records_keep_the_save_tags")
{
    WorldSnapshot s;
    s.Class("Class name : T");
    s.UInt("u", 1);
    s.Int("i", -1);
    const float half = 0.5f;
    s.Float("f", half);
    s.Bool("b", 1);
    s.Byte("y", 2);
    s.Short("h", 3);
    s.Word("w", 4);
    s.Dword("d", 5);
    s.String("s", "ab");
    const char16_t wide[] = u"AB";
    s.WString("ws", wide);
    const int32_t ints[2] = {6, 7};
    s.Ints("is", ints, 2);
    const float floats[1] = {1.0f};
    s.Floats("fs", floats, 1);
    const uint8_t bytes[3] = {8, 9, 10};
    s.Bytes("bs", bytes, 3);
    OBL_CHECK_EQ(s.Text(),
            std::string("OBL-world-v3\n"
                        "T\tclass\t0\t54\n"
                        "T\tu\t6\t01000000\n"
                        "T\ti\t4\tffffffff\n"
                        "T\tf\t3\t0000003f\n"
                        "T\tb\t2\t01\n"
                        "T\ty\t9\t02\n"
                        "T\th\t5\t0300\n"
                        "T\tw\t13\t0400\n"
                        "T\td\t12\t05000000\n"
                        "T\ts\t1\t6162\n"
                        "T\tws\t8\t41004200\n"
                        "T\tis\t7\t0600000007000000\n"
                        "T\tfs\t11\t0000803f\n"
                        "T\tbs\t10\t08090a\n"));
}

OBL_TEST_CASE("world_snapshot_model.player_and_path")
{
    world_data::Player player{};
    player.m_name = {'A', 'B'};
    player.m_cash = 100.0f;
    WorldSnapshot s;
    ws::EmitPath(s);
    ws::EmitPlayer(s, player);
    OBL_CHECK_EQ(s.Text(),
            std::string("OBL-world-v3\n"
                        "Path\tclass\t0\t50617468\n"
                        "Player\tclass\t0\t506c61796572\n"
                        "Player\tName\t8\t41004200\n"
                        "Player\tCash\t3\t0000c842\n"));
}

OBL_TEST_CASE("world_snapshot_model.grid_areas_then_cells")
{
    std::vector<obl::map::GridCell> cells(2);
    cells[0].m_flags = 0x11223344;
    cells[0].m_zOffset = 0x55;
    cells[1].m_flags = 1;
    cells[1].m_zOffset = 0x578;
    obl::map::Grid grid{};
    grid.m_width = 2;
    grid.m_height = 1;
    grid.m_cells = cells;
    grid.m_islandSize = 7;
    grid.m_beachAreas.resize(2);
    grid.m_beachAreas[1].m_shut = true;
    grid.m_seaAreas.resize(1);
    WorldSnapshot s;
    ws::EmitGrid(s, grid);
    OBL_CHECK_EQ(s.Text(),
            std::string("OBL-world-v3\n"
                        "WorldMap\tclass\t0\t576f726c644d6170\n"
                        "WorldMap\tisland_size\t4\t07000000\n"
                        "WorldMap\tbeach_areas_open[]\t6\t01000000\n"
                        "WorldMap\tbeach_areas_open[]\t6\t00000000\n"
                        "WorldMap\tsea_areas_open[]\t6\t01000000\n"
                        "WorldMap\tflags[]\t12\t44332211\n"
                        "WorldMap\theights[]\t13\t5500\n"
                        "WorldMap\tflags[]\t12\t01000000\n"
                        "WorldMap\theights[]\t13\t7805\n"));
}

OBL_TEST_CASE("world_snapshot_model.world_fields")
{
    auto world = std::make_unique<world_data::World>();
    world->m_mapId[0] = 'm';
    world->m_gameDay = 3;
    world->m_boatList[0] = 0x12345678; // a raw pointer in the save file, never in the snapshot
    world->m_profitRecord[5] = 1.0f;
    world->m_objectiveStatus[2] = 1;
    world->m_unbuiltAfterStartup = 9;
    WorldSnapshot s;
    ws::EmitWorldFields(s, *world, 2);
    const std::string text = s.Text();
    OBL_CHECK_EQ(s.Count(), size_t{70}); // the class record and the 69 fields
    OBL_CHECK(text.starts_with("OBL-world-v3\nWorld\tclass\t0\t576f726c64\nWorld\tmap_id\t1\t6d\n"));
    OBL_CHECK(text.find("World\tgame_day\t4\t03000000\n") != std::string::npos);
    OBL_CHECK(text.find("boat_list") == std::string::npos);
    OBL_CHECK(text.find("Record") == std::string::npos);
    OBL_CHECK(text.find("World\tobjective_status\t6\t00000000\nWorld\tobjective_status\t6\t01000000\n") != std::string::npos);
    OBL_CHECK(text.ends_with("World\tunbuilt_after_startup\t4\t09000000\nWorld\tscript_state\t4\t02000000\n"));
    // Normalize keeps every emitted record as it is.
    WorldSnapshot normalized;
    ws::EmitWorldFields(normalized, *world, 2);
    normalized.Normalize();
    OBL_CHECK_EQ(normalized.Text(), text);
}

OBL_TEST_CASE("world_snapshot_model.first_difference")
{
    OBL_CHECK_EQ(WorldSnapshot::FirstDifference("h\na\n", "h\na\n", "model", "save"), std::string());
    OBL_CHECK_EQ(WorldSnapshot::FirstDifference("h\nA\tx\t3\t00\nB\n", "h\nA\tx\t3\t01\nB\n", "model", "save"),
            std::string("line 2 model=[A\tx\t3\t00] save=[A\tx\t3\t01]"));
    OBL_CHECK_EQ(WorldSnapshot::FirstDifference("h\na\n", "h\na\nb\n", "model", "save"), std::string("line 3 model=[<end>] save=[b]"));
}

OBL_TEST_CASE("world_snapshot_model.ferry_cursor_index")
{
    obl::ferry::Boat first{}, second{};
    first.NumGuests = 4;
    second.TimeFlags = 1;
    obl::ferry::Manager ferry;
    ferry.schedule = {first, second};
    auto records = [&] {
        WorldSnapshot s;
        ws::EmitFerry(s, ferry);
        return s;
    };
    ferry.cursor = std::next(ferry.schedule.begin());
    WorldSnapshot s = records();
    // Class, list size, 2 x (8 + 20 keywords) boat records, cursor, performance size, three leaver records.
    OBL_CHECK_EQ(s.Count(), size_t{2 + 2 * 27 + 2 + 3});
    OBL_CHECK(s.Text().find("FerryManager\tnext_boat\t6\t01000000\n") != std::string::npos);
    OBL_CHECK(s.Text().find("FerryManager\tguests\t4\t04000000\n") != std::string::npos);
    ferry.cursor = ferry.schedule.end();
    OBL_CHECK(records().Text().find("FerryManager\tnext_boat\t6\tffffffff\n") != std::string::npos);
}

OBL_TEST_CASE("world_snapshot_model.delayed_messages")
{
    obl::messages::Message message{};
    message.sender = 1;
    message.receiver = 2;
    message.type = 3;
    message.flags = 4;
    message.data = 5;
    message.extra = 6;
    message.floatData = 7.0f;
    message.time = 1.0f;
    obl::messages::Handler handler;
    handler.delayed.push_back(message);
    WorldSnapshot s;
    ws::EmitMessages(s, handler);
    OBL_CHECK_EQ(s.Text(),
            std::string("OBL-world-v3\n"
                        "MessageHandler\tclass\t0\t4d65737361676548616e646c6572\n"
                        "MessageHandler\tdelayed\t6\t01000000\n"
                        "MessageHandler\tsender\t4\t01000000\n"
                        "MessageHandler\treceiver\t4\t02000000\n"
                        "MessageHandler\ttype\t4\t03000000\n"
                        "MessageHandler\tflags\t4\t04000000\n"
                        "MessageHandler\tdata\t4\t05000000\n"
                        "MessageHandler\ttime\t3\t0000803f\n"));
}

OBL_TEST_CASE("world_snapshot_model.cleaning_then_weather_share_a_section")
{
    obl::cleaning::Manager queues;
    queues.jobs = {{7, 2, 1.0f, 1}};
    queues.activeJobs = {{8, 1, 0}};
    queues.cleaningCount = 3;
    queues.activeCount = 1;
    auto weather = std::make_unique<obl::weather::Weather>();
    weather->m_lightIndex = 5;
    WorldSnapshot s;
    ws::EmitCleaning(s, queues);
    ws::EmitWeather(s, *weather);
    const std::string text = s.Text();
    OBL_CHECK(text.find("CleaningManager\tjobs\t6\t01000000\nCleaningManager\tobject\t4\t07000000\n"
                        "CleaningManager\tcalls\t6\t02000000\nCleaningManager\ttime_called\t3\t0000803f\n"
                        "CleaningManager\turgent\t2\t01\nCleaningManager\tactive_jobs\t6\t01000000\n") != std::string::npos);
    OBL_CHECK(text.find("CleaningManager\tactive_count\t4\t01000000\nCleaningManager\tlight_index\t6\t05000000\n") != std::string::npos);
    OBL_CHECK(text.find("last_colour") == std::string::npos);
    OBL_CHECK(text.ends_with("CleaningManager\tcurrent_day_offset\t4\t00000000\n"));
}

namespace {

// The record names of a snapshot's ObjectManager section after its header (class, four counts, the id list, the next id).
std::vector<std::string> ObjectRecords(const WorldSnapshot& s)
{
    std::vector<std::string> names;
    const std::string text = s.Text();
    size_t line = 0;
    for (size_t at = 0, end; (end = text.find('\n', at)) != std::string::npos; at = end + 1, ++line) {
        if (line <= 8) continue; // the header line and the eight manager records
        const size_t tab = text.find('\t', at);
        const size_t name = text.find('\t', tab + 1);
        names.push_back(text.substr(tab + 1, name - tab - 1));
    }
    return names;
}

} // namespace

OBL_TEST_CASE("world_snapshot_model.objects_by_type")
{
    auto manager = std::make_unique<obl::object::Manager>();
    auto scenery = std::make_unique<object::Scenery>();
    scenery->m_type = obl::object::ObjectType::Scenery;
    std::snprintf(scenery->m_Name, sizeof(scenery->m_Name), "Litter");
    std::snprintf(scenery->m_ObjectName, sizeof(scenery->m_ObjectName), "Littercancrush");
    scenery->m_RepairState = 3;
    auto building = std::make_unique<object::Building>();
    building->m_type = obl::object::ObjectType::Building;
    object::BuildingQueue queue{};
    queue.m_ownerId = 9;
    building->m_queue = &queue;
    auto guest = std::make_unique<object::Guest>();
    guest->m_type = obl::object::ObjectType::Guest;
    obl::path::RouteCursor cursor{std::make_shared<obl::path::Route>(obl::path::Route{.m_steps = {{1, 2}, {3, 4}}}), 1, 1};
    guest->m_pathCursor = &cursor;
    manager->AddBuilding(building.get(), 0);
    manager->AddGuest(guest.get(), 0);
    manager->AddOther(scenery.get(), 0);
    WorldSnapshot s;
    ws::EmitObjects(s, *manager);
    const std::string text = s.Text();
    // Buildings, then guests, then the others; every object starts with its type and its virtual name.
    OBL_CHECK(text.find("ObjectManager\tobject.type\t9\t02\n") < text.find("ObjectManager\tobject.type\t9\t03\n"));
    OBL_CHECK(text.find("ObjectManager\tobject.type\t9\t03\n") < text.find("ObjectManager\tobject.type\t9\t01\n"));
    OBL_CHECK(
            text.find("ObjectManager\tobject.name\t1\t6c697474657263616e6372757368\nObjectManager\tscenery.repair_state\t6\t03000000\n") !=
            std::string::npos);
    OBL_CHECK(text.find("ObjectManager\tbuilding.queue\t2\t01\nObjectManager\tbuilding.queue.owner\t4\t09000000\n") != std::string::npos);
    OBL_CHECK(text.find("ObjectManager\tmoving.path\t6\t01000000\nObjectManager\tmoving.path.time_stamp\t3\t00000000\n"
                        "ObjectManager\tmoving.path.steps\t6\t02000000\nObjectManager\tmoving.path.steps[].x\t6\t01000000\n") !=
            std::string::npos);
    OBL_CHECK(text.find("ObjectManager\tmoving.path.reverse\t2\t01\nObjectManager\tmoving.path.current_step\t6\t01000000\n") !=
            std::string::npos);
    const auto names = ObjectRecords(s);
    // Type and name, then: building 39 own (with the queue flag) + queue 4 + tail 2 + static 8 + base 10; guest 22 + moving 19 (with
    // the path flag) + route 14 + base 10; scenery 1 + static 8 + base 10.
    OBL_CHECK_EQ(names.size(), size_t{1 + 63 + 2 + 65 + 2 + 19});
    // Every name is one the snapshot keeps: Normalize erases none of them.
    WorldSnapshot normalized;
    ws::EmitObjects(normalized, *manager);
    normalized.Normalize();
    OBL_CHECK_EQ(normalized.Count(), s.Count());
}
