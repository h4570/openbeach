#include "core/char16.h"

using ListKeyValue = gui_content::ListRow;
void PopulateBase(List* list, uint32_t dt)
{
    EmptyCallFunction(list, static_cast<int>(dt));
}
object::Building* PopulateProvider(void* manager)
{
    return static_cast<object::Building*>(static_cast<ui::GuiManager*>(manager)->VariableProvider());
}
tables::BeerDef* PopulateBeer(void* params, uint32_t id)
{
    return world::world_params::GetBeerDefinition(static_cast<tables::WorldParams*>(params), static_cast<int>(id));
}
tables::EventDef* PopulateEventDef(void* params, int id)
{
    return world::world_params::GetEventDefinition(static_cast<tables::WorldParams*>(params), id);
}
uint8_t PopulateSlotActive(void* events, int slot)
{
    return static_cast<object::BuildingEvents*>(events)->IsSlotActive(slot) ? 1 : 0;
}
int PopulateSlotEvent(void* events, int slot)
{
    return static_cast<object::BuildingEvents*>(events)->GetSlotEventId(slot);
}
uint32_t PopulateScenarioCount(void*)
{
    return static_cast<uint32_t>(game::game_api::ScenarioCount());
}
const obl::Char16* PopulateScenario(void*, uint32_t id)
{
    return game::game_api::Scenario(static_cast<int>(id));
}
void PopulateRequest(void* manager, int width, int height)
{
    (static_cast<ui::GuiManager*>(manager))->RequestResolution(width, height);
}
void PopulateAddItem(List* list, const char* key, int value)
{
    GuiListAddItem(list, key, value);
}
void PopulateAddLiteral(List* list, const obl::Char16* text, int value)
{
    g_comboCounter.Count();
    GameEnv env;
    gui_combo::AddWideItem(*list, text, value, env);
}
struct PopulateEnv : GameEnv {
    void BaseUpdate(List& list, uint32_t dt) { PopulateBase(&list, dt); }
    void AddItem(List& list, const char* key, int value) { PopulateAddItem(&list, key, value); }
    void AddLiteral(List& list, const obl::Char16* text, int value) { PopulateAddLiteral(&list, text, value); }
    void ClearItems(List& list) { GuiListClearItems(&list); }
    object::Building* Provider() { return PopulateProvider(game::GetGuiManager()); }
    uint32_t BeerCount() { return game::GetWorldParams()->m_numBeers; }
    auto* Beer(uint32_t id) { return PopulateBeer(game::GetWorldParams(), id); }
    auto* Event(int id) { return PopulateEventDef(game::GetWorldParams(), id); }
    uint8_t SlotActive(void* events, int slot) { return PopulateSlotActive(events, slot); }
    int SlotEvent(void* events, int slot) { return PopulateSlotEvent(events, slot); }
    const char* EventInProgress() { return gui_data::kListEventInProgress; }
    int Context(int index) { return game::GetGuiManager()->m_context[index]; }
    auto Resolutions() { return ui::ResolutionListEntries(); }
    int ScreenHeight() { return game::GetRenderer()->m_height; }
    uint8_t ModeSupported(int index) { return game::GetRenderer()->m_modeSupported[index]; }
    uint32_t ScenarioCount() { return PopulateScenarioCount(game::GetGame()); }
    const obl::Char16* Scenario(uint32_t id) { return PopulateScenario(game::GetGame(), id); }
    void FormatNumber(obl::Char16* out, int value)
    {
        const auto print = ::obl::ui::host_text::Print;
        print(out, gui_data::kListNumberFormat, value);
    }
    std::span<const ListKeyValue> Table(int kind)
    {
        using gui_content::List;
        switch (kind) {
        case 0: return gui_content::Rows(List::Guest);
        case 2: return gui_content::Rows(List::Cell);
        case 6: return gui_content::Rows(List::Building);
        case 7: return gui_content::Rows(List::Staff);
        case 8: return gui_content::Rows(List::Hire);
        case 9: return gui_content::Rows(List::Event);
        }
        return {};
    }
    void RequestResolution(int w, int h) { PopulateRequest(game::GetGuiManager(), w, h); }
    // The list rows carry definition ids; the command carries the keys (docs/architecture/commands.md).
    void SetBeer(object::Building* provider, int value)
    {
        game::SubmitCommand(command::SetBeer{provider->m_id, game::GetWorldParams()->m_beers[value].m_key});
    }
    void SetEvent(object::Building* provider, int slot, int value)
    {
        game::SubmitCommand(command::SetShow{provider->m_id, slot, game::GetWorldParams()->m_events[value].m_key});
    }
};

} // namespace

namespace {
// call-drive GuiListBeer|GuiListShow <building id>: the real selection callback with a synthetic row on a bare list, the building as the
// dialog's provider. The row is the first beer (show) that differs from the current one, so the command changes something; a show is compared with the building's event entry 1.
test::CallCounter g_beerListCommand("GuiListBeer");
test::CallCounter g_showListCommand("GuiListShow");
template<bool beer> void DriveListSelection(int id)
{
    auto* building = game::GetWorld()
            ? static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), static_cast<uint32_t>(id)))
            : nullptr;
    if (!building || (!beer && !building->m_events)) {
        OBL_LOG("autotest: FAILED GuiList command driver needs gameplay and a building id");
        return;
    }
    tables::WorldParams& params = *game::GetWorldParams();
    int value = -1;
    if (beer) {
        for (int i = 0; i < static_cast<int>(params.m_numBeers) && value < 0; ++i)
            if (params.m_beers[i].m_scriptFlag && i != building->m_BeerType) value = i;
    } else {
        for (int i = 0; i < params.m_numEvents && value < 0; ++i)
            if (params.m_events[i].m_available && i != PopulateSlotEvent(building->m_events, 1)) value = i;
    }
    if (value < 0) {
        OBL_LOG("autotest: FAILED GuiList command driver found no row to select");
        return;
    }
    auto* manager = game::GetGuiManager();
    void* saved = manager->m_variableProvider;
    manager->m_variableProvider = building;
    List list{};
    list.m_kind = beer ? 4 : 10;
    ListItem item{nullptr, value};
    PopulateEnv env;
    gui_populate::OnItemSelected(list, item, env);
    manager->m_variableProvider = saved;
}

bool RegisterPopulateDrivers()
{
    g_beerListCommand.SetDriver(DriveListSelection<true>);
    g_showListCommand.SetDriver(DriveListSelection<false>);
    return true;
}
