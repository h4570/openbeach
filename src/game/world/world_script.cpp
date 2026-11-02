#include "game/game/rng.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/music_player.h"
#include "game/ui/sound_types.h"
#include "game/ui/sound_api.h"
#include "game/objects/script_value_convert.h"
#include "game/game/game_api.h"
#include "game/game/diagnostics.h"
#include "game/weather/weather_state.h"
#include "game/world/world_script.h"
#include "game/objects/script_text.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"
#include "game/events/event_manager.h"
#include "game/weather/weather.h"

#include "game/map/grid_areas.h"
#include "game/map/grid_state.h"
#include "game/game/game_accessors.h"
#include "game/world/world_script_host_game.h"
#include "game/ferry/ferry.h"
#include "game/map/path_queries.h"
#include "game/repair/repair_state.h"
#include "game/game/scenario_outcome.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "core/ascii_case.h"
#include "core/log.h"
#include "game/objects/script_arith.h"
#include "game/objects/script_message.h"
#include "game/world/world_script_delegate.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/world/world_control.h"
#include "game/world/world_receive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/ui/map_view.h"

namespace obl::world {
namespace {

using layout::World;
using object::Building;
using object::Guest;
using script::ScriptValue;

// Types of a script value (Entity::getValue uses the same numbers).
constexpr int32_t kByte = 0, kString = 1, kInt = 2, kFloat = 3;

// Class bytes of object_manager::CountByClass behind CleanerCount ... RepCount.
constexpr char kClassCleaner = 0xb, kClassMechanic = 8, kClassSecurity = 0xc, kClassLifeguard = 7, kClassRep = 9;
// RepairState of a broken building that waits for a mechanic; AllBuildingsOperating looks for it.
constexpr int kRepairStateBroken = 3;
// Guest states counted by GuestPartyCount (party / dancing states).
constexpr int kGuestStateParty1 = 0x1e, kGuestStateParty2 = 0x3f;
using BuildingJobTest = bool (*)(const Building*);
const BuildingJobTest kHasBuildJob{&object::building_api::HasBuildJob};
const BuildingJobTest kHasDemolishJob{&object::building_api::HasDemolishJob};

int32_t FloatBits(float value)
{
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// A byte result leaves the upper three bytes as they were in the `data` argument slot; the interpreter
// only reads the low byte; preserve the full result for callers that observe the entire word.
int32_t ByteResult(bool value, const int32_t* data)
{
    return static_cast<int32_t>((reinterpret_cast<uintptr_t>(data) & ~uintptr_t{0xff}) | (value ? 1u : 0u));
}

Building* BuildingAt(const object::Manager& objects, uint32_t i)
{
    return static_cast<Building*>(objects.m_buildings[i]);
}

Guest* GuestAt(const object::Manager& objects, uint32_t i)
{
    return static_cast<Guest*>(objects.m_guests[i]);
}

// Finished buildings of a list: neither under construction nor being demolished. `onlyBuiltByUser` is the BarsBuilt variant that also
// requires the player to have built it.
int CountReady(object::Base* const* list, uint32_t count, bool onlyBuiltByUser)
{
    int ready = 0;
    for (uint32_t i = 0; i < count; ++i) {
        auto* building = static_cast<Building*>(list[i]);
        if (kHasBuildJob(building) || kHasDemolishJob(building)) continue;
        if (onlyBuiltByUser && !building->m_builtByUser) continue;
        ++ready;
    }
    return ready;
}

// Finished instances of one building name (a script's key).
int CountReady(const char* name, bool onlyBuiltByUser)
{
    const auto list = object::object_manager::FindObjectsByName(game::GetObjectManager(), name);
    return CountReady(list.data(), static_cast<uint32_t>(list.size()), onlyBuiltByUser);
}

// Average of one opinion slot over all guests, double style: the sum and the quotient are 53-bit values rounded to float on
// the store. The result is also cached in the World (the GUI reads the cache), even when there are no guests.
float AverageOpinion(const object::Manager& objects, int slot)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) sum += GuestAt(objects, i)->m_opinion[slot];
    if (objects.m_guests.size() != 0) sum = sum / static_cast<int32_t>(objects.m_guests.size());
    return static_cast<float>(sum);
}

ScriptValue Average(World& world, int slot, float& cache)
{
    cache = AverageOpinion(*world.m_objectManager, slot);
    return {kFloat, FloatBits(cache)};
}

// ResortRating (and world::GetResortRating): mean happiness x 100 truncated, 100 from 96 up. The unsigned comparison sends
// negative results to 100 as well.
int ResortRating(const object::Manager& objects)
{
    if (objects.m_guests.size() == 0) return 0;
    double sum = 0.0;
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) sum += GuestAt(objects, i)->m_happiness;
    double scaled = sum / static_cast<int32_t>(objects.m_guests.size()) * 100.0f;
    uint32_t rating = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(scaled)));
    return rating >= 0x60u ? 100 : static_cast<int>(rating);
}

ScriptValue AllBuildingsOperating(const World& world, const int32_t* data)
{
    const object::Manager& objects = *world.m_objectManager;
    bool operating = true;
    for (uint32_t i = 0; operating && i < objects.m_buildings.size(); ++i)
        operating = BuildingAt(objects, i)->m_RepairState != kRepairStateBroken;
    return {kByte, ByteResult(operating, data)};
}

// DiffConstructCount: distinct definitions among the buildings the player built.
ScriptValue DiffConstructCount(World& world)
{
    const object::Manager& objects = *world.m_objectManager;
    std::set<const void*> definitions;
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) {
        const Building* building = BuildingAt(objects, i);
        if (building->m_builtByUser) definitions.insert(building->m_definition);
    }
    world.m_diffBuildingsConstructed = static_cast<int>(definitions.size());
    return {kInt, world.m_diffBuildingsConstructed};
}

// BuildTreeExist: every allowed building type has a finished instance. The number of missing types is cached.
ScriptValue BuildTreeExist(World& world, const int32_t* data)
{
    const tables::WorldParams* params = game::GetWorldParams();
    bool allExist = true;
    int present = 0;
    for (int i = 0; i < world.m_numValidBuildings; ++i) {
        const tables::BuildingDef& def = params->m_buildings[world.m_validBuildingIDs[i]];
        if (CountReady(def.m_key.c_str(), false) == 0)
            allExist = false;
        else
            ++present;
    }
    world.m_buildingsLeftToBuild = world.m_numValidBuildings - present;
    return {kByte, ByteResult(allExist, data)};
}

ScriptValue BarsBuilt(World& world)
{
    int total = 0;
    // The five bar keys (BEACHBAR, POOLBAR, BAR1, BAR2 are Bar; COCKTAIL is CocktailBar) by behavior, in list order.
    for (BuildingBehavior behavior : {BuildingBehavior::Bar, BuildingBehavior::CocktailBar}) {
        const auto list = obl::object_manager::FindBuildings(*game::GetObjectManager(), behavior);
        total += CountReady(list.data(), static_cast<uint32_t>(list.size()), true);
    }
    world.m_barsConstructed = total;
    return {kInt, total};
}

ScriptValue GuestPartyCount(World& world)
{
    const object::Manager& objects = *world.m_objectManager;
    int count = 0;
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) {
        int state = GuestAt(objects, i)->m_state;
        if (state == kGuestStateParty1 || state == kGuestStateParty2) ++count;
    }
    world.m_partyCount = count;
    return {kInt, count};
}

ScriptValue GuestPissedCount(World& world)
{
    const object::Manager& objects = *world.m_objectManager;
    int count = 0;
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i)
        if (GuestAt(objects, i)->m_drunk > 0.5f) ++count;
    world.m_pissedCount = count;
    return {kInt, count};
}

} // namespace

