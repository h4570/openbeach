// Test-only checks of the commodity provider layer, split out of guest_desires.cpp:
//  * a model of the commodity_providers::FindProvider rules, called after every search of the game (game/commodity_providers reports
//    each one to the observer), which judges the answer whichever side computed it,
//  * an observer of object::building_api::GetProviderScore that compares each score with the building's own table,
//  * `provider-force`, a fixture that lends a live building to scratch provider tables to reach every branch (empty list, excluded
//    provider, distant provider, user closure, broken repair state, ranking), the six rental sea-area gates and the LIVEVENT scores.

#include "commodity_providers.h"
#include "core/ascii_case.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_params.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "game/objects/building_methods.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace obl::test {
namespace {

using commodity::ProviderRecord;
using object::Building;
using tables::BuildingDef;

BuildingDef* Def(const Building* object)
{
    return const_cast<BuildingDef*>(object->m_definition);
}

struct ProviderStats {
    unsigned calls = 0, bad = 0;
    unsigned branch[3]{};
    std::string firstBad;
};

bool Close(float a, float b)
{
    return std::fabs(a - b) <= 1e-5f;
}

void Expect(ProviderStats& stats, const char* what, bool ok)
{
    if (ok) return;
    ++stats.bad;
    OBL_LOG("autotest: helper mismatch (%s)", what);
    if (stats.firstBad.empty()) stats.firstBad = std::string("first bad (") + what + ")";
}

void Report(const char* name, ProviderStats& stats, const char* branches)
{
    bool ok = !stats.bad;
    OBL_LOG("autotest: %s calls=%u skipped=%u bad=%u branches(%s)=%u/%u/%u %s", name, stats.calls, 0u, stats.bad, branches, stats.branch[0],
            stats.branch[1], stats.branch[2], stats.firstBad.c_str());
    OBL_LOG("autotest: %s %s", ok ? "PASS" : "FAILED", name);
    stats = {};
}

ProviderStats g_provider;
unsigned g_providerCases[7]{};
unsigned g_providerRentalChecks = 0, g_providerEventChecks = 0;
ProviderStats g_providerScoreStats;

// The observer of object::building_api::GetProviderScore: the game's answer is checked against the building's own table.
void ObserveProviderScore(const Building* building, int32_t slot, int32_t preference, float result)
{
    auto def = Def(building);
    const char* commodity = world::world_params::GetCommodityName(game::GetWorldParams(), def->m_Commodity[preference]);
    bool event = ascii::EqualsNoCase(commodity, "LIVEVENT");
    float expected;
    if (event) {
        const object::BuildingEvents* events = building->m_events;
        expected = events->m_slots[events->m_CurrentSlot].m_intensity[preference != 0];
    } else {
        expected = building->m_Commod_Intensity[slot][preference];
    }
    ++g_providerScoreStats.calls;
    ++g_providerScoreStats.branch[event ? 2 : preference == 0 ? 0 : 1];
    Expect(g_providerScoreStats, "provider slot score", Close(result, expected));
}

// The rules of commodity_providers::FindProvider applied to the same provider table, buildings and counters;
// only the flags/counters are contractual on an unsuccessful search of a non-empty list (the result there is unspecified).
void CheckSearch(const object::ProviderSearchRecord& call)
{
    commodity::Providers* providers = game::GetCommodityProviders();
    object::Manager* manager = game::GetObjectManager();
    const int family = call.search.family;
    const int preference = call.search.preference;
    const int* tile = call.tile;
    const std::vector<ProviderRecord> records = providers->records[family];
    const int count = static_cast<int>(records.size());
    int wantClosed = call.closedBefore, wantBeach = call.beachClosedBefore;
    int wantBroken = 0;
    bool wantRetry = false;
    struct Candidate {
        int id, slot;
        float score;
    };
    std::vector<Candidate> candidates;
    for (int i = 0; i < count; ++i) {
        const auto& record = records[i];
        auto building = static_cast<Building*>(object::object_manager::ObjectById(manager, record.id));
        if (record.id == call.search.excludedId) {
            wantRetry = true;
            continue;
        }
        bool usable = object::commodity_providers::IsSlotUsable(providers, record.id, record.slot);
        bool open = object::building_api::IsOpen(building);
        if (open && usable) {
            float quality = object::building_api::GetProviderScore(building, record.slot, preference != 0);
            int distance = std::abs(tile[0] - record.x) + std::abs(tile[1] - record.y);
            candidates.push_back({record.id, record.slot, 1.0f - distance / 256.0f + quality + 0.001f});
        } else if ((3 - static_cast<int>(building->m_RepairState)) * 0.33f == 3.0f) {
            ++wantBroken;
        } else if (object::BuildingIsUserOrPowerClosedDuringHours(building)) {
            ++wantClosed;
        } else if (!usable) {
            ++wantBeach;
        } else {
            wantRetry = true;
        }
    }
    ++g_provider.calls;
    ++g_provider.branch[count == 0 ? 0 : call.found ? 1 : 2];
    if (count == 0) {
        Expect(g_provider, "empty provider list outputs",
                call.result == 0 && !call.found && !call.retry && !call.broken && !call.closed && !call.beachClosed);
        return;
    }
    Expect(g_provider, "provider refusal counters",
            call.broken == wantBroken && call.closed == wantClosed && call.beachClosed == wantBeach);
    // The game's rand()/32767 term subtracts [0, 0.0009]. Check the winner's possible score interval without consuming another random number.
    float best = -1.0f, chosen = -1e9f;
    for (const auto& candidate : candidates) {
        best = std::max(best, candidate.score - 0.0009f);
        if (candidate.id == call.result && candidate.slot == call.slot) chosen = std::max(chosen, candidate.score);
    }
    bool definitelyFound = best > -1.0f;
    bool possiblyFound = std::any_of(candidates.begin(), candidates.end(), [](const auto& c) { return c.score > -1.0f; });
    Expect(g_provider, "provider score/filter", call.found ? chosen >= best - 1e-5f && chosen > -1.0f : !definitelyFound);
    Expect(g_provider, "provider success threshold", !call.found || possiblyFound);
    Expect(g_provider, "provider retry flag", call.retry == (call.found ? 0 : wantRetry));
}

// The search through the facade, then the model's verdict arrives through the observer.

void ForceProviders(int guestId)
{
    commodity::Providers* providers = game::GetCommodityProviders();
    // Borrow an existing provider; table, definition, scores and closure flags
    // are restored before the next frame. Refusals may append its thought log.
    int family = -1;
    for (int f = 0; f < commodity::kFamilies; ++f)
        if (f != 10 && providers->Count(f)) {
            family = f;
            break;
        }
    if (family < 0) {
        OBL_LOG("autotest: FAILED provider-force needs a provider");
        return;
    }
    const std::vector<ProviderRecord> savedRecords = providers->records[family];
    ProviderRecord records[2]{savedRecords[0], savedRecords[0]};
    auto building = static_cast<Building*>(object::object_manager::ObjectById(game::GetObjectManager(), records[0].id));
    int repair = building->m_RepairState;
    int closed = building->m_OpenStatus;
    uint8_t power = building->m_closedForPower, user = building->m_userClose;
    uint32_t complaints = providers->complaints[family];
    float scores[8];
    float* intensity = &building->m_Commod_Intensity[0][0];
    std::memcpy(scores, intensity, sizeof(scores));
    for (int i = 0; i < 8; ++i) intensity[i] = 0.125f * (i + 1);
    for (int slot = 0; slot < 4; ++slot)
        for (int preference : {0, 1}) object::building_api::GetProviderScore(building, slot, preference);
    std::memcpy(intensity, scores, sizeof(scores));
    tables::WorldParams* params = game::GetWorldParams();
    auto def = Def(building);
    int commodity[2]{def->m_Commodity[0], def->m_Commodity[1]};
    const int alcohol = world::world_params::GetCommodityFamilyId(params, "ALCOHOL");
    const int beer = building->m_BeerType;
    const float priceFactor = building->m_Commod_PriceFactor[0];
    for (int i = 0; i < params->m_numCommodities; ++i) {
        if (params->m_commodities[i].m_family != alcohol) continue;
        def->m_Commodity[0] = i;
        building->m_BeerType = -1;
        building->m_Commod_PriceFactor[0] = 2.0f;
        Expect(g_provider, "no beer reference price", object::SlotReferencePrice(*building, 0) == 0.0);
        building->m_BeerType = 0;
        Expect(g_provider, "beer reference price",
                object::SlotReferencePrice(*building, 0) == static_cast<double>(params->m_beers[0].m_Price) * 2.0);
        break;
    }
    def->m_Commodity[0] = commodity[0];
    building->m_BeerType = beer;
    building->m_Commod_PriceFactor[0] = priceFactor;
    auto savedEvents = building->m_events;
    object::BuildingEvents events{};
    building->m_events = &events;
    def->m_Commodity[0] = def->m_Commodity[1] = world::world_params::GetCommodityId(params, "LIVEVENT");
    for (int current = 0; current < 3; ++current) {
        events.m_CurrentSlot = current;
        events.m_slots[current].m_intensity[0] = 0.2f * (current + 1);
        events.m_slots[current].m_intensity[1] = 0.1f * (current + 1);
        for (int preference : {0, 1}) {
            object::building_api::GetProviderScore(building, 0, preference);
            ++g_providerEventChecks;
        }
    }
    building->m_events = savedEvents;
    def->m_Commodity[0] = commodity[0];
    tables::OpeningHoursDef& hours = params->m_openingHours[def->m_OpeningHours];
    float openHour = hours.m_open, closeHour = hours.m_close;
    hours.m_open = 0.0f;
    hours.m_close = 24.0f;
    def->m_Commodity[1] = commodity[1];
    std::vector<std::pair<map::Area*, bool>> areas;
    for (map::Area& node : game::GetGrid()->m_seaAreas) areas.emplace_back(&node, node.m_shut);
    for (const char* name : {"LILOS", "SURFHIRE", "SURFBOARD", "BOATHIRE", "SNORKEL", "PEDHIRE", "LIVEVENT"}) {
        def->m_Commodity[0] = world::world_params::GetCommodityId(params, name);
        for (int shut : {0, 1}) {
            for (auto [node, flag] : areas) node->m_shut = shut != 0;
            bool usable = object::commodity_providers::IsSlotUsable(providers, records[0].id, 0);
            Expect(g_provider, "rental sea area gate", usable == (ascii::EqualsNoCase(name, "LIVEVENT") || !shut));
            ++g_providerRentalChecks;
        }
    }
    for (auto [node, flag] : areas) node->m_shut = flag;
    def->m_Commodity[0] = commodity[0];
    int tile[2]{records[0].x, records[0].y};
    for (int test = 0; test < 6; ++test) {
        building->m_OpenStatus = test == 3 || test == 4;
        building->m_RepairState = test == 4 ? 3 : 0;
        building->m_userClose = test == 3;
        building->m_closedForPower = 0;
        records[0].x = tile[0] + (test == 2 ? 10000 : 0);
        records[1].x = tile[0] + 100;
        providers->records[family].assign(records, records + (test == 0 ? 0 : test == 5 ? 2 : 1));
        for (int preference : {0, 1}) {
            int slot = -777, broken = 7, closedCount = 0, beach = 0;
            char found = 0, retry = 0;
            int result = object::commodity_providers::FindProvider(providers, &slot, &found, &broken, &closedCount, &beach, &retry,
                    preference, family, tile, test == 1 ? records[0].id : -1, guestId);
            ++g_providerCases[test];
            if (test == 0) Expect(g_provider, "empty provider complaints", providers->complaints[family] == complaints + preference + 1);
            if (test == 1) Expect(g_provider, "excluded provider", !found && retry);
            if (test == 2) Expect(g_provider, "distant provider below -1", !found && !retry);
            if (test == 3) Expect(g_provider, "user closure counted during hours", !found && closedCount == 1 && !retry);
            if (test == 4) Expect(g_provider, "broken branch unreachable for RepairState 3", !broken && retry);
            if (test == 5) Expect(g_provider, "nearer provider wins", found && result == records[0].id);
        }
    }
    // A rental whose sea area is shut: the provider is not usable, not user-closed, so the beach counter and the BEACH_CLOSED thought follow.
    {
        const int slotOfRecord = records[0].slot;
        const int rented = def->m_Commodity[slotOfRecord];
        def->m_Commodity[slotOfRecord] = world::world_params::GetCommodityId(params, "LILOS");
        building->m_OpenStatus = 0;
        building->m_RepairState = 0;
        building->m_userClose = 0;
        records[0].x = tile[0];
        providers->records[family].assign(records, records + 1);
        for (auto [node, flag] : areas) node->m_shut = true;
        for (int preference : {0, 1}) {
            int slot = -777, broken = 7, closedCount = 0, beach = 0;
            char found = 0, retry = 0;
            object::commodity_providers::FindProvider(
                    providers, &slot, &found, &broken, &closedCount, &beach, &retry, preference, family, tile, -1, guestId);
            ++g_providerCases[6];
            Expect(g_provider, "shut sea area counts as beach closed", !found && !retry && beach == 1 && closedCount == 0);
        }
        for (auto [node, flag] : areas) node->m_shut = flag;
        def->m_Commodity[slotOfRecord] = rented;
    }
    providers->records[family] = savedRecords;
    providers->complaints[family] = complaints;
    building->m_RepairState = repair;
    building->m_OpenStatus = closed;
    building->m_closedForPower = power;
    building->m_userClose = user;
    hours.m_open = openHour;
    hours.m_close = closeHour;
}

} // namespace

