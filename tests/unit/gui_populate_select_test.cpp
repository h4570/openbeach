// OnItemSelected of the GUI list combos (src/game/ui/gui_lists_populate.h) for every list kind; gui_populate_test.cpp
// covers the drinks and the sentinel value 6 only.

#include "unit_test.h"
#include "game/ui/gui_lists_populate.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include <string>
#include <vector>

namespace {
struct Env {
    obl::object::Building provider{};
    std::vector<std::string> calls;
    auto* Provider() { return &provider; }
    void SetContext(int index, int value) { calls.push_back("context:" + std::to_string(index) + ":" + std::to_string(value)); }
    void RequestResolution(int width, int height) { calls.push_back(std::to_string(width) + "x" + std::to_string(height)); }
    void SetBeer(obl::object::Building*, int value) { calls.push_back("beer:" + std::to_string(value)); }
    void SetEvent(obl::object::Building*, int slot, int value)
    {
        calls.push_back("event:" + std::to_string(slot) + ":" + std::to_string(value));
    }
};

std::vector<std::string> Select(int kind, int value)
{
    obl::ui::GuiListData list{};
    obl::ui::GuiListItem item{nullptr, value};
    Env env;
    list.m_kind = kind;
    obl::gui_populate::OnItemSelected(list, item, env);
    return env.calls;
}
} // namespace

OBL_TEST_CASE("gui_populate_select.context_slots_per_kind")
{
    OBL_CHECK((Select(0, 5) == std::vector<std::string>{"context:1:5"}));
    OBL_CHECK((Select(2, 5) == std::vector<std::string>{"context:3:5"}));
    OBL_CHECK((Select(3, 5) == std::vector<std::string>{"context:4:5"}));
    OBL_CHECK((Select(6, 5) == std::vector<std::string>{"context:2:5"}));
    OBL_CHECK((Select(8, 5) == std::vector<std::string>{"context:11:5"}));
    // Staff type sets both filters, except the sentinel 6 which leaves the second one alone.
    OBL_CHECK((Select(7, 3) == std::vector<std::string>{"context:10:3", "context:11:3"}));
    OBL_CHECK((Select(7, 6) == std::vector<std::string>{"context:10:6"}));
}

OBL_TEST_CASE("gui_populate_select.resolution_heights_map_to_modes")
{
    OBL_CHECK((Select(1, 480) == std::vector<std::string>{"640x480"}));
    OBL_CHECK((Select(1, 600) == std::vector<std::string>{"800x600"}));
    OBL_CHECK((Select(1, 768) == std::vector<std::string>{"1024x768"}));
    OBL_CHECK((Select(1, 960) == std::vector<std::string>{"1280x960"}));
    OBL_CHECK(Select(1, 720).empty()); // an unrecognized height requests nothing
    OBL_CHECK(Select(1, 0).empty());
}

OBL_TEST_CASE("gui_populate_select.drinks_and_events_and_unhandled_kinds")
{
    OBL_CHECK((Select(4, 2) == std::vector<std::string>{"beer:2"}));
    OBL_CHECK((Select(12, 1) == std::vector<std::string>{"beer:1"}));
    OBL_CHECK((Select(9, 3) == std::vector<std::string>{"event:0:3"}));
    OBL_CHECK((Select(10, 3) == std::vector<std::string>{"event:1:3"}));
    OBL_CHECK((Select(11, 3) == std::vector<std::string>{"event:2:3"}));
    OBL_CHECK(Select(5, 3).empty()); // the 0..99 number list has no selection action
    OBL_CHECK(Select(13, 3).empty());
    OBL_CHECK(Select(-1, 3).empty());
}