ScriptValue GetScriptValue(World& world, int key, const int32_t* data)
{
    object::Manager* objects = world.m_objectManager;
    // Flag<n>/Var<n>/ScenarioObj<n> index with the parsed number and no range check; the scripts keep it in range.
    auto index = [data]() { return *data; };
    switch (key) {
    case 0x36: return {kString, script::text::Id(reinterpret_cast<const char*>(data))}; // "text": the data is the text, the value its id
    case 0xb9: return {kInt, world.m_numGuests}; // GuestCount
    case 0xba: return {kInt, world.m_repairManager->breakdownCount};
    case 0xbb: return {kInt, world.m_repairManager->vandalismCount};
    case 0xbc: return {kInt, world.m_scriptFlags & static_cast<int32_t>(1u << (index() & 31))}; // Flag<n>
    case 0xbd: return {kInt, object::object_manager::CountByClass(objects, kClassCleaner)};
    case 0xbe: return {kInt, object::object_manager::CountByClass(objects, kClassMechanic)};
    case 0xbf: return {kInt, object::object_manager::CountByClass(objects, kClassSecurity)};
    case 0xc0: return {kInt, object::object_manager::CountByClass(objects, kClassLifeguard)};
    case 0xc1: return {kInt, object::object_manager::CountByClass(objects, kClassRep)};
    case 0xc2: return {kInt, (world.m_scriptVars + 0)[index()]}; // Var<n>
    case 0xc5: return {kFloat, FloatBits(world.m_demoTime)}; // DemoTime
    case 0xc6: return {kInt, static_cast<int32_t>((world.m_objectiveStatus + 0)[index()])}; // ScenarioObj<n>
    case 0xc7: return {kByte, game::GetGame()->m_sandbox}; // SandBox
    case 0xc8: return {kInt, world.m_numLitter};
    case 0xc9: return {kInt, world.m_totalGuestUnhappyDepart[1] + world.m_totalGuestUnhappyDepart[0]};
    case 0xca: return {kFloat, FloatBits(world.m_incomeToday - world.m_costsToday)}; // ProfitToday
    case 0xcb: return {kInt, world.m_buildingsConstructed};
    case 0xcc: return AllBuildingsOperating(world, data);
    case 0xcd: return GuestPartyCount(world);
    case 0xce: return Average(world, 2, world.m_averageTotty); // GuestTottyLevel
    case 0xcf: return Average(world, 0, world.m_averageBeauty); // GuestBeautyLevel
    case 0xd0: return DiffConstructCount(world);
    case 0xd1: return BuildTreeExist(world, data);
    case 0xd4: return {kInt, CountReady(reinterpret_cast<const char*>(data), false)}; // BuildingCount<BUILDING>
    case 0xd6: return {kInt, world.m_guestsEaten};
    case 0xd7: return {kInt, world.m_guestsDrowned};
    case 0xd8: return {kFloat, FloatBits(world.m_totalFines)};
    case 0xd9: return {kInt, world.m_shagCount};
    case 0xda: return GuestPissedCount(world);
    case 0xdc: return {kFloat, FloatBits(world.m_incomeToday)};
    case 0xdd: return Average(world, 6, world.m_averageSafety); // GuestSafetyLevel
    case 0xde: return BarsBuilt(world);
    case 0xe2: return {kInt, -2}; // Me
    case 0xe8: return {kFloat, *data}; // a number: the float's bits
    case 0xea: return {kFloat, FloatBits(world.m_gameDayTime)}; // GameTime
    case 0xec: return {kInt, ResortRating(*objects)};
    case 0xed: return {kInt, world.m_gameDay};
    case 0xee: return {kFloat, FloatBits(world.m_player->m_cash)}; // PlayerCash
    default: return {key, static_cast<int32_t>(reinterpret_cast<uintptr_t>(data))};
    }
}

