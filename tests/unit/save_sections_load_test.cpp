#include "unit_test.h"
#include "game/player/player_state.h"
#include "game/ui/map_view.h"
#include "game/world/save_sections_json.h"
#include "game/world/save_sections_load.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

OBL_TEST_CASE("save_sections_load.player_reads_back_the_name_and_the_cash")
{
    player::Player player(std::basic_string<Char16>{u'Z', 0x00f3, u'f', u'i', u'a'});
    player.m_cash = 25000.5f;
    nlohmann::ordered_json written;
    world_save::WritePlayerJson(player, written);
    player::Player loaded(std::basic_string<Char16>{u'x'});
    std::string error;
    OBL_REQUIRE(world_save::ReadPlayerJson(nlohmann::json(written), loaded, error));
    OBL_CHECK(loaded.m_name == player.m_name);
    OBL_CHECK(loaded.m_cash == 25000.5f);

    // Missing keys: an empty name and no cash; the name stops at the player load routine's 64 units.
    loaded.m_cash = 3.0f;
    OBL_REQUIRE(world_save::ReadPlayerJson(nlohmann::json::object(), loaded, error));
    OBL_CHECK(loaded.m_name.empty());
    OBL_CHECK(loaded.m_cash == 0.0f);
    OBL_REQUIRE(world_save::ReadPlayerJson(nlohmann::json{{"name", std::string(80, 'a')}}, loaded, error));
    OBL_CHECK_EQ(loaded.m_name.size(), size_t(player::kNameCapacity));

    OBL_CHECK(!world_save::ReadPlayerJson(nlohmann::json{{"cash", "lots"}}, loaded, error));
    OBL_CHECK_EQ(error, std::string("Player.cash: not a float"));
}

OBL_TEST_CASE("save_sections_load.view_reads_back_the_pairs_and_the_burn_queue")
{
    auto view = std::make_unique<ui::MapView>();
    view->m_burnQueue = {7, -9};
    view->m_cameraX = 64;
    view->m_cameraY = -2;
    view->m_cameraPositionX = 64.5f;
    view->m_velocityY = std::numeric_limits<float>::infinity();
    view->m_mapLocked = 1;
    view->m_moveMapDuration = 1.25f;
    view->m_moveMapStartX = 5;
    view->m_moveMapDestY = 3;
    view->m_lastUnBurnTime = 9.0f;
    view->m_guestObjectsDrawn = 11;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteViewJson(*view, written, error));

    auto loaded = std::make_unique<ui::MapView>();
    std::vector<int32_t> burnQueue;
    OBL_REQUIRE(world_save::ReadViewJson(nlohmann::json(written), *loaded, burnQueue, error));
    OBL_CHECK((burnQueue == std::vector<int32_t>{7, -9}));
    loaded->m_burnQueue = view->m_burnQueue; // the caller appends the entries
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteViewJson(*loaded, again, error));
    OBL_CHECK_EQ(again.dump(), written.dump());

    // Missing keys read as the bridge's zeros.
    OBL_REQUIRE(world_save::ReadViewJson(nlohmann::json::object(), *loaded, burnQueue, error));
    OBL_CHECK(burnQueue.empty());
    OBL_CHECK_EQ(loaded->m_cameraX, 0);
    OBL_CHECK_EQ(loaded->m_mapLocked, 0);
    OBL_CHECK(loaded->m_moveMapDuration == 0.0f);
    OBL_CHECK_EQ(loaded->m_guestObjectsDrawn, 0);

    OBL_CHECK(!world_save::ReadViewJson(nlohmann::json{{"camera", {1}}}, *loaded, burnQueue, error));
    OBL_CHECK_EQ(error, std::string("ZEngine.camera: not a pair"));
}
