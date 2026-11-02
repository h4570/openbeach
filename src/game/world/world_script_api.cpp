#include "game/world/world_script_api.h"

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
};

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

} // namespace obl::world_script_parse