namespace {

// The conversion helpers of the script interpreter (shared with ScriptProgram, src/game/objects/script_value_convert.h):
// ToInt(type, bits) is 0 for kinds other than byte/int/float, a float is truncated; ToFloat converts an int as unsigned.
int32_t ToInt(int type, int32_t bits)
{
    return script_value::ToInt(type, static_cast<uint32_t>(bits));
}

float ToFloat(int type, int32_t bits)
{
    return static_cast<float>(script_value::ToFloat(type, static_cast<uint32_t>(bits)));
}

float BitsToFloat(int32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Name -> id lookups of WorldParams; an unknown name is a FatalError of the game, as before.

constexpr float kOneOverTwentyFour = 0.041666668f; // 1/24 as a float, not double: the float rounding is part of the world hashes
constexpr float kNightStartHour = 19.0f;

// GameTime: only while the scenario's STATE_WORLD_STARTUP runs. The day length scales the script's hour to game
// seconds; the night test uses the unrounded double product, the stored fields are rounded to float.
void SetGameTime(World& world, int32_t bits)
{
    auto* script = static_cast<world_script::WorldScript*>(world.m_worldScript);
    const uint32_t current = script->m_currentState;
    if (current != static_cast<uint32_t>(WorldScriptFindState(*script, "STATE_WORLD_STARTUP"))) return;
    int32_t dayLength = 0;
    game::game_parameters::GetInt(game::game_parameters::Current(), &dayLength, "iDAY_LENGTH_IN_SECONDS");
    double day = dayLength;
    double seconds = day * kOneOverTwentyFour * BitsToFloat(bits);
    world.m_gameTime = static_cast<float>(seconds);
    double hour = 24.0f / day * seconds;
    float hourStored = static_cast<float>(hour);
    world.m_gameDayTime = hourStored;
    world.m_previousDayTime = hourStored;
    world.m_lastGameDayTime = hourStored;
    static_cast<audio::MusicPlayer*>(game::GetGuiManager()->m_musicPlayer)->SetNight(hour >= kNightStartHour ? 1 : 0);
}

} // namespace

void SetScriptValue(World& world, int key, const void* index, int type, int32_t bits)
{
    tables::WorldParams* params = game::GetWorldParams();
    // Flag<n>/Var<n>/ScenarioObj<n> index with the parsed number and no range check.
    auto number = [index]() { return *static_cast<const int32_t*>(index); };
    switch (key) {
    case 0xbc: { // Flag<n>: the raw bits are compared as a float, so integer 0 and 0.0 both clear it, NaN clears too
        int32_t bit = static_cast<int32_t>(1u << (number() & 31));
        float value = BitsToFloat(bits);
        if (value < 0.0f || value > 0.0f)
            world.m_scriptFlags |= bit;
        else
            world.m_scriptFlags &= ~bit;
        break;
    }
    case 0xc2: { // Var<n>
        int32_t n = number();
        (world.m_scriptVars + 0)[n] = ToInt(type, bits);
        break;
    }
    case 0xc5: // DemoTime
        world.m_demoTime = ToFloat(type, bits);
        world.m_demoTimeSet = 1;
        break;
    case 0xc6: { // ScenarioObj<n>: a change refreshes the Objectives window
        int32_t n = number();
        int32_t status = ToInt(type, bits);
        if (static_cast<int32_t>((world.m_objectiveStatus + 0)[n]) != status) {
            (world.m_objectiveStatus + 0)[n] = static_cast<uint32_t>(status);
            game::GetGuiManager()->InvalidateDialog("Objectives", 0);
        }
        break;
    }
    case 0xd2: { // BeerActive<BEER>
        int beer = world_params::GetBeerId(params, static_cast<const char*>(index));
        params->m_beers[beer].m_scriptFlag = ToInt(type, bits) != 0 ? 1 : 0;
        break;
    }
    case 0xd3: { // DefaultBeer<BUILDING>: the value is the beer's name here, not a number
        int beer = world_params::GetBeerId(params, script::text::Of(bits));
        int building = world_params::GetBuildingId(params, static_cast<const char*>(index));
        params->m_buildings[building].m_DefaultBeer = beer;
        break;
    }
    case 0xd5: { // EventActive<SHOW>
        int event = world_params::GetEventId(params, static_cast<const char*>(index));
        params->m_events[event].m_available = ToInt(type, bits) != 0 ? 1 : 0;
        break;
    }
    case 0xdb: // WeatherDayOffset
        game::GetWeather()->m_dayOffset = ToInt(type, bits);
        break;
    case 0xdf: // DisasterDayOffset
        disaster::event_facade::Current()->dayOffset = ToInt(type, bits);
        break;
    case 0xea: // GameTime
        SetGameTime(world, bits);
        break;
    case 0xee: // PlayerCash
        world.m_player->m_cash = ToFloat(type, bits);
        break;
    default: break;
    }
}

namespace {

// The condition operator numbers of the parser (the `op` of EvalCondition; `&&` and `||` are the first two).
constexpr int32_t kOpAnd = 0, kOpOr = 1, kOpGreater = 2, kOpLess = 3, kOpEqual = 4, kOpLessEqual = 5, kOpGreaterEqual = 6, kOpNotEqual = 7;
// Right-hand keys with a meaning of their own: a quoted text (the same key 0x36 GetScriptValue answers with the text)
// and "no right operand" for a bare `If ( value )`.
constexpr int32_t kTextKey = 0x36, kNoRightOperand = 0x110;

// A script value read as a float comes back without rounding (an int is loaded as an unsigned 64-bit value), and the
// interpreter keeps the right-hand operand of a comparison at that width; a double holds every such value exactly.
double ToFloatWide(int type, int32_t bits)
{
    return script_value::ToFloat(type, static_cast<uint32_t>(bits));
}

// Compare of the right value against the stored left float: below = right < left, equal = right == left; an
// unordered pair sets both, so a NaN counts as "less" and as "equal" for the tests below.
struct Ordering {
    bool below, equal;
};

Ordering Compare(double right, float left)
{
    if (std::isnan(right) || std::isnan(left)) return {true, true};
    return {right < left, right == left};
}

} // namespace

bool EvalCondition(World& world, int32_t leftKey, int32_t* leftData, int32_t rightKey, int32_t* rightData, int32_t op)
{
    if (rightKey == kNoRightOperand) { // `If ( value )`: true for a non-zero byte/int/float, never for a string
        const ScriptValue value = GetScriptValue(world, leftKey, leftData);
        switch (value.type) {
        case kByte: return static_cast<uint8_t>(value.bits) != 0;
        case kInt: return value.bits != 0;
        case kFloat: return !Compare(0.0, BitsToFloat(value.bits)).equal; // a compare with 0.0
        default: return false;
        }
    }
    if (rightKey == kTextKey) { // a quoted text on the right: only == and != exist, the left side's bits are the text's id
        if (op != kOpEqual && op != kOpNotEqual) return false;
        const ScriptValue left = GetScriptValue(world, leftKey, leftData);
        const bool same = ascii::EqualsNoCase(script::text::Of(left.bits), reinterpret_cast<const char*>(rightData));
        return op == kOpEqual ? same : !same;
    }
    if (op < kOpAnd || op > kOpNotEqual) return false;
    // Both sides are evaluated first (the queries cache values in the World), then compared.
    const ScriptValue left = GetScriptValue(world, leftKey, leftData);
    const ScriptValue right = GetScriptValue(world, rightKey, rightData);
    if (op == kOpAnd) return ToInt(left.type, left.bits) != 0 && ToInt(right.type, right.bits) != 0;
    if (op == kOpOr) return ToInt(left.type, left.bits) != 0 || ToInt(right.type, right.bits) != 0;
    const Ordering c = Compare(ToFloatWide(right.type, right.bits), ToFloat(left.type, left.bits));
    switch (op) {
    case kOpGreater: return c.below;
    case kOpLess: return !c.below && !c.equal;
    case kOpEqual: return c.equal;
    case kOpLessEqual: return !c.below;
    case kOpGreaterEqual: return c.below || c.equal;
    default: return !c.equal; // kOpNotEqual
    }
}

namespace {

// Call counters (`call-report world.get-script-value`); `g_driveGet` only carries `call-drive world.get-script-value`.
test::CallCounter g_driveGet("world.get-script-value.drive");
test::CallCounter g_getCalls("world.get-script-value");

// World fields a query writes besides its answer (the GUI and the scenario read them later), as raw words.
using Caches = std::array<int32_t, 8>;

Caches ReadCaches(const World& w)
{
    Caches c;
    std::memcpy(&c[0], &w.m_diffBuildingsConstructed, 4);
    std::memcpy(&c[1], &w.m_barsConstructed, 4);
    std::memcpy(&c[2], &w.m_buildingsLeftToBuild, 4);
    std::memcpy(&c[3], &w.m_partyCount, 4);
    std::memcpy(&c[4], &w.m_pissedCount, 4);
    std::memcpy(&c[5], &w.m_averageTotty, 4);
    std::memcpy(&c[6], &w.m_averageBeauty, 4);
    std::memcpy(&c[7], &w.m_averageSafety, 4);
    return c;
}

void WriteCaches(World& w, const Caches& c)
{
    std::memcpy(&w.m_diffBuildingsConstructed, &c[0], 4);
    std::memcpy(&w.m_barsConstructed, &c[1], 4);
    std::memcpy(&w.m_buildingsLeftToBuild, &c[2], 4);
    std::memcpy(&w.m_partyCount, &c[3], 4);
    std::memcpy(&w.m_pissedCount, &c[4], 4);
    std::memcpy(&w.m_averageTotty, &c[5], 4);
    std::memcpy(&w.m_averageBeauty, &c[6], 4);
    std::memcpy(&w.m_averageSafety, &c[7], 4);
}

// Returns `out` (the interpreter reads the result through the returned pointer).
int32_t* ReadScriptValueEntry(World* world, int32_t* out, int key, int32_t* data)
{
    g_getCalls.Count();
    ScriptValue value = GetScriptValue(*world, key, data);
    out[0] = value.type;
    out[1] = value.bits;
    return out;
}

// ---- world::SetScriptValue --------------------------------------------------------------------------------------------

test::CallCounter g_driveSet("world.set-script-value.drive");
test::CallCounter g_setCalls("world.set-script-value");

// Everything world::SetScriptValue can write, as raw words: the World's script/objective/clock fields, the cash, the two
// day offsets, and the beer/show/default-beer tables of WorldParams.
using SetState = std::vector<int32_t>;

// The day/night flag of the music player (MusicPlayer::SetNight stores it first); a stand-in when the GUI has none.
audio::MusicPlayer* MusicPlayerOf()
{
    static audio::MusicPlayer none{};
    auto* gui = game::GetGuiManager();
    return gui && gui->m_musicPlayer ? static_cast<audio::MusicPlayer*>(gui->m_musicPlayer) : &none;
}

// The selected object of the GUI (MoveCamera clears it); a stand-in when the GUI does not exist.
ui::GuiManager* GuiSelectionOf()
{
    static ui::GuiManager none{};
    auto* gui = game::GetGuiManager();
    return gui ? gui : &none;
}

SetState CaptureSetState(const World& w)
{
    SetState s;
    s.push_back(w.m_scriptFlags);
    s.insert(s.end(), w.m_scriptVars, w.m_scriptVars + 0x20);
    s.push_back(FloatBits(w.m_demoTime));
    s.push_back(w.m_demoTimeSet);
    for (uint32_t status : w.m_objectiveStatus) s.push_back(static_cast<int32_t>(status));
    for (float time : {w.m_gameTime, w.m_gameDayTime, w.m_previousDayTime, w.m_lastGameDayTime}) s.push_back(FloatBits(time));
    s.push_back(FloatBits(w.m_player->m_cash));
    s.push_back(game::GetWeather()->m_dayOffset);
    s.push_back(disaster::event_facade::Current()->dayOffset);
    const tables::WorldParams* params = game::GetWorldParams();
    for (const tables::BeerDef& beer : params->m_beers) s.push_back(beer.m_scriptFlag);
    for (const tables::EventDef& event : params->m_events) s.push_back(event.m_available);
    for (const tables::BuildingDef& building : params->m_buildings) s.push_back(building.m_DefaultBeer);
    s.push_back(MusicPlayerOf()->night);
    s.push_back(GuiSelectionOf()->m_selection);
    return s;
}

void RestoreSetState(World& w, const SetState& s)
{
    size_t i = 0;
    auto bits = [&]() { return s[i++]; };
    auto real = [&]() { return BitsToFloat(s[i++]); };
    w.m_scriptFlags = bits();
    for (int32_t& var : w.m_scriptVars) var = bits();
    w.m_demoTime = real();
    w.m_demoTimeSet = static_cast<uint8_t>(bits());
    for (uint32_t& status : w.m_objectiveStatus) status = static_cast<uint32_t>(bits());
    w.m_gameTime = real();
    w.m_gameDayTime = real();
    w.m_previousDayTime = real();
    w.m_lastGameDayTime = real();
    w.m_player->m_cash = real();
    game::GetWeather()->m_dayOffset = bits();
    disaster::event_facade::Current()->dayOffset = bits();
    tables::WorldParams* params = game::GetWorldParams();
    for (tables::BeerDef& beer : params->m_beers) beer.m_scriptFlag = static_cast<uint8_t>(bits());
    for (tables::EventDef& event : params->m_events) event.m_available = static_cast<uint8_t>(bits());
    for (tables::BuildingDef& building : params->m_buildings) building.m_DefaultBeer = bits();
    MusicPlayerOf()->night = bits();
    GuiSelectionOf()->m_selection = bits();
}

void WriteScriptValueEntry(World* world, int key, const void* index, int type, int32_t value)
{
    g_setCalls.Count();
    SetScriptValue(*world, key, index, type, value);
}

// ---- EvalCondition ----------------------------------------------------------------------------------------------------

test::CallCounter g_driveEval("world.script-eval-condition-fixture");
test::CallCounter g_evalCalls("world.script-eval-condition");

// A member function of the WorldScript (unused), five arguments: the left key/data, the right key/data and the operator, all taken from the `If`
// command's payload.
bool EvaluateConditionEntry(
        world_script::WorldScript*, int32_t leftKey, int32_t* leftData, int32_t rightKey, int32_t* rightData, int32_t op)
{
    g_evalCalls.Count();
    return EvalCondition(*game::GetWorld(), leftKey, leftData, rightKey, rightData, op);
}

// ---- ExecuteCommand --------------------------------------------------------------------------------------

// `ExecuteCommandEntry(ScriptCommand*)` takes the command node as its argument. The `If` command and the runners (RunOnUpdateList, DispatchMessage, Start*) call it for every node.
test::CallCounter g_driveExec("world.script-execute-command-fixture");
// Commands the dispatcher executed.
test::CallCounter g_execCalls("world.script-execute-command");

// The two queues the message commands feed (Post delivers at once unless the world is paused, PostDelayed waits `delay` seconds).

int32_t Truncate(int32_t floatBits)
{
    return ToInt(kFloat, floatBits);
}

// The interpreter's queries, writes and conditions, counted like the entry points.
ScriptValue Query(World& world, int key, void* data)
{
    g_getCalls.Count();
    return GetScriptValue(world, key, static_cast<const int32_t*>(data));
}

void Store(World& world, int key, const void* index, ScriptValue value)
{
    g_setCalls.Count();
    SetScriptValue(world, key, index, value.type, value.bits);
}

bool Condition(World& world, const world_script::ScriptIfData& data)
{
    g_evalCalls.Count();
    return EvalCondition(world, data.m_leftKey, data.m_leftData, data.m_rightKey, data.m_rightData, data.m_op);
}

// Add/Minus/Divide/Multiply <variable> <value>: read both sides, combine by the left value's kind, write the left side back.
void ExecuteArithmetic(World& world, script::Arith op, const world_script::ScriptAssignData& data)
{
    const ScriptValue left = Query(world, data.m_leftKey, data.m_leftData);
    const ScriptValue right = Query(world, data.m_rightKey, data.m_rightData);
    Store(world, data.m_leftKey, data.m_leftData, script::Arithmetic(op, left, right, Truncate));
}

// Posts a script message through the game's queues (the entry points). The message words that are not set are zero.
void PostScriptMessage(const script::BuiltMessage& built, const script::SendPlan& plan)
{
    messages::Message message{};
    message.sender = built.sender;
    message.receiver = built.receiver;
    message.type = built.type;
    message.flags = built.flags;
    message.data = built.data;
    auto* handler = messages::message_facade::Current();
    if (plan.delayed)
        messages::message_facade::PostDelayed(handler, &message, plan.delay);
    else
        messages::message_facade::Post(handler, &message);
}

// `Message MT_x <receiver> <data> <flags> [Time t] [MaxTime t]`: three operands become the message (script::BuildMessage), the delay is
// drawn from [Time, MaxTime] when there is a MaxTime.
void ExecuteMessage(World& world, const world_script::WorldScriptMessageData& data)
{
    const ScriptValue receiver = Query(world, data.m_receiver.m_kind, data.m_receiver.m_data);
    const ScriptValue payload = Query(world, data.m_data.m_kind, data.m_data.m_data);
    const ScriptValue flags = Query(world, data.m_flags.m_kind, data.m_flags.m_data);
    const script::BuiltMessage built = script::BuildMessage(data.m_messageId, receiver, payload, flags);
    PostScriptMessage(built, script::PlanMessageSend(data.m_time, data.m_maxTime, game::Rand));
}

// `ChangeState <state> [Time t]`: the state's index (by name, case-sensitive, -1 when the script has no such state) goes to the world script
// as MT_STATECHANGE. The world's silence byte (not the pause flag Post looks at) silences the command while it is set.
void ExecuteChangeState(World& world, const world_script::ScriptChangeStateData& data)
{
    if (world.m_loadingSave) return;
    const auto* script = static_cast<const world_script::WorldScript*>(world.m_worldScript);
    const script::BuiltMessage message{
            -2, -2, script::kMessageTypeStateChange, 0, script::FindStateIndex(script->m_stateNames, script->m_numStates, data.m_name)};
    PostScriptMessage(message, script::PlanChangeStateSend(data.m_time));
}
} // namespace

int32_t ChangeWorldScriptState(World& world, const char* state)
{
    const auto* script = static_cast<const world_script::WorldScript*>(world.m_worldScript);
    if (!script) return -2;
    if (world.m_loadingSave) return -3;
    const int32_t index = script::FindStateIndex(script->m_stateNames, script->m_numStates, state);
    PostScriptMessage({-2, -2, script::kMessageTypeStateChange, 0, index}, script::PlanChangeStateSend(0.0f));
    return index;
}

namespace {

// The sound the commands play: full volume, flags 1 (a one-shot), no fade-in.
constexpr float kSoundVolume = 1.0f, kSoundFade = 0.0f;
constexpr uint32_t kSoundFlags = 1;
// The dialog opens modal and without a provider.
constexpr uint32_t kMenuModal = 1;

// The hand-over commands call the implementations of the ferry and world modules.
// The world-setup commands' callees: the world's place handler (Create), the object-script host (LoadScript) and the grid's setters (Area, Boat*).
uint32_t* WorldToTile(uint32_t* tile, const float* point)
{
    return ui::MapView::WorldToTile(tile, point);
}

// `PlaySound <name>;`
void ExecutePlaySound(const world_script::ScriptSoundData& data)
{
    audio::CurrentSound()->PlayScaled(data.m_name, kSoundVolume, kSoundFlags, kSoundFade);
}

// `MoveCamera x y <seconds>;`: the GUI drops its selection (the camera leaves the selected object behind), then the map scrolls to the point.
// The selection is real state in the dry run too (the dry-run check compares it); the scroll itself is only logged there.
void ExecuteMoveCamera(const world_script::ScriptCameraData& data)
{
    GuiSelectionOf()->m_selection = -1;
    ui::GetMapView()->ScrollToWorldPoint(data.m_x, data.m_y, data.m_z, data.m_seconds);
}

// `ShowMenu <dialog> [text key];`
void ExecuteShowMenu(const world_script::ScriptMenuData& data)
{
    game::NoteScriptDialog(data.m_name); // the campaign scripts announce a win or loss with this dialog
    game::GetGuiManager()->ShowDialogWithText(data.m_name, data.m_textKey, nullptr, kMenuModal != 0);
}

// ---- hand-over commands (BoatEvent, CircuitPoint, Disaster, Weather) ---------------------------------------------------------
// Each translates its payload (src/game/world/world_script_delegate.h) and calls the owner of the data. No gate byte, no draws, no messages of
// their own: the dry run only logs the call it would make.

// `CircuitPoint <JetSki|PowerBoat> x y;`
void ExecuteCircuitPoint(World& world, const world_script::WorldScriptCircuitData& data)
{
    const script::CircuitCall call = script::CircuitPointCall(data);
    world.AddCircuitPoint(call.circuit, call.x, call.y);
}

// `BoatEvent ...;`: the record goes to the FerryManager, which copies it.
void ExecuteBoatEvent(World& world, const world_script::WorldScriptBoatEventData& data)
{
    const ferry::Boat record = script::BoatEventRecord(data);
    ferry::ferry_facade::AddBoatEvent(world.m_ferryManager, &record);
}

// `Disaster ...;`: the EventManager copies the target's name into the event it keeps.
void ExecuteDisaster(const world_script::WorldScriptDisasterData& data)
{
    disaster::event_facade::AddEvent(disaster::event_facade::Current(), script::DisasterEventRecord(data));
}

// `Weather ...;`
void ExecuteWeather(const world_script::WorldScriptWeatherData& data)
{
    const weather::WeatherEntry entry = script::WeatherEntryRecord(data);
    weather::runtime::InsertEntry(game::GetWeather(), entry);
}

// ---- world-setup commands (Create, LoadScript, Area, BoatPoint, BoatEntry, BoatExit) -----------------------------------------------
// The scenario's start builds the map with them; each ends in one call per object or point (src/game/world/world_script_delegate.h).

void Place(const messages::Message& message, world_script::ScriptPlacement& placement)
{
    messages::Message copy = message; // the callee stamps the time into it
    world::PostPlaceCommand(copy, &placement);
}

// `Create <Scenery|Building|Guest> <type> ...;`
void ExecuteCreate(World& world, const world_script::WorldScriptCreateData& data)
{
    if (world.m_loadingSave) return;
    const messages::Message message = script::CreateMessage(data.m_class);
    world_script::ScriptPlacement placement = script::CreatePlacement(data);
    if (data.m_class == script::kCreateBuilding) {
        Place(message, placement);
        return;
    }
    if (data.m_class != script::kCreateScenery && data.m_class != script::kCreateGuest) {
        game::FatalError("The world script asked to create an unrecognized kind of object"); // the game ends here
        return;
    }
    const map::Grid& grid = *game::GetGrid();
    for (int32_t i = 0; i < data.m_number; ++i) {
        if (script::IsRandomCreate(data)) {
            script::CreatePoint point;
            uint32_t tile[2];
            do {
                point = script::RandomCreatePoint(game::Rand);
                const float position[3] = {point.x, point.y, 0.0f};
                WorldToTile(tile, position);
            } while (!script::IsCreateTile(grid.Cell(tile[0], tile[1]).m_flags));
            if (data.m_class == script::kCreateScenery) { // scenery stands on the tile, a guest at the point
                placement.m_x = static_cast<int32_t>(tile[0]);
                placement.m_y = static_cast<int32_t>(tile[1]);
            } else {
                std::memcpy(&placement.m_x, &point.x, sizeof(point.x));
                std::memcpy(&placement.m_y, &point.y, sizeof(point.y));
            }
        }
        Place(message, placement);
    }
}

// `LoadScript <logic> <path>;`
void ExecuteLoadScript(const world_script::WorldScriptTwoTextsData& data)
{
    game::GetWorldScriptHost()->AddScript(data.m_first, data.m_second);
}

// `Area <Beach|Sea> ...;`: a new area appended to the grid's beach or sea list.
void ExecuteArea(const world_script::WorldScriptAreaData& data)
{
    const world_script::WorldScriptPoint* r = data.m_rectangle;
    const int32_t integers[] = {r[0].m_x, r[0].m_y, r[1].m_x, r[1].m_y, r[2].m_x, r[2].m_y, r[3].m_x, r[3].m_y, script::kAreaLastArgument};
    map::Area area = grid_areas::MakeArea(reinterpret_cast<const uint32_t*>(data.m_points), static_cast<uint32_t>(data.m_count), integers);
    if (data.m_kind == script::kAreaBeach)
        game::GetGrid()->AddBeachArea(std::move(area));
    else
        game::GetGrid()->AddSeaArea(std::move(area));
}

// `BoatPoint x y [Unload|Destroy];`
void ExecuteBoatPoint(const world_script::WorldScriptBoatPointData& data)
{
    game::GetGrid()->AddBoatPoint(data.m_x, data.m_y, data.m_z, data.m_flags);
}

// `BoatEntry x y n dx dy;` / `BoatExit ...`
void ExecuteBoatEnd(bool entry, const world_script::WorldScriptBoatEndpointData& data)
{
    if (entry)
        game::GetGrid()->SetBoatEntry(data.m_x, data.m_y, data.m_z, data.m_number, data.m_dx, data.m_dy);
    else
        game::GetGrid()->SetBoatExit(data.m_x, data.m_y, data.m_z, data.m_number, data.m_dx, data.m_dy);
}

// `Path x y n <direction>;` / `Grass ...`: a run of n tiles.
void ExecuteTiles(World& world, const world_script::WorldScriptTileData& data, uint32_t flags)
{
    if (world.m_loadingSave) return;
    map::Grid* grid = game::GetGrid();
    uint32_t tile[2] = {static_cast<uint32_t>(data.m_x), static_cast<uint32_t>(data.m_y)};
    for (int32_t i = 0; i < data.m_layer; ++i) {
        grid->Cell(tile[0], tile[1]).m_flags |= flags;
        game::GetPathTiles()->RebuildTile(tile[0], tile[1], 0);

        const grid_math::TileXY next = grid->GetNeighbour(tile[0], tile[1], data.m_direction);
        tile[0] = next.x;
        tile[1] = next.y;
    }
}

// Executes one command.
void Execute(World& world, script::ScriptCommand& command)
{
    switch (command.m_opcode) {
    case world_script::kScriptOpFunction: {
        const auto& data = *static_cast<const world_script::ScriptFunctionData*>(command.m_data);
        if (data.m_function != world_script::kScriptFunctionDebug) world::CallScriptFunction(world, data.m_function);
        break;
    }
    case world_script::kScriptOpMessage:
        ExecuteMessage(world, *static_cast<const world_script::WorldScriptMessageData*>(command.m_data));
        break;
    case world_script::kScriptOpChangeState:
        ExecuteChangeState(world, *static_cast<const world_script::ScriptChangeStateData*>(command.m_data));
        break;
    case world_script::kScriptOpIf: {
        const auto& data = *static_cast<const world_script::ScriptIfData*>(command.m_data);
        const bool holds = Condition(world, data);
        for (script::ScriptCommand* node = holds ? data.m_true : data.m_false; node; node = node->m_next) Execute(world, *node);
        break;
    }
    case world_script::kScriptOpAdd:
    case world_script::kScriptOpMinus:
    case world_script::kScriptOpDivide:
    case world_script::kScriptOpMultiply: {
        const auto& data = *static_cast<const world_script::ScriptAssignData*>(command.m_data);
        // Opcodes 6, 7, 8, 9 are Add, Minus, Divide, Multiply.
        static constexpr script::Arith kOps[] = {script::Arith::Add, script::Arith::Minus, script::Arith::Divide, script::Arith::Multiply};
        ExecuteArithmetic(world, kOps[command.m_opcode - world_script::kScriptOpAdd], data);
        break;
    }
    case world_script::kScriptOpSet: {
        const auto& data = *static_cast<const world_script::ScriptAssignData*>(command.m_data);
        Store(world, data.m_leftKey, data.m_leftData, Query(world, data.m_rightKey, data.m_rightData));
        break;
    }
    case world_script::kScriptOpPlaySound: ExecutePlaySound(*static_cast<const world_script::ScriptSoundData*>(command.m_data)); break;
    case world_script::kScriptOpMoveCamera: ExecuteMoveCamera(*static_cast<const world_script::ScriptCameraData*>(command.m_data)); break;
    case world_script::kScriptOpPath:
        ExecuteTiles(world, *static_cast<const world_script::WorldScriptTileData*>(command.m_data), script::kPathFlags);
        break;
    case world_script::kScriptOpGrass:
        ExecuteTiles(world, *static_cast<const world_script::WorldScriptTileData*>(command.m_data), script::kGrassFlags);
        break;
    case world_script::kScriptOpBoatEvent:
        ExecuteBoatEvent(world, *static_cast<const world_script::WorldScriptBoatEventData*>(command.m_data));
        break;
    case world_script::kScriptOpCircuitPoint:
        ExecuteCircuitPoint(world, *static_cast<const world_script::WorldScriptCircuitData*>(command.m_data));
        break;
    case world_script::kScriptOpDisaster:
        ExecuteDisaster(*static_cast<const world_script::WorldScriptDisasterData*>(command.m_data));
        break;
    case world_script::kScriptOpWeather: ExecuteWeather(*static_cast<const world_script::WorldScriptWeatherData*>(command.m_data)); break;
    case world_script::kScriptOpShowMenu: ExecuteShowMenu(*static_cast<const world_script::ScriptMenuData*>(command.m_data)); break;
    case world_script::kScriptOpCreate:
        ExecuteCreate(world, *static_cast<const world_script::WorldScriptCreateData*>(command.m_data));
        break;
    case world_script::kScriptOpLoadScript:
        ExecuteLoadScript(*static_cast<const world_script::WorldScriptTwoTextsData*>(command.m_data));
        break;
    case world_script::kScriptOpArea: ExecuteArea(*static_cast<const world_script::WorldScriptAreaData*>(command.m_data)); break;
    case world_script::kScriptOpBoatPoint:
        ExecuteBoatPoint(*static_cast<const world_script::WorldScriptBoatPointData*>(command.m_data));
        break;
    case world_script::kScriptOpBoatEntry:
    case world_script::kScriptOpBoatExit:
        ExecuteBoatEnd(command.m_opcode == world_script::kScriptOpBoatEntry,
                *static_cast<const world_script::WorldScriptBoatEndpointData*>(command.m_data));
        break;
    case world_script::kScriptOpUndefined: game::FatalError("The world script ran an unrecognized command"); break;
    default: // 3, 5 and anything above 26 (as unsigned) do nothing
        break;
    }
    g_execCalls.Count();
}

} // namespace

void WorldScriptExecute(script::ScriptCommand& command)
{
    Execute(*game::GetWorld(), command);
}

namespace {

void ExecuteCommandEntry(script::ScriptCommand* command)
{
    WorldScriptExecute(*command);
}

// ---- call-drive ----------------------------------------------------------------------------------------------
// A scenario asks for a handful of variables, so `call-drive` queries every key with every kind of argument against
// the living world, several rounds, each with the guests' and buildings' fields randomized (and restored afterwards;
// nothing runs in between, so the game never sees the changes).

struct GuestState {
    float happiness, drunk, opinion[7];
    int state;
};
struct BuildingState {
    int repairState;
    uint8_t builtByUser;
};

void Drive(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_objectManager) {
        OBL_LOG("autotest: FAILED call-drive world.get-script-value: no world");
        return;
    }
    object::Manager& objects = *world->m_objectManager;
    const tables::WorldParams* params = game::GetWorldParams();
    std::vector<GuestState> guests(objects.m_guests.size());
    std::vector<BuildingState> buildings(objects.m_buildings.size());
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) {
        const Guest* g = GuestAt(objects, i);
        std::memcpy(guests[i].opinion, g->m_opinion, sizeof(guests[i].opinion));
        guests[i].happiness = g->m_happiness;
        guests[i].drunk = g->m_drunk;
        guests[i].state = g->m_state;
    }
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) {
        buildings[i].repairState = BuildingAt(objects, i)->m_RepairState;
        buildings[i].builtByUser = BuildingAt(objects, i)->m_builtByUser;
    }
    const Caches savedCaches = ReadCaches(*world);

    uint32_t seed = 0x1234567;
    auto next = [&seed]() { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    auto unit = [&next]() { return static_cast<float>(next() & 0xffff) / 65535.0f; };

    // Keys without an argument, then the ones that take an index, a number, a string or a building name.
    static const int plainKeys[] = {0xb9, 0xba, 0xbb, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc5, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce,
            0xcf, 0xd0, 0xd1, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdc, 0xdd, 0xde, 0xe2, 0xea, 0xec, 0xed, 0xee};
    static const int unlistedKeys[] = {
            0, 0x35, 0x37, 0xb8, 0xc3, 0xc4, 0xd2, 0xd3, 0xd5, 0xdb, 0xdf, 0xe3, 0xe4, 0xe9, 0xeb, 0xef, 0x1000, -1};
    std::vector<std::string> names = {"NO_SUCH_BUILDING", ""};
    for (int i = 0; i < params->m_numBuildings; ++i) names.push_back(params->m_buildings[i].m_key);
    for (const char* bar : {"BEACHBAR", "POOLBAR", "BAR1", "BAR2", "COCKTAIL"}) names.push_back(bar);

    for (int round = 0; round < rounds; ++round) {
        for (uint32_t i = 0; round > 0 && i < objects.m_guests.size(); ++i) {
            Guest* g = GuestAt(objects, i);
            g->m_happiness = round % 3 == 1 ? unit() * 1.2f - 0.1f : unit();
            g->m_drunk = unit();
            for (float& o : g->m_opinion) o = unit();
            uint32_t pick = next() % 4;
            g->m_state = pick == 0 ? kGuestStateParty1 : pick == 1 ? kGuestStateParty2 : guests[i].state;
        }
        for (uint32_t i = 0; round > 0 && i < objects.m_buildings.size(); ++i) {
            BuildingAt(objects, i)->m_builtByUser = round % 2 == 0 || (next() & 1);
            BuildingAt(objects, i)->m_RepairState = (round % 4 == 0 && i == 0) ? kRepairStateBroken : buildings[i].repairState;
        }
        int32_t out[2];
        for (int key : plainKeys) ReadScriptValueEntry(world, out, key, reinterpret_cast<int32_t*>(0x12345678));
        for (int key : unlistedKeys) ReadScriptValueEntry(world, out, key, reinterpret_cast<int32_t*>(0xcafe0000 + round));
        for (int n = 0; n < 34; ++n) {
            int32_t index = n;
            ReadScriptValueEntry(world, out, 0xbc, &index);
            if (n < 0x20) ReadScriptValueEntry(world, out, 0xc2, &index);
            if (n < 4) ReadScriptValueEntry(world, out, 0xc6, &index);
        }
        const float numbers[] = {0.0f, 1.5f, -3.25f, 100000.0f};
        for (float number : numbers) {
            int32_t bits = FloatBits(number);
            ReadScriptValueEntry(world, out, 0xe8, &bits);
        }
        ReadScriptValueEntry(world, out, 0x36, reinterpret_cast<int32_t*>(const_cast<char*>("a quoted text")));
        for (const std::string& name : names)
            ReadScriptValueEntry(world, out, 0xd4, reinterpret_cast<int32_t*>(const_cast<char*>(name.c_str())));
    }

    // ResortRating truncates mean happiness x 100 and saturates from 96: probe both sides of every step of that rule.
    for (float happiness : {-0.5f, 0.0f, 0.5f, 0.9499f, 0.9599f, 0.96f, 0.9601f, 0.9699f, 0.97f, 1.0f, 1.5f, 100.0f}) {
        for (uint32_t i = 0; i < objects.m_guests.size(); ++i) GuestAt(objects, i)->m_happiness = happiness;
        int32_t out[2];
        ReadScriptValueEntry(world, out, 0xec, nullptr);
    }

    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) {
        Guest* g = GuestAt(objects, i);
        std::memcpy(g->m_opinion, guests[i].opinion, sizeof(guests[i].opinion));
        g->m_happiness = guests[i].happiness;
        g->m_drunk = guests[i].drunk;
        g->m_state = guests[i].state;
    }
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) {
        BuildingAt(objects, i)->m_RepairState = buildings[i].repairState;
        BuildingAt(objects, i)->m_builtByUser = buildings[i].builtByUser;
    }
    WriteCaches(*world, savedCaches);
}

