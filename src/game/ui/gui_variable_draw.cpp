#include "game/objects/scenery_names.h"
#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/text_renderer.h"
#include "game/ui/gui_variable.h"
#include "game/game/game_api.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "game/ui/gui_variable_lifecycle.h"

#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/gui_definitions.h"
#include "game/ferry/ferry.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/ui/sound_api.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/ui/text_encoding.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace obl::gui {
namespace {

using Widget = ui::GuiVariable;
using TextRenderer = ui::TextRenderer;

test::CallCounter g_counter("ui.gui-variable"); // counts the draws (without a provider nothing is drawn)
test::CallCounter g_undefined("GuiVariableUndefined");

class GameEnv final : public gui_variable::Env {
public:
    float Cash() const override { return game::GetWorld()->m_player->m_cash; }
    int NumGuests() const override { return game::GetWorld()->m_numGuests; }
    int GameDay() const override { return game::GetWorld()->m_gameDay; }
    float GameTime() const override { return game::GetWorld()->m_gameDayTime; }
    float MinDistance() const override { return audio::CurrentSound()->minDistance; }
    float MaxDistance() const override { return audio::CurrentSound()->maxDistance; }
    const Char16* Text(const char* key) const override { return ui::text_table::Lookup(key); }
    int WholeHour(float time) const override { return game::GameTimeWholeHour(time); }
    int QuarterMinutes(float time) const override { return game::GameTimeQuarterMinutes(time); }
    Swprintf Format() const override { return ui::host_text::Print; }

    bool ProviderText(const char* key, Char16* out) const override
    {
        void* provider = static_cast<ui::GuiManager*>(Gui())->VariableProvider();
        if (!provider) return false;
        constexpr Char16 kUntouched = 0xffff;
        out[0] = kUntouched;
        provider_virtual::GetVariableText(provider, key, out);
        providerSilent = out[0] == kUntouched;
        if (providerSilent) out[0] = u'\0';
        return true;
    }
    mutable bool providerSilent = false; // the last ProviderText call left the buffer alone

    double NextBoatTime(int todayOnly, uint32_t* day) const override
    {
        return ferry::ferry_facade::GetNextBoatTime(game::GetWorld()->m_ferryManager, todayOnly, day);
    }
    const char* IslandKey(gui_variable::IslandText text) const override
    {
        layout::World* world = game::GetWorld();
        switch (text) {
        case gui_variable::IslandText::Title: return world->m_islandTitle.c_str();
        case gui_variable::IslandText::Area: return world->m_islandArea.c_str();
        case gui_variable::IslandText::Weather: return world->m_islandWeather.c_str();
        case gui_variable::IslandText::Temp: return world->m_islandTemp.c_str();
        case gui_variable::IslandText::Hazards: return world->m_islandHazards.c_str();
        }
        return nullptr;
    }
    const char* ScriptTextKey() const override { return game::GetGuiManager()->m_scriptTextKey; }
    const char* MessageTextKey() const override { return game::GetGuiManager()->m_messageTextKey; }
    double GameVersion() const override { return game::game_api::Version(); }
    // The converted {heading key, filter id} pairs (as gui_tables::GetHeadingKey), searched for the active filter.
    const char* FilterHeadingKey() const override
    {
        const uint32_t active = game::GetContext().guiTableActiveFilter;
        for (const auto& heading : gui_content::Headings())
            if (heading.m_filter == active) return heading.m_key;
        return nullptr;
    }
    int BrowserCount() const override { return game::GetGuiManager()->m_context[6]; }
    bool ObjectExists(const char* key) const override { return object::object_manager::CountByName(game::GetObjectManager(), key) != 0; }
    double BoatTimeToday(int index) const override
    {
        return ferry::ferry_facade::GetBoatTimeToday(game::GetWorld()->m_ferryManager, index);
    }

