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
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Env {
    obl::object::Building provider{};
    obl::tables::BeerDef beers[3]{};
    std::vector<std::string> calls;
    void Invalidate(obl::ui::GuiListData&) { calls.push_back("invalidate"); }
    void Free(void*) { calls.push_back("free"); }
    auto* Provider()
    {
        calls.push_back("provider");
        return &provider;
    }
    unsigned BeerCount() { return 3; }
    auto* Beer(unsigned id) { return &beers[id]; }
    void AddItem(obl::ui::GuiListData&, const char* text, int value) { calls.push_back(std::string(text) + ":" + std::to_string(value)); }
    void SetContext(int index, int value) { calls.push_back("context:" + std::to_string(index) + ":" + std::to_string(value)); }
    void RequestResolution(int width, int height) { calls.push_back(std::to_string(width) + "x" + std::to_string(height)); }
    void SetBeer(obl::object::Building*, int value) { calls.push_back("beer:" + std::to_string(value)); }
    void SetEvent(obl::object::Building*, int slot, int value)
    {
        calls.push_back("event:" + std::to_string(slot) + ":" + std::to_string(value));
    }
};
} // namespace

OBL_TEST_CASE("gui_populate: drink selection uses the filtered row and preserves an absent selection")
{
    obl::ui::GuiListData list{};
    obl::ui::GuiListItem* vec[3]{};
    list.m_itemsBegin = list.m_itemsEnd = vec;
    list.m_refresh = 0x81;
    list.m_selected = 42;
    Env env;
    env.provider.m_BeerType = 2;
    env.beers[0].m_scriptFlag = env.beers[2].m_scriptFlag = 1;
    std::strcpy(env.beers[0].m_Brand, "Brand0");
    std::strcpy(env.beers[2].m_Brand, "Brand2");
    std::strcpy(env.beers[0].m_Cocktail, "Mix0");
    std::strcpy(env.beers[2].m_Cocktail, "Mix2");
    obl::gui_populate::PopulateDrinks(list, false, env);
    OBL_CHECK_EQ(list.m_selected, 1u);
    OBL_CHECK_EQ(list.m_refresh, 0);
    OBL_CHECK_EQ(env.calls[2], "Brand0:0");
    OBL_CHECK_EQ(env.calls[3], "Brand2:2");
    env.provider.m_BeerType = 1;
    list.m_selected = 42;
    env.calls.clear();
    obl::gui_populate::PopulateDrinks(list, true, env);
    OBL_CHECK_EQ(list.m_selected, 42u);
    OBL_CHECK_EQ(env.calls[2], "Mix0:0");
    OBL_CHECK_EQ(env.calls[3], "Mix2:2");
}

OBL_TEST_CASE("gui_populate: sentinel six changes the staff filter but cannot change hire type or event slots")
{
    obl::ui::GuiListData list{};
    obl::ui::GuiListItem item{nullptr, 6};
    Env env;
    list.m_kind = 7;
    obl::gui_populate::OnItemSelected(list, item, env);
    OBL_CHECK_EQ(env.calls.size(), size_t{1});
    OBL_CHECK_EQ(env.calls[0], "context:10:6");
    env.calls.clear();
    for (int kind = 9; kind <= 11; ++kind) {
        list.m_kind = kind;
        obl::gui_populate::OnItemSelected(list, item, env);
    }
    OBL_CHECK(env.calls.empty());
    item.m_value = 2;
    list.m_kind = 11;
    obl::gui_populate::OnItemSelected(list, item, env);
    OBL_CHECK_EQ(env.calls[0], "provider");
    OBL_CHECK_EQ(env.calls[1], "event:2:2");
}