// Every writable key with every kind of value and index. GameTime needs STATE_WORLD_STARTUP, so the state pointer is
// swapped for the duration of those calls; everything the calls wrote is restored at the end.
void DriveSet(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_worldScript || !game::GetWeather() || !disaster::event_facade::Current()) {
        OBL_LOG("autotest: FAILED call-drive world.set-script-value: no world");
        return;
    }
    const tables::WorldParams* params = game::GetWorldParams();
    const SetState saved = CaptureSetState(*world);
    auto* script = static_cast<world_script::WorldScript*>(world->m_worldScript);
    const uint32_t savedState = script->m_currentState;
    const uint32_t startup = static_cast<uint32_t>(WorldScriptFindState(*script, "STATE_WORLD_STARTUP"));

    struct Value {
        int type;
        int32_t bits;
    };
    static const char text[] = "text";
    const std::vector<Value> values = {
            {0, 0x1ff},
            {0, 0},
            {2, -5},
            {2, 7},
            {2, 0},
            {3, FloatBits(2.5f)},
            {3, FloatBits(0.0f)},
            {3, FloatBits(-0.0f)},
            {3, 0x7fc00000},
            {3, 1},
            {3, FloatBits(-3.7f)},
            {3, FloatBits(1e10f)},
            {3, FloatBits(9.0f)},
            {3, FloatBits(20.5f)},
            {1, script::text::Id(text)},
            {9, 5},
    };
    for (int round = 0; round < rounds; ++round) {
        for (const Value& v : values) {
            for (int32_t n = 0; n < 34; ++n) {
                WriteScriptValueEntry(world, 0xbc, &n, v.type, v.bits);
                if (n < 0x20) WriteScriptValueEntry(world, 0xc2, &n, v.type, v.bits);
                if (n < 4) WriteScriptValueEntry(world, 0xc6, &n, v.type, v.bits);
            }
            for (int key : {0xc5, 0xee, 0xdb, 0xdf, 0xea, 0xb9, 0xc3, 0xe3, 0x36, 0})
                WriteScriptValueEntry(world, key, nullptr, v.type, v.bits);
            for (int i = 0; i < params->m_numBeers; ++i)
                WriteScriptValueEntry(world, 0xd2, params->m_beers[i].m_key.c_str(), v.type, v.bits);
            for (int i = 0; i < params->m_numEvents; ++i)
                WriteScriptValueEntry(world, 0xd5, params->m_events[i].m_key.c_str(), v.type, v.bits);
        }
        // GameTime during the scenario's startup state.
        script->m_currentState = startup;
        for (float hour : {0.0f, 6.5f, 9.0f, 18.99f, 19.0f, 23.5f, 100.0f, -2.0f})
            WriteScriptValueEntry(world, 0xea, nullptr, 3, FloatBits(hour));
        // A sweep, so that the float rounding of the products is exercised on many different values.
        for (int k = 0; k < 480; ++k) WriteScriptValueEntry(world, 0xea, nullptr, 3, FloatBits(static_cast<float>(k) * 0.0517f - 1.3f));
        script->m_currentState = savedState;
        // DefaultBeer<BUILDING> takes the beer's name as the value.
        for (int b = 0; b < params->m_numBuildings; ++b)
            for (int i = 0; i < params->m_numBeers; ++i)
                WriteScriptValueEntry(
                        world, 0xd3, params->m_buildings[b].m_key.c_str(), 1, script::text::Id(params->m_beers[i].m_key.c_str()));
    }
    script->m_currentState = savedState;
    RestoreSetState(*world, saved);
}