    float PowerOutput() const override { return game::GetWorld()->m_currentPower; }
    float PowerDemand() const override { return game::GetWorld()->m_powerDemand; }
    float PowerUse() const override { return game::GetWorld()->m_powerUse; }
    int WeatherColor() const override { return static_cast<int>(game::GetWeather()->m_manualColor); }
    int LitterCount() const override { return game::GetWorld()->m_numLitter; }
    int ObjectCount(int objectClass) const override
    {
        return object::object_manager::CountByClass(game::GetObjectManager(), static_cast<char>(objectClass));
    }
    int SceneryNonLitterCount() const override
    {
        constexpr int kSceneryClass = 1;
        const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), kSceneryClass);
        const auto count = static_cast<uint32_t>(list.size());
        int plain = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (!scenery_names::HasLitterInName(list[i]->m_Name)) ++plain;
        return plain;
    }
    int GuestGenderCount(bool nonZero) const override
    {
        const object::Manager* manager = game::GetObjectManager();
        int matching = 0;
        for (uint32_t i = 0; i < manager->m_guests.size(); ++i) {
            const auto* guest = static_cast<const object::Person*>(manager->m_guests[i]);
            if ((guest->m_definition->m_Gender != 0) == nonZero) ++matching;
        }
        return matching;
    }
    int GuestFlow(gui_variable::GuestFlowField field, int type) const override
    {
        const layout::World* world = game::GetWorld();
        using Field = gui_variable::GuestFlowField;
        switch (field) {
        case Field::TotalArrive: return world->m_totalGuestArrive[type];
        case Field::TotalDepart: return world->m_totalGuestDepart[type];
        case Field::TodayArrive: return world->m_todayGuestArrive[type];
        case Field::TodayDepart: return world->m_todayGuestDepart[type];
        case Field::TotalUnhappyDepart: return world->m_totalGuestUnhappyDepart[type];
        case Field::TodayUnhappyDepart: return world->m_todayGuestUnhappyDepart[type];
        }
        return 0;
    }
    float Money(gui_variable::MoneyField field) const override { return *MoneySlot(game::GetWorld(), field); }

    int SelectedBuilding() const override { return Gui()->m_context[0]; }
    int SelectedScenery() const override { return Gui()->m_context[5]; }
    const char* BuildingKey(int building) const override { return BuildingAt(building)->m_key.c_str(); }
    int BuildingCost(int building) const override { return BuildingAt(building)->m_Cost2Build; }
    int BuildingPower(int building) const override { return BuildingAt(building)->m_Power; }
    const char* BuildingTreeKey(int building) const override { return BuildingAt(building)->m_Tree.c_str(); }
    const char* BuildingHoursKey(int building) const override
    {
        return game::GetWorldParams()->m_openingHours[BuildingAt(building)->m_OpeningHours].m_key.c_str();
    }
    int BuildingIdByKey(const char* key) const override { return world::world_params::GetBuildingId(game::GetWorldParams(), key); }
    bool BuildingAllowed(int building) const override { return game::GetWorld()->BuildingAllowed(building); }
    bool HasIdleBuilding(const char* key) const override { return IdleBuildingCount(key) > 0; }
    int IdleBuildingCount(const char* key) const override
    {
        const auto found = object::object_manager::FindObjectsByName(game::GetObjectManager(), key);
        const auto count = static_cast<uint32_t>(found.size());
        int idle = 0;
        for (uint32_t i = 0; i < count; ++i) {
            auto* building = found[i];
            const auto* job = static_cast<const object::Building*>(building);
            if (!object::building_api::HasBuildJob(job) && !object::building_api::HasDemolishJob(job)) ++idle;
        }
        return idle;
    }
    int BuildingMaxStaff(int building) const override { return BuildingAt(building)->m_MaxStaff; }
    int StaffCategory() const override { return Gui()->m_context[10]; }
    int StaffCountAll() const override
    {
        return static_cast<int>(object::object_manager::GetNonServiceStaffList(game::GetObjectManager()).size());
    }
    int StaffOnPayroll(int objectClass) const override
    {
        const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), objectClass);
        const auto count = static_cast<uint32_t>(list.size());
        int hired = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (static_cast<const object::Staff*>(list[i])->m_gotTheSack == 0) ++hired;
        return hired;
    }
    int BuildingCommodityCount(int building) const override { return BuildingAt(building)->m_NumCommodities; }
    float BuildingAppeal(int building, int slot, bool female) const override
    {
        return (&BuildingAt(building)->m_CommodityAB[0][0])[slot * 2 + (female ? 1 : 0)];
    }
    const char* SceneryKey(int scenery) const override { return SceneryAt(scenery)->m_key.c_str(); }
    int SceneryCost(int scenery) const override { return SceneryAt(scenery)->m_Cost2Build; }

    const char* ScenarioDescriptionKey() const override { return game::GetWorld()->m_scenarioDescription.c_str(); }
    const char* ScenarioObjectivesKey() const override { return game::GetWorld()->m_scenarioObjectives.c_str(); }
    const char* ScenarioTitleKey() const override { return game::GetWorld()->m_scenarioTitle.c_str(); }
    uint32_t ObjectiveCount() const override { return game::GetWorld()->m_numObjectives; }

    const char* ObjectiveKey(uint32_t index) const override { return index < 4 ? game::GetWorld()->m_objectiveText[index].c_str() : ""; }
    uint32_t ObjectiveStatus(uint32_t index) const override { return (&game::GetWorld()->m_objectiveStatus[0])[index]; }
    int WorldCounterValue(gui_variable::WorldCounter counter) const override { return *CounterSlot(game::GetWorld(), counter); }
    bool SandboxMode() const override { return game::GetGame()->m_sandbox != 0; }
    int CurrentScenario() const override { return game::GetGame()->m_lastUnlockedScenario; }
    bool ScenarioCompleted(int scenario) const override { return game::game_api::IsScenarioCompleted(scenario); }
    bool ScenarioAlwaysAvailable(int scenario) const override { return game::game_api::IsScenarioAlwaysAvailable(scenario); }
    int GuestCapacityOfLodgings() const override
    {
        int capacity = 0;
        for (const char* name : {"ACCOMBLK", "HOTEL"}) {
            const auto found = object::object_manager::FindObjectsByName(game::GetObjectManager(), name);
            const auto count = static_cast<uint32_t>(found.size());
            for (uint32_t i = 0; i < count; ++i) capacity += static_cast<object::Building*>(found[i])->m_definition->m_GuestCapacity;
        }
        return capacity;
    }
    // The heading is measured with the second text renderer whatever the widget's own font is.
    int MeasureWidth(const Char16* text) const override { return ui::font_text::MeasureWidth(Gui()->m_textRenderers[1], text); }

    static ui::GuiManager* Gui() { return game::GetGuiManager(); }
    static const tables::BuildingDef* BuildingAt(int id) { return game::GetWorldParams()->m_buildings.data() + id; }
    static const tables::SceneryDef* SceneryAt(int id) { return game::GetWorldParams()->m_scenery.data() + id; }

    static float* MoneySlot(layout::World* world, gui_variable::MoneyField field)
    {
        using Field = gui_variable::MoneyField;
        switch (field) {
        case Field::Income: return &world->m_incomeToday;
        case Field::DemolishIncome: return &world->m_demolishIncomeToday;
        case Field::FineIncome: return &world->m_fineIncomeToday;
        case Field::AccomIncome: return &world->m_accomIncomeToday;
        case Field::AttractIncome: return &world->m_attractIncomeToday;
        case Field::RetailIncome: return &world->m_retailIncomeToday;
        case Field::Costs: return &world->m_costsToday;
        case Field::ConstructCost: return &world->m_constructCostToday;
        case Field::SceneryCost: return &world->m_sceneryCostToday;
        case Field::WagesCost: return &world->m_wagesCostToday;
        case Field::StockCost: return &world->m_stockCostToday;
        case Field::AverageProfit: return &world->m_averageProfit;
        case Field::AverageCost: return &world->m_averageCost;
        case Field::AverageIncome: return &world->m_averageIncome;
        case Field::TotalFines: return &world->m_totalFines;
        }
        return &world->m_incomeToday;
    }

    static int* CounterSlot(layout::World* world, gui_variable::WorldCounter counter)
    {
        using Counter = gui_variable::WorldCounter;
        switch (counter) {
        case Counter::BuildingsConstructed: return &world->m_buildingsConstructed;
        case Counter::DiffBuildingsConstructed: return &world->m_diffBuildingsConstructed;
        case Counter::BarsConstructed: return &world->m_barsConstructed;
        case Counter::BuildingsLeftToBuild: return &world->m_buildingsLeftToBuild;
        case Counter::PartyCount: return &world->m_partyCount;
        case Counter::ShagCount: return &world->m_shagCount;
        case Counter::PissedCount: return &world->m_pissedCount;
        case Counter::GuestsDrowned: return &world->m_guestsDrowned;
        case Counter::GuestsEaten: return &world->m_guestsEaten;
        case Counter::UnbuiltAfterStartup: return &world->m_unbuiltAfterStartup;
        }
        return &world->m_buildingsConstructed;
    }
};