bool RegisterCommodityProviderTrace()
{
    object::SetProviderSearchObserver(&CheckSearch);
    object::building_api::g_providerScoreObserver = &ObserveProviderScore;
    return true;
}

bool ExecuteCommodityProviderCommand(const std::string& name, const std::vector<std::string>&)
{
    if (name == "provider-force") {
        object::Manager* objects = game::GetObjectManager();
        if (objects && objects->m_guests.size() > 0)
            ForceProviders(static_cast<int>(objects->m_guests[0]->m_id));
        else
            OBL_LOG("autotest: FAILED provider-force needs a guest");
    } else if (name == "provider-report") {
        OBL_LOG("autotest: provider-cases empty=%u excluded=%u distant=%u userClosed=%u broken=%u ranking=%u beachClosed=%u",
                g_providerCases[0], g_providerCases[1], g_providerCases[2], g_providerCases[3], g_providerCases[4], g_providerCases[5],
                g_providerCases[6]);
        OBL_LOG("autotest: provider-extra rentalChecks=%u eventChecks=%u", g_providerRentalChecks, g_providerEventChecks);
        Report("provider-score", g_providerScoreStats, "first/second/event");
        Report("provider", g_provider, "empty/found/refused");
    } else {
        return false;
    }
    return true;
}

} // namespace obl::test