// Every operand kind against every other under every operator (and a few invalid ones): keys with and without data, number
// literals including NaN/-0/infinity, and script variables holding values that round differently as a float (the
// right-hand side is kept unrounded, as a wider register would). The variables and flags are set for the run and restored afterwards.
void DriveEval(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_objectManager || !world->m_worldScript) {
        OBL_LOG("autotest: FAILED call-drive world.script-eval-condition-fixture: no world");
        return;
    }
    auto* script = static_cast<world_script::WorldScript*>(world->m_worldScript);
    const tables::WorldParams* params = game::GetWorldParams();

    const int32_t savedFlags = world->m_scriptFlags;
    int32_t savedVars[4];
    std::memcpy(savedVars, world->m_scriptVars, sizeof(savedVars));
    const int32_t vars[4] = {0x01000001, -1, 0x7fffffff, 0};
    std::memcpy(world->m_scriptVars, vars, sizeof(vars));
    world->m_scriptFlags = 0x5;

    struct Operand {
        int32_t key;
        int32_t* data;
    };
    static int32_t indices[4] = {0, 1, 2, 3};
    static int32_t numbers[10];
    const float literals[] = {
            0.0f, -0.0f, 1.0f, 1.5f, -3.25f, 16777216.0f, 100000.0f, 4294967296.0f, BitsToFloat(0x7fc00000), BitsToFloat(0x7f800000)};
    for (size_t i = 0; i < std::size(literals); ++i) numbers[i] = FloatBits(literals[i]);
    static std::vector<std::string> names;
    names = {"NO_SUCH_BUILDING"};
    for (int i = 0; i < params->m_numBuildings && i < 3; ++i) names.push_back(params->m_buildings[i].m_key);

    std::vector<Operand> operands;
    for (int key : {0xb9, 0xba, 0xc5, 0xc7, 0xc8, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xd0, 0xd1, 0xd8, 0xdc, 0xde, 0xe2, 0xea, 0xec, 0xed, 0xee,
                 0x1000})
        operands.push_back({key, reinterpret_cast<int32_t*>(0x12345678)});
    for (int key : {0xbc, 0xc2, 0xc6})
        for (int32_t& index : indices) operands.push_back({key, &index});
    for (int32_t& number : numbers) operands.push_back({0xe8, &number});
    for (const std::string& name : names) operands.push_back({0xd4, reinterpret_cast<int32_t*>(const_cast<char*>(name.c_str()))});
    // `RandDWORD<n>`/`Random<n>`: the parser keeps a pointer to the int n, the interpreter has no draw for them (no shipped scenario
    // uses one): GetScriptValue passes the pair through and the comparison reads it as a value of an unknown kind.
    static int32_t randomIndices[2] = {7, 0};
    operands.push_back({0xe3, &randomIndices[0]});
    operands.push_back({0xe4, &randomIndices[1]});

    for (int round = 0; round < rounds; ++round) {
        for (const Operand& left : operands) {
            for (const Operand& right : operands)
                for (int32_t op = -1; op <= 8; ++op) EvaluateConditionEntry(script, left.key, left.data, right.key, right.data, op);
            // A bare `If ( value )`: the operator and the right data are ignored.
            for (int32_t op : {0, 3, 7}) EvaluateConditionEntry(script, left.key, left.data, kNoRightOperand, nullptr, op);
        }
        // Text comparisons: the left side is a quoted text as well, so its bits are a valid string.
        static const char* const texts[] = {"Hello", "hello", "HELLO ", "", "Hellp", "hello world"};
        for (const char* left : texts)
            for (const char* right : texts)
                for (int32_t op = -1; op <= 8; ++op)
                    EvaluateConditionEntry(script, kTextKey, reinterpret_cast<int32_t*>(const_cast<char*>(left)), kTextKey,
                            reinterpret_cast<int32_t*>(const_cast<char*>(right)), op);
        // A text on the left against a number (never a string comparison on the right): evaluated as plain values.
        for (const char* left : texts)
            for (int32_t& number : numbers)
                for (int32_t op = kOpAnd; op <= kOpNotEqual; ++op)
                    EvaluateConditionEntry(script, kTextKey, reinterpret_cast<int32_t*>(const_cast<char*>(left)), 0xe8, &number, op);
    }

    world->m_scriptFlags = savedFlags;
    std::memcpy(world->m_scriptVars, savedVars, sizeof(savedVars));
}

