#include "core/char16.h"
#include "game/map/grid_state.h"
#include "game/game/game_api.h"
#include "game/world/world_queries_api.h"
#include "bot_host.h"

#include "game/game/command_recorder.h"
#include "core/ascii_case.h"
#include "core/config.h"
#include "platform/test_tools/crash.h"
#include "determinism.h"
#include "flow.h"
#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/game/command_loop.h"
#include "game/objects/commodity_providers.h"
#include "game/ui/gui_modes_place.h"
#include "game/game/scenario_outcome.h"
#include "game/ui/text_table.h"
#include "game/objects/guest_search_desire.h"
#include "headless_sim.h"
#include "game/game/game.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/tables/text_table.h"
#include "core/log.h"
#include "core/wide_path.h"
#include "world_hash.h"
#include "game/world/world_queries.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <iterator>
#include <string>
#include "game/ui/map_view_math.h"

namespace obl::test {
namespace {

// LoadGame enters WorldTest after 0.1 s of frame time; far more frames means the start never settles (as the replay run).
constexpr int kMaxBootFrames = 10000;

// The professions of bot::kProfessions as object classes (object_manager::CountByClass).
constexpr object::ObjectType kProfessionTypes[] = {object::ObjectType::Cleaner, object::ObjectType::Builder, object::ObjectType::Mechanic,
        object::ObjectType::Rep, object::ObjectType::Lifeguard, object::ObjectType::SecurityGuard};
static_assert(std::size(kProfessionTypes) == bot::kProfessions.size());

void Report(const char* format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::printf("obl-sim: %s\n", line);
    std::fflush(stdout);
    OBL_LOG("obl-sim: %s", line);
}

double Milliseconds(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// Beats the hang watchdog like a script frame (chaos_host.cpp, `Update`).
void Update(game::Game& game)
{
    WatchdogBeat();
    game::game_api::Update(&game);
}

bool HasBuildJob(const object::Building* building)
{
    return object::building_api::HasBuildJob(building);
}

bool HasDemolishJob(const object::Building* building)
{
    return object::building_api::HasDemolishJob(building);
}

void WorldToTile(const float* point, int32_t tile[2])
{
    uint32_t out[2] = {};
    ui::MapView::WorldToTile(out, point);
    tile[0] = static_cast<int32_t>(out[0]);
    tile[1] = static_cast<int32_t>(out[1]);
}

const tables::SceneryDef* SceneryDefByKey(std::string_view key)
{
    const tables::WorldParams* params = game::GetWorldParams();
    for (int i = 0; i < params->m_numScenery; ++i)
        if (ascii::EqualsNoCase(key, params->m_scenery[i].m_key)) return &params->m_scenery[i];
    return nullptr;
}

const tables::BuildingDef* BuildingDefByKey(std::string_view key)
{
    const tables::WorldParams* params = game::GetWorldParams();
    for (int i = 0; i < params->m_numBuildings; ++i)
        if (ascii::EqualsNoCase(key, params->m_buildings[i].m_key)) return &params->m_buildings[i];
    return nullptr;
}

class Live final : public bot::Queries {
public:
    int32_t BeachAreaAt(int32_t x, int32_t y) const override
    {
        map::Grid* grid = game::GetGrid();
        if (!grid || x < 0 || y < 0 || static_cast<uint32_t>(x) >= grid->m_width || static_cast<uint32_t>(y) >= grid->m_height) return -1;
        float point[3]{};
        const grid_math::WorldPoint centre = grid->TileToWorldPoint(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1);
        point[0] = centre.x;
        point[1] = centre.y;
        point[2] = centre.z;
        return grid->GetBeachAreaOfPoint(point[0], point[1], point[2]);
    }
    bool BuildingFits(std::string_view key, int32_t x, int32_t y, int32_t direction) const override
    {
        const map::Grid* grid = game::GetGrid();
        const tables::BuildingDef* definition = grid ? BuildingDefByKey(key) : nullptr;
        if (!definition || !definition->m_footprint || direction < 0 || direction > 3) return false;
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= grid->m_width || static_cast<uint32_t>(y) >= grid->m_height) return false;
        return gui::BuildingFits(*definition, static_cast<uint32_t>(x), static_cast<uint32_t>(y), direction);
    }
    bool SceneryFits(std::string_view key, int32_t x, int32_t y, int32_t direction) const override
    {
        const map::Grid* grid = game::GetGrid();
        const tables::SceneryDef* definition = grid ? SceneryDefByKey(key) : nullptr;
        if (!definition || definition->m_Category == 1 || direction < 0 || direction > 7) return false;
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= grid->m_width || static_cast<uint32_t>(y) >= grid->m_height) return false;
        return gui::SceneryFits(*definition, static_cast<uint32_t>(x), static_cast<uint32_t>(y), direction);
    }
};

std::optional<bot::BotRun> LoadBotRun()
{
    const Text16& path = config::Get().headlessReplay;
    std::ifstream in(WidePath(path), std::ios::binary);
    if (!in) {
        Report("FAILED cannot read the bot run file %s", WideUtf8(path).c_str());
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto run = bot::ReadBotRun(text, &error);
    if (!run) Report("FAILED bot run %s: %s", WideUtf8(path).c_str(), error.c_str());
    return run;
}

// The bot's tables: `<run file stem>.plan` next to the run file; a run without one (the L0 and wait bots) plays with an empty plan.
std::optional<bot::BotPlan> LoadBotPlan()
{
    std::filesystem::path path = WidePath(config::Get().headlessReplay);
    path.replace_extension(".plan");
    std::ifstream in(path, std::ios::binary);
    if (!in) return bot::BotPlan{};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto plan = bot::BotPlan::Parse(text, &error);
    if (!plan) Report("FAILED bot plan %s: %s", path.string().c_str(), error.c_str());
    return plan;
}

// The game hour of the last metrics sample (bots/metrics.md).
struct SampleClock {
    int32_t day = -1;
    int hour = -1;
};

// One `sample` line at the start and at every new game hour: the cash curve and guest satisfaction (the resort rating is the
// guests' mean happiness) that tools/bot_runs.py compares with the run's tolerance bands. Keyed by game time, not ticks, so a
// bot that changes speed differently still compares the same hours. Reads only; the world hash is unchanged.
// Diagnostics (OBL_BOT_OPINIONS): every drop of a guest's variety opinion, by the desire it pursued and whether crowding fell too (a
// queue), reported and cleared with each sample.
std::map<std::string, int> g_varietyDrops;
void TrackVarietyDrops()
{
    static const bool enabled = std::getenv("OBL_BOT_OPINIONS") != nullptr;
    if (!enabled) return;
    const layout::World* world = game::GetWorld();
    if (!world || !world->m_objectManager) return;
    static std::map<uint32_t, std::pair<float, float>> last;
    const object::Manager* objects = world->m_objectManager;
    for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
        const auto* guest = static_cast<const object::Guest*>(objects->m_guests[i]);
        const float variety = guest->m_opinion[4], crowd = guest->m_opinion[1];
        auto [it, inserted] = last.try_emplace(guest->m_id, variety, crowd);
        if (!inserted && variety < it->second.first - 1e-6f) {
            const bool known = guest->m_desired_commod >= 0 && guest->m_desired_commod < static_cast<int32_t>(guest_search::kDesireCount);
            const char* desire = known ? guest_search::kDesireNames[guest->m_desired_commod] : "NONE";
            const char* at = "-";
            for (uint32_t b = 0; b < objects->m_buildings.size(); ++b) {
                const auto* building = static_cast<const object::Building*>(objects->m_buildings[b]);
                if (static_cast<int32_t>(building->m_id) == guest->m_DestObjectID && building->m_definition)
                    at = building->m_definition->m_key.c_str();
            }
            std::string thought = "?";
            const text::Table* texts = ui::text_table::Current();
            if (texts && guest->m_Thoughts[0] >= 0 && static_cast<size_t>(guest->m_Thoughts[0]) < texts->entries.size())
                thought = texts->entries[static_cast<size_t>(guest->m_Thoughts[0])].key;
            while (!thought.empty() && (std::isdigit(static_cast<unsigned char>(thought.back())) || thought.back() == '_'))
                thought.pop_back();
            g_varietyDrops[std::string(desire) + "@" + at + (crowd < it->second.second - 1e-6f ? "/queue" : "") + ":" + thought] += 1;
        }
        it->second = {variety, crowd};
    }
}

void SampleIfNewHour(uint32_t tick, SampleClock& last)
{
    const layout::World* world = game::GetWorld();
    if (!world || !world->m_player) return;
    const int hour = world_queries::GetHour(*world);
    if (world->m_gameDay == last.day && hour == last.hour) return;
    last = {world->m_gameDay, hour};
    // Leavers: the unhappy ones are the loss counter of scenarios 9 and 10. Fields after rating are appended, the band parser
    // ignores them.
    Report("sample tick=%u day=%d hour=%d cash=%.0f guests=%d rating=%d income=%.0f safety=%.2f accom=%.0f retail=%.0f attract=%.0f "
           "costs=%.0f construct=%.0f scenery=%.0f wages=%.0f stock=%.0f unhappy-leavers=%u",
            tick, last.day, hour, world->m_player->m_cash, world->m_numGuests, world_queries::GetResortRating(*world), world->m_incomeToday,
            world->m_averageSafety, world->m_accomIncomeToday, world->m_retailIncomeToday, world->m_attractIncomeToday, world->m_costsToday,
            world->m_constructCostToday, world->m_sceneryCostToday, world->m_wagesCostToday, world->m_stockCostToday,
            static_cast<uint32_t>(world->m_totalGuestUnhappyDepart[0] + world->m_totalGuestUnhappyDepart[1]));
    if (std::getenv("OBL_BOT_OPINIONS") && world->m_objectManager->m_guests.size() > 0) { // diagnostics: mean opinion per kind
        double sum[7]{}, tol[14]{};
        const object::Manager* objects = world->m_objectManager;
        for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
            const auto* guest = static_cast<const object::Guest*>(objects->m_guests[i]);
            for (int k = 0; k < 7; ++k) sum[k] += guest->m_opinion[k];
            for (int k = 0; k < 14; ++k) tol[k] += guest->m_Tolerance[k];
        }
        const double n = objects->m_guests.size();
        int finished = 0, building = 0;
        for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
            const auto* entry = static_cast<const object::Building*>(objects->m_buildings[i]);
            (HasBuildJob(entry) ? building : finished) += 1;
        }
        std::string states;
        for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
            const auto* entry = static_cast<const object::Building*>(objects->m_buildings[i]);
            if (HasBuildJob(entry)) continue;
            char text[96];
            std::snprintf(text, sizeof(text), " %s:r%u%s%s g%d", entry->m_definition ? entry->m_definition->m_key.c_str() : "?",
                    entry->m_RepairState, entry->m_OpenStatus ? "C" : "", entry->m_closedForPower ? "P" : "", entry->m_NumGuests);
            states += text;
        }
        Report("states%s", states.c_str());
        Report("buildings finished=%d under-construction=%d power use=%.0f max=%.0f current=%.0f demand=%.0f", finished, building,
                world->m_powerUse, world->m_maxPower, world->m_currentPower, world->m_powerDemand);
        Report("party count=%d", world->m_partyCount);
        Report("opinions beauty=%.2f crowd=%.2f totty=%.2f price=%.2f variety=%.2f clean=%.2f safety=%.2f", sum[0] / n, sum[1] / n,
                sum[2] / n, sum[3] / n, sum[4] / n, sum[5] / n, sum[6] / n);
        // The guests close to leaving early (happiness, the mean opinion, <= 0.25 leaves): what drags them down.
        double low[7]{};
        int lowCount = 0, noBed = 0, early = 0;
        for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
            const auto* guest = static_cast<const object::Guest*>(objects->m_guests[i]);
            noBed += guest->m_StayingAt == -1;
            early += guest->m_leavingEarly != 0;
            static std::set<uint32_t> s_reported;
            if (guest->m_leavingEarly != 0 && s_reported.insert(guest->m_id).second)
                Report("early-leaver id=%u happiness=%.2f bed=%d nights=%d opinions=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", guest->m_id,
                        guest->m_happiness, guest->m_StayingAt, guest->m_NightsToStay, guest->m_opinion[0], guest->m_opinion[1],
                        guest->m_opinion[2], guest->m_opinion[3], guest->m_opinion[4], guest->m_opinion[5], guest->m_opinion[6]);
            double happiness = 0;
            for (int k = 0; k < 7; ++k) happiness += guest->m_opinion[k] / 7.0;
            if (happiness >= 0.35) continue;
            ++lowCount;
            for (int k = 0; k < 7; ++k) low[k] += guest->m_opinion[k];
        }
        Report("beds no-bed=%d leaving-early=%d", noBed, early);
        // Strong wants (desire > 0.9) per desire: what the island lacks (a missed desire costs variety).
        int wants[26]{};
        for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
            const auto* guest = static_cast<const object::Guest*>(objects->m_guests[i]);
            for (int d = 0; d < 26; ++d) wants[d] += guest->m_desires[d] > 0.9f;
        }
        std::string wantText;
        for (int d = 0; d < 26; ++d)
            if (wants[d] > 0) wantText += std::string(" ") + guest_search::kDesireNames[d] + "=" + std::to_string(wants[d]);
        Report("wants%s", wantText.c_str());
        std::string drops;
        for (const auto& [key, count] : g_varietyDrops) drops += " " + key + "=" + std::to_string(count);
        g_varietyDrops.clear();
        Report("variety-drops%s", drops.c_str());
        if (lowCount > 0)
            Report("low-guests n=%d beauty=%.2f crowd=%.2f totty=%.2f price=%.2f variety=%.2f clean=%.2f safety=%.2f", lowCount,
                    low[0] / lowCount, low[1] / lowCount, low[2] / lowCount, low[3] / lowCount, low[4] / lowCount, low[5] / lowCount,
                    low[6] / lowCount);
        Report("tolerances scenic=%.1f litter=%d tol0=%.2f tol1=%.2f tol10=%.2f tol11=%.2f tol12=%.2f tol13=%.2f", world->m_scenicVal,
                static_cast<int>(world->m_numLitter), tol[0] / n, tol[1] / n, tol[10] / n, tol[11] / n, tol[12] / n, tol[13] / n);
        // Per guest type: its definition once (start ranges, raise/lower effects, tolerance pairs), then hourly count, mean
        // happiness and opinions; what a high-rating goal (scenario 12) must lift.
        std::map<std::string, std::pair<int, std::array<double, 9>>> types; // [8]: the highest totty (sightings are not clamped)
        static std::set<std::string> s_defsReported;
        for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
            const auto* guest = static_cast<const object::Guest*>(objects->m_guests[i]);
            const auto* def = guest->m_definition;
            auto& [count, means] = types[def->m_key];
            ++count;
            means[0] += guest->m_happiness;
            for (int k = 0; k < 7; ++k) means[k + 1] += guest->m_opinion[k];
            means[8] = std::max<double>(means[8], guest->m_opinion[tables::Totty]);
            if (!s_defsReported.insert(def->m_key).second) continue;
            std::string text;
            for (int k = 0; k < 7; ++k) {
                char part[96];
                std::snprintf(part, sizeof(part), " o%d=start%.2f-%.2f,eff+%.3f-%.3f,tol%.2f/%.2f", k, def->m_OpinionStart[k][0],
                        def->m_OpinionStart[k][1], def->m_OpinionEffect[k][0], def->m_OpinionEffect[k][1], def->m_OpinionTol[k][0],
                        def->m_OpinionTol[k][1]);
                text += part;
            }
            Report("guest-def %s gender=%d pref=%.2f attractive=%.2f nights=%d-%d%s", def->m_key.c_str(), def->m_Gender,
                    def->m_AttractivePref, def->m_Attractive, def->m_NightsMin, def->m_NightsMax, text.c_str());
        }
        for (const auto& [key, entry] : types) {
            const auto& [count, means] = entry;
            Report("guest-type %s n=%d happy=%.2f opinions=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f max-totty=%.2f", key.c_str(), count,
                    means[0] / count, means[1] / count, means[2] / count, means[3] / count, means[4] / count, means[5] / count,
                    means[6] / count, means[7] / count, means[8]);
        }
    }
}

