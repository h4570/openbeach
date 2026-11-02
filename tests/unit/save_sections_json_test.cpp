#include "unit_test.h"
#include "game/construction/construction_state.h"
#include "game/feedback/feedback_state.h"
#include "game/map/grid_state.h"
#include "game/messages/message_state.h"
#include "game/player/player_state.h"
#include "game/repair/repair_state.h"
#include "game/ui/map_view.h"
#include "game/world/save_sections_json.h"

#include <memory>
#include <string>

using namespace obl;

OBL_TEST_CASE("save_sections_json.player_writes_the_name_in_utf8_and_the_cash")
{
    player::Player player(std::basic_string<Char16>{u'Z', 0x00f3, u'f', u'i', u'a'});
    player.m_cash = 25000.5f;
    nlohmann::ordered_json out;
    world_save::WritePlayerJson(player, out);
    OBL_CHECK_EQ(out.dump(),
            std::string("{\"name\":\"Z\xc3\xb3"
                        "fia\",\"cash\":25000.5}"));
}

OBL_TEST_CASE("save_sections_json.view_writes_the_pairs_and_the_burn_queue")
{
    auto view = std::make_unique<ui::MapView>();
    view->m_burnQueue = {7, 9};
    view->m_cameraX = 64;
    view->m_cameraY = -2;
    view->m_cameraPositionX = 64.5f;
    view->m_mapLocked = 1;
    view->m_moveMapDestY = 3;
    view->m_guestObjectsDrawn = 11;
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteViewJson(*view, out, error));
    OBL_CHECK_EQ(out["camera"].dump(), std::string("[64,-2]"));
    OBL_CHECK_EQ(out["position"].dump(), std::string("[64.5,0.0]"));
    OBL_CHECK_EQ(out["burn_queue"].dump(), std::string("[7,9]"));
    OBL_CHECK(out["locked"].get<bool>());
    OBL_CHECK_EQ(out["move_destination"].dump(), std::string("[0,3]"));
    OBL_CHECK_EQ(out["guests_drawn"].get<int>(), 11);
    OBL_CHECK_EQ(std::next(out.begin(), 3).key(), std::string("burn_queue")); // in the record order

    view->m_burnQueue.clear();
    OBL_REQUIRE(world_save::WriteViewJson(*view, out, error));
    OBL_CHECK_EQ((--out.end()).key(), std::string("burn_queue")); // where the converter puts a list without records

    view->m_movingMap = 2;
    OBL_CHECK(!world_save::WriteViewJson(*view, out, error));
    OBL_CHECK_EQ(error, std::string("ZEngine.moving: bool value 2"));
}

OBL_TEST_CASE("save_sections_json.feedback_writes_the_notices_and_the_masks")
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return "<" + std::string(text) + ">"; };
    feedback::Manager manager;
    manager.pending.push_back({7, feedback::Text::Key("FB_LITTER")});
    manager.history.push_back({-1, 3, 9.5f, feedback::Text::Wide(u"Hi")});
    manager.mechanicMessages = 0x80000001u;
    manager.suppressed[1] = 12;
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteFeedbackJson(manager, context, out, error));
    OBL_CHECK_EQ(out["pending"].dump(), std::string(R"([{"object":7,"wide":false,"text":"<FB_LITTER>"}])"));
    OBL_CHECK_EQ(out["history"].dump(), std::string(R"([{"object":-1,"day":3,"hour":9.5,"wide":true,"text":"Hi"}])"));
    OBL_CHECK_EQ(out["mechanic_messages"].get<int64_t>(), int64_t(-2147483647));
    OBL_CHECK_EQ(out["suppressed"].size(), size_t(feedback::kMaxSuppressed));
    OBL_CHECK_EQ(out["suppressed"][1].get<int>(), 12);
    OBL_CHECK_EQ((--out.end()).key(), std::string("last_missed_desires_check"));

    manager.suppressObject = 2;
    OBL_CHECK(!world_save::WriteFeedbackJson(manager, context, out, error));
    OBL_CHECK_EQ(error, std::string("FeedbackManager.suppress_objects: bool value 2"));
}