// Command trees for `call-drive`: nodes the interpreter walks like the parser's.
class Tree {
public:
    script::ScriptCommand* Assign(int opcode, int leftKey, void* leftData, int rightKey, void* rightData)
    {
        m_assigns.push_back({leftKey, leftData, rightKey, rightData});
        return Node(opcode, &m_assigns.back());
    }
    script::ScriptCommand* Message(int type, int receiverKey, void* receiverData, int dataKey, void* dataData, int flagsKey,
            void* flagsData, float time, float maxTime)
    {
        world_script::WorldScriptMessageData data{};
        data.m_messageId = type;
        data.m_receiver = {receiverKey, receiverData};
        data.m_data = {dataKey, dataData};
        data.m_flags = {flagsKey, flagsData};
        data.m_time = time;
        data.m_maxTime = maxTime;
        m_messages.push_back(data);
        return Node(world_script::kScriptOpMessage, &m_messages.back());
    }
    script::ScriptCommand* ChangeState(const char* name, float time)
    {
        m_changes.push_back({name, time});
        return Node(world_script::kScriptOpChangeState, &m_changes.back());
    }
    script::ScriptCommand* Function(int function)
    {
        m_functions.push_back({function});
        return Node(world_script::kScriptOpFunction, &m_functions.back());
    }
    script::ScriptCommand* If(script::ScriptCommand* whenTrue, script::ScriptCommand* whenFalse, int leftKey, void* leftData, int rightKey,
            void* rightData, int op)
    {
        m_ifs.push_back({whenTrue, whenFalse, leftKey, static_cast<int32_t*>(leftData), rightKey, static_cast<int32_t*>(rightData), op, 0});
        return Node(world_script::kScriptOpIf, &m_ifs.back());
    }
    script::ScriptCommand* PlaySound(const char* name)
    {
        m_sounds.push_back({name});
        return Node(world_script::kScriptOpPlaySound, &m_sounds.back());
    }
    script::ScriptCommand* MoveCamera(float x, float y, float z, float seconds)
    {
        m_cameras.push_back({x, y, z, seconds});
        return Node(world_script::kScriptOpMoveCamera, &m_cameras.back());
    }
    script::ScriptCommand* ShowMenu(const char* name, const char* textKey)
    {
        m_menus.push_back({name, textKey});
        return Node(world_script::kScriptOpShowMenu, &m_menus.back());
    }
    script::ScriptCommand* Tiles(int opcode, int32_t x, int32_t y, int32_t count, int32_t direction)
    {
        m_tiles.push_back({x, y, direction, count});
        return Node(opcode, &m_tiles.back());
    }
    script::ScriptCommand* CircuitPoint(int32_t x, int32_t y, int32_t kind)
    {
        m_circuits.push_back({x, y, kind});
        return Node(world_script::kScriptOpCircuitPoint, &m_circuits.back());
    }
    script::ScriptCommand* BoatEvent(const world_script::WorldScriptBoatEventData& data)
    {
        m_boats.push_back(data);
        return Node(world_script::kScriptOpBoatEvent, &m_boats.back());
    }
    script::ScriptCommand* Disaster(const world_script::WorldScriptDisasterData& data)
    {
        m_disasters.push_back(data);
        return Node(world_script::kScriptOpDisaster, &m_disasters.back());
    }
    script::ScriptCommand* Weather(const world_script::WorldScriptWeatherData& data)
    {
        m_weathers.push_back(data);
        return Node(world_script::kScriptOpWeather, &m_weathers.back());
    }
    script::ScriptCommand* Create(const world_script::WorldScriptCreateData& data)
    {
        m_creates.push_back(data);
        return Node(world_script::kScriptOpCreate, &m_creates.back());
    }
    script::ScriptCommand* LoadScript(const char* name, const char* path)
    {
        m_texts.push_back({const_cast<char*>(name), const_cast<char*>(path)});
        return Node(world_script::kScriptOpLoadScript, &m_texts.back());
    }
    // The points are copied into the tree's own storage.
    script::ScriptCommand* Area(
            int32_t kind, std::vector<world_script::WorldScriptPoint> points, const world_script::WorldScriptPoint (&rectangle)[4])
    {
        m_points.push_back(std::move(points));
        world_script::WorldScriptAreaData data{kind, static_cast<int32_t>(m_points.back().size()), m_points.back().data(), {}};
        std::memcpy(data.m_rectangle, rectangle, sizeof(rectangle));
        m_areas.push_back(data);
        return Node(world_script::kScriptOpArea, &m_areas.back());
    }
    script::ScriptCommand* BoatPoint(const world_script::WorldScriptBoatPointData& data)
    {
        m_boatPoints.push_back(data);
        return Node(world_script::kScriptOpBoatPoint, &m_boatPoints.back());
    }
    script::ScriptCommand* BoatEnd(int opcode, const world_script::WorldScriptBoatEndpointData& data)
    {
        m_boatEnds.push_back(data);
        return Node(opcode, &m_boatEnds.back());
    }
    // Links the nodes into one list and returns its head.
    static script::ScriptCommand* Chain(std::initializer_list<script::ScriptCommand*> nodes)
    {
        script::ScriptCommand* head = nullptr;
        for (auto it = nodes.end(); it != nodes.begin();) {
            (*--it)->m_next = head;
            head = *it;
        }
        return head;
    }

private:
    script::ScriptCommand* Node(int opcode, void* data)
    {
        m_commands.push_back({nullptr, opcode, data});
        return &m_commands.back();
    }
    std::deque<script::ScriptCommand> m_commands;
    std::deque<world_script::ScriptAssignData> m_assigns;
    std::deque<world_script::ScriptFunctionData> m_functions;
    std::deque<world_script::WorldScriptMessageData> m_messages;
    std::deque<world_script::ScriptChangeStateData> m_changes;
    std::deque<world_script::ScriptIfData> m_ifs;
    std::deque<world_script::ScriptSoundData> m_sounds;
    std::deque<world_script::ScriptCameraData> m_cameras;
    std::deque<world_script::ScriptMenuData> m_menus;
    std::deque<world_script::WorldScriptTileData> m_tiles;
    std::deque<world_script::WorldScriptCircuitData> m_circuits;
    std::deque<world_script::WorldScriptBoatEventData> m_boats;
    std::deque<world_script::WorldScriptDisasterData> m_disasters;
    std::deque<world_script::WorldScriptWeatherData> m_weathers;
    std::deque<world_script::WorldScriptCreateData> m_creates;
    std::deque<world_script::WorldScriptTwoTextsData> m_texts;
    std::deque<std::vector<world_script::WorldScriptPoint>> m_points;
    std::deque<world_script::WorldScriptAreaData> m_areas;
    std::deque<world_script::WorldScriptBoatPointData> m_boatPoints;
    std::deque<world_script::WorldScriptBoatEndpointData> m_boatEnds;
};

