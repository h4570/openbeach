#include "game/world/world_script_api.h"

#include "core/ascii_case.h"
#include "game/world/world_control_logic.h"

#include <span>

namespace obl::world_script_parse {
namespace {

struct NamedRow {
    int id;
    const char* name;
};

// The numbers of world::CallScriptFunction (world_control_logic.h); Debug is parsed but never passed (the interpreter skips it).
const NamedRow kFunctions[] = {
        {world_control::kFunctionPauseGame, "PauseGame"},
        {world_control::kFunctionUnpauseGame, "ResumeGame"},
        {world_control::kFunctionLockCamera, "LockMap"},
        {world_control::kFunctionUnlockCamera, "UnlockMap"},
        {world_control::kFunctionBreakdownsOff, "NoBreakDowns"},
        {world_control::kFunctionBreakdownsOn, "AllowBreakDowns"},
        {world_control::kFunctionCompleteScenario, "UnlockNextScenario"},
        {world_control::kFunctionDebug, "Debug"},
        {world_control::kFunctionNoLitter, "NoLitter"},
};

const NamedRow kValues[] = {
        {185, "GuestCount"},
        {186, "BreakdownCount"},
        {187, "VandalismCount"},
        {189, "CleanerCount"},
        {190, "MechanicCount"},
        {191, "SecurityCount"},
        {192, "LifeGuardCount"},
        {193, "RepCount"},
        {197, "DemoTime"},
        {199, "SandBox"},
        {200, "LitterCount"},
        {201, "UnHappyLeavers"},
        {202, "ProfitToday"},
        {203, "ConstructCount"},
        {204, "AllBuildingsOperating"},
        {205, "GuestPartyCount"},
        {206, "GuestTottyLevel"},
        {207, "GuestBeautyLevel"},
        {208, "DiffConstructCount"},
        {209, "BuildTreeExist"},
        {214, "GuestsEaten"},
        {215, "GuestsDrowned"},
        {216, "FinesCollected"},
        {217, "GuestShagCount"},
        {218, "GuestPissedCount"},
        {219, "WeatherDayOffset"},
        {220, "IncomeToday"},
        {221, "GuestSafetyLevel"},
        {222, "BarsBuilt"},
        {223, "DisasterDayOffset"},
        {226, "Me"},
        {234, "GameTime"},
        {236, "ResortRating"},
        {237, "GameDay"},
        {238, "PlayerCash"},
        {world_script::kScriptValueEventKey, "EventKey"},
        {world_script::kScriptValueEventObjectId, "EventObjectId"},
        {world_script::kScriptValueEventValue, "EventValue"},
};

const WorldEventInfo kWorldEvents[] = {
        {world_script::WorldEvent::DayStarted, "DayStarted"},
        {world_script::WorldEvent::HourStarted, "HourStarted"},
        {world_script::WorldEvent::NightStarted, "NightStarted"},
        {world_script::WorldEvent::DayLightStarted, "DayLightStarted"},
        {world_script::WorldEvent::BuildingFinished, "BuildingFinished"},
        {world_script::WorldEvent::BuildingBroken, "BuildingBroken"},
        {world_script::WorldEvent::BuildingRepaired, "BuildingRepaired"},
        {world_script::WorldEvent::BuildingDemolished, "BuildingDemolished"},
        {world_script::WorldEvent::Vandalism, "Vandalism"},
        {world_script::WorldEvent::GuestArrived, "GuestArrived"},
        {world_script::WorldEvent::GuestLeft, "GuestLeft"},
        {world_script::WorldEvent::GuestDrowned, "GuestDrowned"},
        {world_script::WorldEvent::GuestEaten, "GuestEaten"},
        {world_script::WorldEvent::WeatherChanged, "WeatherChanged"},
        {world_script::WorldEvent::DisasterStarted, "DisasterStarted"},
        {world_script::WorldEvent::DisasterFinished, "DisasterFinished"},
        {world_script::WorldEvent::StaffHired, "StaffHired"},
        {world_script::WorldEvent::StaffFired, "StaffFired"},
        {world_script::WorldEvent::FerryArrived, "FerryArrived"},
        {world_script::WorldEvent::BusArrived, "BusArrived"},
        {world_script::WorldEvent::ShowStarted, "ShowStarted"},
        {world_script::WorldEvent::BeerChanged, "BeerChanged"},
};

constexpr const char* kWeatherKindNames[] = {"HeatWave", "Normal", "Overcast", "LightRain", "HeavyRain", "ThunderStorm"};
constexpr const char* kDisasterKindNames[] = {"Sharks", "WaterContamination", "BeerShortage", "Lightning", "Strike"};

// Dialogs that announce a scenario win or loss (bot::OutcomeOfDialog). A mod world script may not open them.
constexpr std::string_view kRefusedModMenus[] = {"ScenarioWon", "GameWon", "ScenarioLost"};

std::vector<NamedId> Table(std::span<const NamedRow> rows)
{
    std::vector<NamedId> out;
    out.reserve(rows.size());
    for (const NamedRow& row : rows) out.push_back({row.id, row.name});
    return out;
}

} // namespace

const std::vector<NamedId>& EngineFunctions()
{
    static const std::vector<NamedId> kTable = Table(kFunctions);
    return kTable;
}

const std::vector<NamedId>& EngineValues()
{
    static const std::vector<NamedId> kTable = Table(kValues);
    return kTable;
}

std::span<const WorldEventInfo> WorldEvents()
{
    return kWorldEvents;
}

std::optional<world_script::WorldEvent> FindWorldEvent(std::string_view name)
{
    for (const WorldEventInfo& info : kWorldEvents)
        if (name == info.name) return info.event;
    return std::nullopt;
}

const char* WorldEventName(world_script::WorldEvent event)
{
    for (const WorldEventInfo& info : kWorldEvents)
        if (info.event == event) return info.name;
    return "";
}

const char* WeatherKindName(uint32_t state)
{
    return state < std::size(kWeatherKindNames) ? kWeatherKindNames[state] : "";
}

const char* DisasterKindName(int kind)
{
    return kind >= 0 && static_cast<size_t>(kind) < std::size(kDisasterKindNames) ? kDisasterKindNames[kind] : "";
}

bool IsRefusedModFunction(int functionId)
{
    return functionId == world_control::kFunctionCompleteScenario;
}

bool IsRefusedModMenu(std::string_view dialog)
{
    for (std::string_view refused : kRefusedModMenus)
        if (ascii::EqualsNoCase(dialog, refused)) return true;
    return false;
}

} // namespace obl::world_script_parse
