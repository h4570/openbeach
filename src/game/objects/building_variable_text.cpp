#include "core/char16.h"
#include "game/objects/object_building.h"
#include "game/game/game_api.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"

#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/objects/building_variable_text.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/construction/construction_manager.h"
#include "game/map/grid_state.h"
#include "game/repair/repair_manager.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/game/game.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/game/diagnostics.h"

// The text fields of a building's panels (the logic is building_variable_text.h): a key may leave the buffer untouched, an
// unknown key is fatal.
namespace obl::object {
using game::FatalError;
using game::GameTimeQuarterMinutes;
using game::GameTimeWholeHour;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;

namespace {

struct GameEnv {
    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
    // The key of the opening-hours set, without a range check.
    const char* HoursName(int hours) { return GetWorldParams()->m_openingHours[hours].m_key.c_str(); }
    using PrintFunc = int(__cdecl*)(Char16*, const Char16*, ...);
    static PrintFunc Printer() { return ui::host_text::Print; }
    void PrintInt(Char16* out, const Char16* format, int value) { Printer()(out, format, value); }
    void PrintReal(Char16* out, const Char16* format, double value) { Printer()(out, format, value); }
    void PrintInt3(Char16* out, const Char16* format, int a, int b, int c) { Printer()(out, format, a, b, c); }
    // The game clock's conversions of an hour value.
    int WholeHour(float time) { return GameTimeWholeHour(time); }
    int QuarterMinutes(float time) { return GameTimeQuarterMinutes(time); }
    int RepairJobStatus(int id) { return repair::repair_facade::GetJobStatus(GetWorld()->m_repairManager, id); }
    int CleaningJobStatus(int id) { return cleaning::cleaning_facade::GetJobStatus(GetWorld()->m_cleaningManager, id); }
    int CountByClass(int objectClass) { return object_manager::CountByClass(GetObjectManager(), static_cast<char>(objectClass)); }
    void PrintMoney(Char16* out, const Char16* format, int dollars, int cents) { Printer()(out, format, dollars, cents); }
    void PrintString(Char16* out, const Char16* format, const Char16* text) { Printer()(out, format, text); }
    const char* CommodityDisplayName(int id) { return world::world_params::GetCommodityDisplayName(GetWorldParams(), id); }
    const char* CommodityName(int id) { return world::world_params::GetCommodityName(GetWorldParams(), id); }
    const tables::BeerDef* Beer(int id) { return world::world_params::GetBeerDefinition(GetWorldParams(), id); }
    // A thought's author is a guest; its name is read without checking that the object still exists.
    const Char16* ObjectName(int32_t objectId)
    {
        const auto* object = object_manager::ObjectById(GetObjectManager(), objectId);
        return static_cast<const object::Guest*>(object)->m_name;
    }
    int Hour() { return GetWorld()->Hour(); }
    float OpeningStart(int set) { return GetWorldParams()->m_openingHours[set].m_open; }
    float OpeningEnd(int set) { return GetWorldParams()->m_openingHours[set].m_close; }
    const world_data::World& GameWorld() { return *GetWorld(); }
    float BuildProgress(int id) { return construction::runtime::BuildProgress(GetWorld()->m_constructionManager, id); }
    float DemolishProgress(int id) { return construction::runtime::DemolishProgress(GetWorld()->m_constructionManager, id); }
    bool AreaOpen(int kind, int area) { return GetGrid()->IsAreaOpen(kind, area); }
    void Fatal(const char* key) { FatalError("A script read unrecognized building variable \"%s\"", key); }
};

} // namespace

void Building::GetVariableText(const char* key, Char16* out)
{
    GameEnv env;
    building_text::GetVariableText(*this, key, out, env);
}
} // namespace obl::object