// `Message` and `ChangeState` on synthetic trees. The commands post for real: only a receiver that does not exist (its entries are cleared
// afterwards) and `ChangeState` silenced by the gate byte.
void DriveMessages(World& world, int rounds)
{
    auto* script = static_cast<world_script::WorldScript*>(world.m_worldScript);
    const uint8_t savedGate = world.m_loadingSave;
    const uint32_t savedStates = script->m_numStates;
    const int32_t savedVar2 = world.m_scriptVars[2], savedVar3 = world.m_scriptVars[3];
    constexpr int32_t kNoReceiver = 0x7ffffff0;
    constexpr int kTypeNewDay = 38;

    static int32_t indices[4] = {0, 1, 2, 3};
    static int32_t numbers[6];
    const float literals[] = {0.0f, 1.5f, -3.25f, 1e10f, BitsToFloat(0x7fc00000), 300.0f};
    for (size_t i = 0; i < std::size(literals); ++i) numbers[i] = FloatBits(literals[i]);
    static const char text[] = "a text";
    struct Operand {
        int32_t key;
        void* data;
    };
    // Every kind of value: ints (Var), a byte (Flag), floats (number literals, cash), a string, a value computed by the world.
    const std::vector<Operand> operands = {{0xc2, &indices[0]}, {0xc2, &indices[1]}, {0xc2, &indices[2]}, {0xc2, &indices[3]},
            {0xbc, &indices[0]}, {0xe8, &numbers[0]}, {0xe8, &numbers[1]}, {0xe8, &numbers[2]}, {0xe8, &numbers[3]}, {0xe8, &numbers[4]},
            {0xe8, &numbers[5]}, {0x36, const_cast<char*>(text)}, {0xee, reinterpret_cast<void*>(0x12345678)},
            {0xb9, reinterpret_cast<void*>(0x12345678)}};
    struct Delay {
        float time, maxTime;
    };
    const float nan = BitsToFloat(0x7fc00000);
    const Delay delays[] = {{0.0f, 0.0f}, {1.5f, 0.0f}, {-2.0f, 0.0f}, {nan, 0.0f}, {-0.0f, 0.0f}, {0.0f, nan}, {0.0f, 0.25f}, {1.0f, 3.0f},
            {3.0f, 1.0f}, {2.5f, 2.5f}, {0.0f, 1e10f}, {nan, 2.0f}, {0.5f, 0.0f}};
    auto resetVariables = [&world]() {
        world.m_scriptVars[0] = 7;
        world.m_scriptVars[1] = -1;
        world.m_scriptVars[2] = 100;
        world.m_scriptVars[3] = kNoReceiver;
        world.m_scriptFlags = 0x5;
    };

    Tree tree;
    for (int round = 0; round < rounds; ++round) {
        resetVariables();
        for (const Delay& delay : delays)
            ExecuteCommandEntry(
                    tree.Message(kTypeNewDay, 0xc2, &indices[3], 0xe8, &numbers[1], 0xc2, &indices[2], delay.time, delay.maxTime));

        // ChangeState: every state of the script, a name it does not have, the case of an existing one, an empty name; every kind of delay.
        std::vector<std::string> names = {"NO_SUCH_STATE", ""};
        for (uint32_t i = 0; i < savedStates; ++i) {
            names.push_back(script->m_stateNames[i]);
            std::string lower = script->m_stateNames[i];
            for (char& c : lower) c = ascii::Lower(c);
            names.push_back(lower);
        }
        const float times[] = {0.0f, 1.5f, nan, -0.0f, 3.0f, -1.0f};
        for (uint8_t gate : {uint8_t{0}, uint8_t{1}}) {
            if (gate == 0) continue; // the gate shuts the command: the only form that is safe to run for real
            world.m_loadingSave = gate;
            for (const std::string& name : names)
                for (float time : times) ExecuteCommandEntry(tree.ChangeState(name.c_str(), time));
        }

        // Both commands inside an `If` (the walk is compared command by command).
        world.m_loadingSave = 1;
        script::ScriptCommand* body = Tree::Chain({tree.Message(kTypeNewDay, 0xc2, &indices[3], 0xe8, &numbers[1], 0xc2, &indices[2], 0.5f,
                                                           0.0f),
                tree.ChangeState(names[0].c_str(), 0.0f), tree.Assign(world_script::kScriptOpAdd, 0xc2, &indices[0], 0xe8, &numbers[1])});
        script::ScriptCommand* other =
                Tree::Chain({tree.Message(kTypeNewDay, 0xc2, &indices[3], 0xe8, &numbers[2], 0xc2, &indices[2], 0.0f, 2.0f)});
        ExecuteCommandEntry(tree.If(body, other, 0xc2, &indices[0], 0xc2, &indices[1], 2));
        ExecuteCommandEntry(tree.If(body, other, 0xc2, &indices[1], 0xc2, &indices[0], 2));
    }

    messages::message_facade::ClearDelayedForReceiver(messages::message_facade::Current(), kNoReceiver); // what the commands really posted
    world.m_loadingSave = savedGate;
    script->m_numStates = savedStates;
    world.m_scriptVars[2] = savedVar2;
    world.m_scriptVars[3] = savedVar3;
}

