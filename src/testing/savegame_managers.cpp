#include "game/weather/weather_state.h"
#include "savegame_managers.h"
#include "game/events/event_manager.h"
#include "game/weather/weather.h"

#include "game/construction/construction_manager.h"
#include "game/ferry/ferry.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "test_util.h"
#include "world_state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace obl::test {
namespace {
int g_buildingId = -1;
const construction::Hoarding kFenceRecords[2] = {{1, 80, 45, 101}, {3, 81, 45, 202}};

nlohmann::json State()
{
    auto world = World();
    auto* event = world->m_eventManager.get();
    auto* ferry = world->m_ferryManager.get();
    auto construction = static_cast<construction::Manager*>(world->m_constructionManager);
    auto weather = static_cast<weather::Weather*>(world->m_weather);
    nlohmann::json state{{"active", event->numCurEvents}, {"events", event->numEvents}, {"next", event->nextEvent},
            {"contamination", world->m_waterContam}, {"flash", weather->m_lightFlash != 0},
            {"lightmap", weather->m_lightningLightmap != nullptr}, {"strikePoint", weather->m_strikePoint},
            {"strikeTime", weather->m_nextStrikeTime}, {"flashTime", weather->m_flashTime}, {"weather", weather->m_currentState}};
    state["performance"] = nlohmann::json::array();
    for (const auto& boat : ferry->performance) {
        state["performance"].push_back({{"count", boat.NumGuests}, {"info", boat.GuestInfo}, {"flags", boat.TimeFlags}, {"time", boat.Time},
                {"day", boat.Day}, {"types", boat.GuestType}, {"keywords", boat.GuestKeywordNumbers}, {"keyword0", boat.GuestKeyword[0]}});
    }
    state["jobs"] = nlohmann::json::array();
    for (const auto& owned : construction->jobs) {
        const auto& job = *owned;
        nlohmann::json hoardings = nlohmann::json::array();
        for (const auto& h : job.hoardings) hoardings.push_back({h.m_direction, h.x, h.y, h.m_savedMetadata});
        state["jobs"].push_back({{"id", job.m_buildingId}, {"hoardings", hoardings}, {"building", job.m_underConstruction != 0},
                {"started", job.m_started != 0}});
    }
    return state;
}
// mode: "no-build" (no job), "job" (a real job without fences) or "fence" (a job with two pending fences).
void Fixture(const std::string& mode)
{
    const bool construction = mode != "no-build";
    auto world = World();
    // Scenario 3 supplies its own WaterContamination schedule, also reconstructed on load.
    disaster::event_facade::Update(world->m_eventManager);
    ferry::Boat boat{};
    boat.GuestInfo = 3;
    boat.TimeFlags = 3;
    boat.NumGuests = 17;
    boat.GuestType[0] = 7;
    boat.GuestKeywordNumbers[0] = 11;
    std::strcpy(boat.GuestKeyword[0], "SAVEGAME_FIXTURE");
    ferry::ferry_facade::AddBoatEvent(world->m_ferryManager, &boat);
    auto* ferry = world->m_ferryManager.get();
    // Keep the queued performance pending across the first gameplay update after load.
    ferry->performance.back().Time += 1;
    if (construction) {
        object::Building* building = SpawnBuilding("STAGE", 80, 45);
        if (!building) return;
        g_buildingId = building->m_id;
        auto job = construction::runtime::FindJob(world->m_constructionManager, g_buildingId);
        if (!job || !job->hoardings.empty()) {
            OBL_LOG("autotest: FAILED managers-fixture expected new construction job without hoardings");
            return;
        }
        // Scenario creation starts a real job but leaves its pending fences empty.
        // Supply two distinct fence records to exercise a save-loop case no scenario reaches.
        if (mode == "fence") job->hoardings.assign(std::begin(kFenceRecords), std::end(kFenceRecords));
        for (auto& point : job->workPoints) {
            // Unused work points (direction 0) stay at (0, 0).
            if (point.direction == 0) point = {};
        }
    }
    auto weather = static_cast<weather::Weather*>(world->m_weather);
    // Force a due storm at an interior point. The update creates the bolt and flash,
    // posts damage messages and schedules the next strike; no bitmap/pointer is fabricated.
    weather->m_currentState = weather->m_previousState = 5;
    weather->m_strikeBuildingId = -1;
    weather->m_hasStrikeTarget = 0;
    weather->m_strikePoint[2] = 0;
    weather::runtime::SetLightningArea(weather, 2000, 3000, 2100, 3100);
    weather::runtime::SetThunderLevel(weather, 7);
    weather->m_nextStrikeTime = world->m_gameTime - 1;
    weather::runtime::UpdateLighting(weather);
}
} // namespace
bool ExecuteSavegameManagersCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "managers-fixture" && name != "managers-state") return false;
    if (!World() || GameStateIndex() != 0 || Game()->m_guiReloadPending) {
        OBL_LOG("autotest: FAILED %s needs settled gameplay", name.c_str());
        return true;
    }
    if (name == "managers-fixture" && args.size() == 1 && (args[0] == "fence" || args[0] == "job" || args[0] == "no-build"))
        Fixture(args[0]);
    else if (name != "managers-state" || !args.empty()) {
        OBL_LOG("autotest: FAILED %s invalid arguments", name.c_str());
        return true;
    }
    const auto state = State();
    int hoardings = 0;
    for (const auto& job : state["jobs"]) hoardings += static_cast<int>(job["hoardings"].size());
    // Fence records of the first job that has any (-1 when absent): direct side and the unknown field.
    int fence[2][2] = {{-1, -1}, {-1, -1}};
    for (const auto& job : state["jobs"]) {
        const auto& list = job["hoardings"];
        for (size_t i = 0; i < list.size() && i < 2; ++i) {
            fence[i][0] = list[i][0].get<int>();
            fence[i][1] = list[i][3].get<int>();
        }
        if (!list.empty()) break;
    }
    const auto& loaded = construction::runtime::LastLoadedFences();
    OBL_LOG("autotest: managers-state active=%d performance=%zu jobs=%zu hoardings=%d flash=%d lightmap=%d contamination=%g "
            "fence0d=%d fence0u=%d fence1d=%d fence1u=%d loadedTotal=%d loaded0u=%d loaded1d=%d loaded1u=%d",
            state["active"].get<int>(), state["performance"].size(), state["jobs"].size(), hoardings, int(state["flash"].get<bool>()),
            int(state["lightmap"].get<bool>()), state["contamination"].get<float>(), fence[0][0], fence[0][1], fence[1][0], fence[1][1],
            loaded.total, loaded.records[0].m_savedMetadata, loaded.records[1].m_direction, loaded.records[1].m_savedMetadata);
    return true;
}
} // namespace obl::test