std::string EndHash()
{
    WorldHash hash;
    const char* error = nullptr;
    return HashWorld(hash, error) ? hash.hash : std::string("none");
}

} // namespace

bool IsBotRunPath(Text16View path)
{
    constexpr Text16View kExtension = u".bot";
    if (path.size() < kExtension.size()) return false;
    const Text16View tail = path.substr(path.size() - kExtension.size());
    return ascii::EqualsNoCase(tail, kExtension);
}

bot::WorldView CaptureWorldView(uint32_t tick)
{
    bot::WorldView view;
    view.tick = tick;
    const layout::World* world = game::GetWorld();
    if (!world) return view;
    view.day = world->m_gameDay;
    view.hour = world->m_gameDayTime;
    view.cash = world->m_player->m_cash;
    view.incomeToday = world->m_incomeToday;
    view.safety = world->m_averageSafety;
    view.scenic = world->m_scenicVal;
    view.maxPower = world->m_maxPower;
    view.currentPower = world->m_currentPower;
    view.powerDemand = world->m_powerDemand;
    view.guests = world->m_numGuests;
    view.rating = world_queries::GetResortRating(*world);
    view.beauty = world->m_averageBeauty;
    for (uint32_t i = 0; i < world->m_objectManager->m_guests.size(); ++i) {
        const auto* guest = static_cast<const object::Guest*>(world->m_objectManager->m_guests[i]);
        ++(guest->m_definition->m_Gender == 0 ? view.maleGuests : view.femaleGuests);
    }
    view.drunk = world->m_pissedCount;
    view.paused = world->m_paused != 0;
    view.speedLevel = world->m_gameSpeedLevel;
    const map::Grid* grid = game::GetGrid();
    view.mapWidth = static_cast<int32_t>(grid->m_width);
    view.mapHeight = static_cast<int32_t>(grid->m_height);
    view.beachAreaCount = static_cast<int32_t>(grid->m_beachAreas.size());
    view.objectiveCount = std::min<int32_t>(static_cast<int32_t>(world->m_numObjectives), static_cast<int32_t>(view.objectives.size()));
    for (int32_t i = 0; i < view.objectiveCount; ++i) view.objectives[i] = static_cast<int32_t>(world->m_objectiveStatus[i]);
    view.outcome = game::GetScenarioOutcome();

    auto* mutableWorld = const_cast<layout::World*>(world); // the queries take `this` non-const; they only read
    object::Manager* objects = world->m_objectManager;
    for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
        const auto* building = static_cast<const object::Building*>(objects->m_buildings[i]);
        bot::Building entry;
        entry.id = building->m_id;
        entry.key = building->m_definition ? building->m_definition->m_key : building->m_Name;
        int32_t tile[2];
        WorldToTile(building->m_WorldXYZ, tile);
        entry.tileX = tile[0];
        entry.tileY = tile[1];
        entry.underConstruction = HasBuildJob(building);
        entry.beingDemolished = HasDemolishJob(building);
        entry.builtByUser = building->m_builtByUser != 0;
        entry.repairState = building->m_RepairState;
        entry.beachArea = building->m_InfluenceArea;
        entry.guests = building->m_NumGuests;
        entry.closed = building->m_OpenStatus != 0;
        auto* live = const_cast<object::Building*>(building); // the accessors take `this` non-const; they only read
        entry.maleIntensity = static_cast<float>(object::building_api::GetAverageMaleIntensity(live));
        entry.femaleIntensity = static_cast<float>(object::building_api::GetAverageFemaleIntensity(live));
        const int32_t slots = building->m_definition ? std::clamp(building->m_definition->m_NumCommodities, 0, 5) : 0;
        for (int32_t slot = 0; slot < slots; ++slot) {
            entry.prices.push_back(building->m_Commod_Price[slot]);
            entry.referencePrices.push_back(static_cast<float>(object::SlotReferencePrice(*building, slot)));
        }
        view.buildings.push_back(std::move(entry));
    }
    const tables::WorldParams* params = game::GetWorldParams();
    for (int i = 0; i < world->m_numValidBuildings; ++i) {
        const int id = world->m_validBuildingIDs[i];
        const tables::BuildingDef& def = params->m_buildings[id];
        bot::BuildingType type;
        type.key = def.m_key;
        type.category = def.m_Category;
        type.cost = def.m_Cost2Build;
        type.width = def.m_GridWidth;
        type.height = def.m_GridHeight;
        type.buildOn = def.m_BuildOn;
        type.power = def.m_Power;
        type.maxInstances = def.m_MaxInstances;
        type.staffCapacity = def.m_MaxStaff;
        type.tree = def.m_Tree;
        type.allowed = mutableWorld->BuildingAllowed(id);
        type.lockedByTree = mutableWorld->BuildingLockedByTree(id);
        for (const bot::Building& building : view.buildings) {
            if (building.key != type.key) continue;
            ++type.total;
            if (!building.underConstruction && !building.beingDemolished) ++type.finished;
        }
        view.buildingTypes.push_back(std::move(type));
    }
    for (int i = 0; i < std::min(params->m_numScenery, tables::WorldParams::kMaxScenery); ++i) {
        const tables::SceneryDef& def = params->m_scenery[i];
        (def.m_Category == 1 ? view.surfaceKeys : view.sceneryKeys).emplace_back(def.m_key);
    }
    for (int i = 0; i < std::min(params->m_numBeers, tables::WorldParams::kMaxBeers); ++i)
        view.beerKeys.emplace_back(params->m_beers[i].m_key);
    for (int i = 0; i < std::min(params->m_numEvents, tables::WorldParams::kMaxEvents); ++i)
        view.showKeys.emplace_back(params->m_events[i].m_key);
    for (size_t i = 0; i < std::size(kProfessionTypes); ++i)
        view.staff[i] = object::object_manager::CountByClass(objects, static_cast<char>(kProfessionTypes[i]));
    for (uint32_t i = 0; i < objects->m_others.size(); ++i) {
        const auto* object = objects->m_others[i];
        for (size_t profession = 0; profession < std::size(kProfessionTypes); ++profession)
            if (object->m_type == kProfessionTypes[profession]) view.staffIds[profession].push_back(object->m_id);
    }
    return view;
}