void DriveExec(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_objectManager || !world->m_worldScript || !world->m_player) {
        OBL_LOG("autotest: FAILED call-drive world.script-execute-command-fixture: no world");
        return;
    }
    auto* script = static_cast<world_script::WorldScript*>(world->m_worldScript);
    const SetState saved = CaptureSetState(*world);
    const Caches savedCaches = ReadCaches(*world);
    const uint32_t savedState = script->m_currentState;

    static int32_t indices[4] = {0, 1, 2, 3};
    static int32_t numbers[12];
    const float literals[] = {0.0f, -0.0f, 1.0f, -1.5f, 3.25f, 0.1f, 1e10f, 16777217.0f, 4294967296.0f, -7.0f, BitsToFloat(0x7fc00000),
            BitsToFloat(0x7f800000)};
    for (size_t i = 0; i < std::size(literals); ++i) numbers[i] = FloatBits(literals[i]);
    static const char text[] = "text";
    struct Operand {
        int32_t key;
        void* data;
    };
    std::vector<Operand> targets, values;
    for (int32_t& index : indices) {
        targets.push_back({0xc2, &index}); // Var<n>
        targets.push_back({0xbc, &index}); // Flag<n>
        targets.push_back({0xc6, &index}); // ScenarioObj<n>
        values.push_back({0xc2, &index});
    }
    values.push_back({0xbc, &indices[0]});
    values.push_back({0xbc, &indices[2]});
    for (int key : {0xee, 0xc5, 0xdb, 0x1000}) // PlayerCash, DemoTime, a key with no value of its own, an unknown key
        targets.push_back({key, reinterpret_cast<void*>(0x12345678)});
    for (int key : {0xee, 0xc5, 0xea, 0xb9, 0xc7, 0xcc, 0xed, 0x1000}) // ... GameTime, GuestCount, SandBox, a byte, GameDay
        values.push_back({key, reinterpret_cast<void*>(0x12345678)});
    for (int32_t& number : numbers) values.push_back({0xe8, &number});
    values.push_back({0x36, const_cast<char*>(text)});
    // `RandDWORD<n>`/`Random<n>` have no draw in the scenario interpreter (see DriveEval): operands of unknown kind. They only join the
    // assignments below; `values` keeps the positions the conditions pick from.
    static int32_t randomIndices[2] = {7, 0};
    const Operand randoms[] = {{0xe3, &randomIndices[0]}, {0xe4, &randomIndices[1]}};
    std::vector<Operand> assignTargets = targets, assignValues = values;
    assignTargets.insert(assignTargets.end(), std::begin(randoms), std::end(randoms));
    assignValues.insert(assignValues.end(), std::begin(randoms), std::end(randoms));

    auto read = [world](const Operand& operand) { return GetScriptValue(*world, operand.key, static_cast<const int32_t*>(operand.data)); };
    auto divisor = [](ScriptValue value) { return value.type == kFloat ? Truncate(value.bits) : value.bits; };
    auto resetVariables = [world]() {
        const int32_t vars[4] = {7, -1, 0x01000001, 100};
        std::memcpy(world->m_scriptVars, vars, sizeof(vars));
        world->m_scriptFlags = 0x5;
        world->m_player->m_cash = 1234.5f;
        world->m_demoTime = 2.5f;
    };

    Tree tree;
    for (int round = 0; round < rounds; ++round) {
        resetVariables();
        for (const Operand& target : assignTargets) {
            for (const Operand& value : assignValues) {
                for (int opcode = world_script::kScriptOpAdd; opcode <= world_script::kScriptOpSet; ++opcode) {
                    if (opcode == world_script::kScriptOpDivide && read(target).type == kInt && divisor(read(value)) == 0) continue;
                    ExecuteCommandEntry(tree.Assign(opcode, target.key, target.data, value.key, value.data));
                }
            }
            resetVariables(); // the operations push the variables to extremes; every target starts from ordinary values
        }

        // GameTime only changes during the scenario's startup state (the hour is the number operand).
        script->m_currentState = static_cast<uint32_t>(WorldScriptFindState(*script, "STATE_WORLD_STARTUP"));
        for (int32_t& number : numbers)
            for (int opcode :
                    {world_script::kScriptOpAdd, world_script::kScriptOpMinus, world_script::kScriptOpMultiply, world_script::kScriptOpSet})
                ExecuteCommandEntry(tree.Assign(opcode, 0xea, nullptr, 0xe8, &number));
        script->m_currentState = savedState;

        // `If`: operands over all operators, each with a body that writes and a nested branch, and with one branch empty.
        resetVariables();
        // (A quoted text only ever meets a text: the left value's bits would be read as a string pointer otherwise.)
        const Operand conditionOperands[] = {values[0], values[1], values[4], values[8], values[10], values[12], values[14],
                values[values.size() - 8], values[values.size() - 2]};
        for (const Operand& left : conditionOperands) {
            for (const Operand& right : conditionOperands) {
                for (int op = -1; op <= 8; ++op) {
                    script::ScriptCommand* inner =
                            tree.If(Tree::Chain({tree.Assign(world_script::kScriptOpMultiply, 0xc2, &indices[3], 0xe8, &numbers[4])}),
                                    Tree::Chain({tree.Function(world_script::kScriptFunctionDebug)}), 0xc2, &indices[1], kNoRightOperand,
                                    nullptr, 0);
                    script::ScriptCommand* whenTrue =
                            Tree::Chain({tree.Assign(world_script::kScriptOpAdd, 0xc2, &indices[0], 0xe8, &numbers[2]),
                                    tree.Assign(world_script::kScriptOpSet, 0xee, nullptr, 0xc2, &indices[0])});
                    script::ScriptCommand* whenFalse =
                            Tree::Chain({tree.Assign(world_script::kScriptOpMinus, 0xc2, &indices[2], 0xe8, &numbers[2]), inner});
                    ExecuteCommandEntry(tree.If(whenTrue, whenFalse, left.key, left.data, right.key, right.data, op));
                    ExecuteCommandEntry(tree.If(whenTrue, nullptr, left.key, left.data, right.key, right.data, op));
                    ExecuteCommandEntry(tree.If(nullptr, whenFalse, left.key, left.data, right.key, right.data, op));
                }
            }
            for (int op : {0, 3, 7}) // a bare `If ( value )`
                ExecuteCommandEntry(tree.If(Tree::Chain({tree.Assign(world_script::kScriptOpAdd, 0xc2, &indices[0], 0xe8, &numbers[2])}),
                        nullptr, left.key, left.data, kNoRightOperand, nullptr, op));
            resetVariables();
        }
        for (const Operand& left : randoms)
            for (const Operand& right : {randoms[0], randoms[1], values[0], values[14]})
                for (int op = -1; op <= 8; ++op)
                    ExecuteCommandEntry(
                            tree.If(Tree::Chain({tree.Assign(world_script::kScriptOpAdd, 0xc2, &indices[0], 0xe8, &numbers[2])}),
                                    Tree::Chain({tree.Assign(world_script::kScriptOpMinus, 0xc2, &indices[0], 0xe8, &numbers[2])}),
                                    left.key, left.data, right.key, right.data, op));
        resetVariables();
        static const char* const texts[] = {"Hello", "hello", "HELLO ", ""};
        for (const char* left : texts)
            for (const char* right : texts)
                for (int op = kOpEqual; op <= kOpNotEqual; ++op)
                    ExecuteCommandEntry(
                            tree.If(Tree::Chain({tree.Assign(world_script::kScriptOpAdd, 0xc2, &indices[0], 0xe8, &numbers[2])}), nullptr,
                                    kTextKey, const_cast<char*>(left), kTextKey, const_cast<char*>(right), op));
        ExecuteCommandEntry(tree.Function(world_script::kScriptFunctionDebug));
    }

    DriveMessages(*world, rounds);
    script->m_currentState = savedState;
    RestoreSetState(*world, saved);
    WriteCaches(*world, savedCaches);
}

} // namespace

bool RegisterWorldScriptDrivers()
{
    g_driveGet.SetDriver(&Drive);
    g_driveSet.SetDriver(&DriveSet);
    g_driveEval.SetDriver(&DriveEval);
    g_driveExec.SetDriver(&DriveExec);
    return true;
}

} // namespace obl::world