OBL_TEST_CASE("save_sections_json.messages_write_the_delayed_queue")
{
    messages::Handler handler;
    handler.delayed.push_back({1, 2, 3, 4, 5, -6, 0.5f, 10.0f});
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteMessagesJson(handler, out, error));
    OBL_CHECK_EQ(out.dump(),
            std::string(R"({"delayed":[{"sender":1,"receiver":2,"type":3,"flags":4,"data":5,"float_data":0.5,"time":10.0,"extra":-6}]})"));
}

OBL_TEST_CASE("save_sections_json.repair_names_its_three_lists_by_record_order")
{
    repair::Manager manager;
    manager.sceneryJobs.push_back({});
    manager.sceneryJobs.back().id = 2050;
    manager.sceneryJobs.back().urgent = 1;
    manager.vandalismCount = 3;
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteRepairJson(manager, out, error));
    OBL_CHECK_EQ(out["building_jobs"].dump(), std::string("[]"));
    OBL_CHECK_EQ(out["scenery_jobs"][0]["object"].get<int>(), 2050);
    OBL_CHECK(out["scenery_jobs"][0]["urgent"].get<bool>());
    OBL_CHECK_EQ(out["active_jobs"].dump(), std::string("[]"));
    OBL_CHECK_EQ(out["vandalisms"].get<int>(), 3);

    manager.sceneryJobs.back().urgent = 5;
    OBL_CHECK(!world_save::WriteRepairJson(manager, out, error));
    OBL_CHECK_EQ(error, std::string("RepairManager.urgent: bool value 5"));
}

OBL_TEST_CASE("save_sections_json.map_writes_the_areas_and_cells_in_record_order")
{
    map::Grid grid;
    grid.m_width = grid.m_height = 2;
    grid.m_cells.resize(4);
    grid.m_cells[1].m_flags = 0x80000003u;
    grid.m_cells[3].m_zOffset = 0x578;
    grid.m_islandSize = 3;
    grid.m_beachAreas.resize(2);
    grid.m_beachAreas[1].m_shut = true;
    grid.m_seaAreas.resize(1);
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteMapJson(grid, out, error));
    OBL_CHECK_EQ(out.dump(),
            std::string("{\"cells\":[2,2],\"island_size\":3,\"areas_open\":[true,false,true],"
                        "\"flags\":[0,2147483651,0,0],\"heights\":[0,0,0,1400]}"));

    grid.m_height = 1; // the old save cannot say the size of a grid that is not square
    grid.m_cells.resize(2);
    OBL_CHECK(!world_save::WriteMapJson(grid, out, error));
    OBL_CHECK(!error.empty());
}

OBL_TEST_CASE("save_sections_json.construction_writes_the_work_point_owners_after_each_job")
{
    construction::Manager manager;
    auto& job = *manager.jobs.emplace_back(std::make_unique<construction::Job>());
    job.m_buildingId = 12;
    job.m_workPointOwners[1] = 905; // the builder holding point 1; a load without it lets the builder claim point 0
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteConstructionJson(manager, out, error));
    OBL_REQUIRE(out["jobs"].size() == 1);
    const nlohmann::ordered_json& written = out["jobs"][0];
    OBL_CHECK_EQ(written["work_point_owners"].dump(), std::string("[-1,905,-1,-1,-1,-1,-1,-1,-1,-1]"));
    auto last = written.end();
    OBL_CHECK_EQ((--last).key(), std::string("work_point_owners"));
}

OBL_TEST_CASE("save_sections_json.commodities_and_beer_write_the_counters_the_old_records_drop")
{
    const uint32_t complaints[] = {0, 4, 1};
    nlohmann::ordered_json out;
    world_save::WriteCommoditiesJson(complaints, out);
    OBL_CHECK_EQ(out.dump(), std::string(R"({"complaints":[0,4,1]})"));
    world_save::WriteBeerJson(7, out);
    OBL_CHECK_EQ(out.dump(), std::string(R"({"served":7})"));
}