const bot::Queries& LiveQueries()
{
    static const Live queries;
    return queries;
}

int RunBot(game::Game& game)
{
    const auto started = std::chrono::steady_clock::now();
    const std::optional<bot::BotRun> run = LoadBotRun();
    if (!run) return kExitHeadlessFailed;
    const bot::BotInfo* info = bot::FindBot(run->bot);
    const auto start = command::ParseStartScenario(run->start);
    if (!info || !start) {
        Report("FAILED bot %s or start %s unrecognized", run->bot.c_str(), run->start.c_str());
        return kExitHeadlessFailed;
    }
    Report("bot %s start %s seed=%u fixed-dt=%g max-ticks=%u expect=%s", run->bot.c_str(), run->start.c_str(), run->seed, run->fixedDt,
            run->maxTicks, std::string(bot::OutcomeName(run->expect)).c_str());

    // The order of a replay's start: seed, fixed frame time, new game, then frames until the world is settled.
    if (!SeedGameRand(run->seed)) {
        Report("FAILED seed (RNG tape active)");
        return kExitHeadlessFailed;
    }
    SetFixedDt(run->fixedDt);
    BootNewGame(game, start->sandbox, start->island);
    int bootFrames = 0;
    for (; bootFrames < kMaxBootFrames && !WorldSettled(); ++bootFrames) Update(game);
    if (!WorldSettled()) {
        Report("FAILED the world did not settle after %d frames", bootFrames);
        return kExitHeadlessFailed;
    }
    game::ResetScenarioOutcome();
    const auto booted = std::chrono::steady_clock::now();
    Report("boot frames=%d ms=%.0f", bootFrames, Milliseconds(started, booted));
    if (std::getenv("OBL_BOT_TYPES")) { // diagnostics for writing a bot: what the scenario offers
        for (const auto& type : CaptureWorldView(0).buildingTypes)
            Report("type %s cost=%d size=%dx%d buildon=%u category=%d max=%d tree=%s allowed=%d", type.key.c_str(), type.cost, type.width,
                    type.height, type.buildOn, type.category, type.maxInstances, type.tree.c_str(), type.allowed ? 1 : 0);
        const tables::WorldParams* params = game::GetWorldParams();
        for (int i = 0; i < params->m_numBuildings; ++i) {
            const tables::BuildingDef& def = params->m_buildings[i];
            std::string families;
            for (int c = 0; c < def.m_NumCommodities; ++c) {
                const tables::CommodityDef& commodity = params->m_commodities[def.m_Commodity[c]];
                families += std::string(" ") + commodity.m_key + "/" + params->m_commodityFamilies[commodity.m_family].m_key;
            }
            Report("capacity %s guests=%d queue=%d commodities:%s", def.m_key.c_str(), def.m_GuestCapacity, def.m_MaxQueue,
                    families.c_str());
        }
        for (int i = 0; i < params->m_numScenery; ++i) {
            const tables::SceneryDef& def = params->m_scenery[i];
            Report("scenery %s cat=%d size=%dx%d cost=%d buildon=%u rating=%.2f clean=%d repair=%d water=%d", def.m_key.c_str(),
                    def.m_Category, def.m_GridWidth, def.m_GridHeight, def.m_Cost2Build, def.m_BuildOn, def.m_Rating, def.m_clean,
                    def.m_repair, def.m_water);
        }
    }

    const std::optional<bot::BotPlan> plan = LoadBotPlan();
    if (!plan) return kExitHeadlessFailed;
    const std::unique_ptr<bot::Bot> player = info->make(*plan);
    const uint32_t interval = std::max<uint32_t>(1, player->ThinkInterval());
    const game::CommandLoopStats before = game::GetCommandLoopStats();
    std::vector<command::Command> commands;
    uint32_t submitted = 0;
    uint32_t tick = 0;
    SampleClock sampled;
    SampleIfNewHour(tick, sampled);
    for (; tick < run->maxTicks && game::GetScenarioOutcome() == bot::Outcome::Running; ++tick) {
        if (tick % 5000 == 0) {
            const auto view = CaptureWorldView(tick);
            Report("progress ticks=%u day=%d hour=%.2f cash=%.0f guests=%d rating=%d beauty=%.2f scenic=%.1f drunk=%d objectives=%d,%d,%d "
                   "income=%.0f safety=%.2f",
                    tick, view.day, view.hour, view.cash, view.guests, view.rating, view.beauty, view.scenic, view.drunk,
                    view.objectives[0], view.objectives[1], view.objectives[2], view.incomeToday, view.safety);
        }
        if (tick % interval == 0) {
            commands.clear();
            player->Think(CaptureWorldView(tick), LiveQueries(), commands);
            for (command::Command& command : commands) {
                // The body line of a replay: the run's commands can be pasted into one.
                Report("@%u %s", tick, command::Format(command).c_str());
                game::SubmitCommand(std::move(command));
                ++submitted;
            }
        }
        Update(game);
        const game::CommandLoopStats stats = game::GetCommandLoopStats();
        if (!stats.inWorld || stats.tick != tick + 1) {
            Report("FAILED tick %u left gameplay (in-world=%d tick=%u)", tick, stats.inWorld, stats.tick);
            return kExitHeadlessFailed;
        }
        TrackVarietyDrops();
        SampleIfNewHour(tick + 1, sampled);
    }

    const bot::Outcome outcome = game::GetScenarioOutcome();
    const bot::WorldView end = CaptureWorldView(tick);
    const game::CommandLoopStats after = game::GetCommandLoopStats();
    const double ms = Milliseconds(booted, std::chrono::steady_clock::now());
    Report("end ticks=%u outcome=%s day=%d hour=%.2f cash=%.0f guests=%d rating=%d commands=%u applied=%u rejected=%u hash=%s ms=%.0f "
           "ticks-per-s=%.0f",
            tick, std::string(bot::OutcomeName(outcome)).c_str(), end.day, end.hour, end.cash, end.guests, end.rating, submitted,
            after.applied - before.applied, after.rejected - before.rejected, EndHash().c_str(), ms, ms > 0 ? tick * 1000.0 / ms : 0.0);
    if (const layout::World* world = game::GetWorld())
        Report("leavers unhappy=%d departed=%d", world->m_totalGuestUnhappyDepart[0] + world->m_totalGuestUnhappyDepart[1],
                world->m_totalGuestDepart[0] + world->m_totalGuestDepart[1]);
    const bool pass = outcome == run->expect;
    Report("%s", pass ? "PASS" : "FAILED");
    return pass ? 0 : kExitHeadlessMismatch;
}

} // namespace obl::test