TextRenderer* RendererOf(const Widget* widget)
{
    auto* gui = game::GetGuiManager();
    const auto count = static_cast<int>(sizeof(gui->m_textRenderers) / sizeof(gui->m_textRenderers[0]));
    if (!gui || widget->m_font < 0 || widget->m_font >= count) return nullptr;
    return gui->m_textRenderers[widget->m_font];
}

// The alignment the draw leaves behind: bit 0 centre, bit 1 left, bit 2 right, otherwise the renderer keeps its own.
int AlignAfterDraw(const Widget* widget, int current)
{
    if (widget->m_alignFlags & 1) return 1;
    if (widget->m_alignFlags & 2) return 0;
    if (widget->m_alignFlags & 4) return 2;
    return current;
}

void DrawText(const Widget* widget, TextRenderer* renderer, const gui_variable::Output& out)
{
    if (widget->m_alignFlags & 7) ui::font_text::SetAlign(renderer, AlignAfterDraw(widget, 0));
    ui::font_text::SetOutlined(renderer, 0);
    ui::font_text::SetColor(renderer, out.color);
    // A caption with a heading starts `xOffset` pixels further right and is that much narrower.
    ui::font_text::SetRect(
            renderer, widget->m_x0 + out.xOffset, widget->m_y0, widget->m_x1 - widget->m_x0 - out.xOffset, widget->m_y1 - widget->m_y0);
    ui::font_text::DrawWrapped(renderer, 0, 0, out.text);
}

} // namespace

} // namespace obl::gui

namespace obl::ui {

void GuiVariable::DrawOverlay()
{
    TextRenderer* renderer = gui::RendererOf(this);
    if (!renderer) {
        // Ignore fonts outside the script font table.
        gui::g_undefined.Count();
        return;
    }
    gui_variable::Output out;
    const gui::GameEnv env;
    const bool draws = gui_variable::Evaluate(m_key, env, m_color, out);
    if (out.undefined || (out.fromProvider && draws && env.providerSilent)) gui::g_undefined.Count();
    gui::g_counter.Count();
    if (draws) gui::DrawText(this, renderer, out);
}

} // namespace obl::ui
