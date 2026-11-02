#include "game/objects/object_base.h"
#include "core/ascii_case.h"
#include "game/objects/guest.h"
#include "game/ui/sound_api.h"
#include "game/weather/weather_state.h"
#include "game/weather/weather.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/weather/weather_draw.h"
#include "game/map/grid_state.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/weather/weather_control.h"
#include "game/weather/weather_lighting.h"
#include "game/weather/weather_strike.h"
#include "game/weather/weather_update.h"
#include "game/messages/message_handler.h"
#include "game/objects/building_methods.h"
#include "game/objects/object_scenery.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace obl::weather {
namespace {

using object::Guest;
using MessageHandler = messages::Handler;
using layout::World;
using weather::RainDropBuffer;
using weather::Weather;
using weather::WeatherEntry;

const auto kApplyModifiers = &object::GuestApplyWeatherModifiers;
const auto kTilePosition = [](object::Base* object, int* out) { return object->GetTilePosition(out); };

constexpr int kGuestClass = 3;
constexpr float kRainFadeSeconds = 2.0f;
constexpr float kRainStartFadeSeconds = 5.0f;
constexpr uint8_t kRainLoopFlags = 2;

MessageHandler* GetMessageHandler()
{
    return messages::message_facade::Current();
}
int32_t& BoltDrift()
{
    return game::LightningBoltDrift();
}

// ---- the lightning-bolt creation ---------------------------------------------------------------------------------------------

test::CallCounter g_answeredBolt("weather.create-lightning-bolt");

// The game side of the bolt Env (src/game/weather/weather_strike.h): the bitmap is an `operator new` block (weather.update-lighting-driver frees it).
struct GameBoltEnv {
    void* NewBitmap(uint32_t bytes) { return ::operator new(bytes); }
    int Rand() { return game::Rand(); }
    int32_t& Drift() { return BoltDrift(); }
};

} // namespace

namespace runtime {

void CreateLightningBolt(Weather* weatherState)
{
    g_answeredBolt.Count();
    GameBoltEnv env;
    weather::CreateLightningBolt(*weatherState, game::GetRenderer()->m_height, env);
}

} // namespace runtime

namespace {

// ---- weather.update-lighting-driver --------------------------------------------------------------------------------------------------

test::CallCounter g_answeredLighting("weather.update-lighting");
test::CallCounter g_anchorLighting("weather.update-lighting-driver");
test::CallCounter g_answeredStrike("weather.update-lighting-strike");

} // namespace

namespace runtime {

void DrawLightning(Weather* weatherState)
{
    DrawLightningFrame(weatherState);
}

} // namespace runtime

namespace {

// The game side of the Env contracts in src/game/weather/weather_lighting.h and weather_strike.h: the game's own functions and objects.
class GameLightingEnv {
public:
    using Message = messages::Message;
    explicit GameLightingEnv(Weather& weather) : m_weather(weather), m_objects(game::GetObjectManager()) {}
    void DrawLightning() { runtime::DrawLightning(&m_weather); }
    void ReleaseLightmap(void* lightmap) { ::operator delete(lightmap); }

    const float* ObjectPoint(int32_t id)
    {
        object::Base* object = object::object_manager::ObjectById(m_objects, id);
        return object ? object->m_WorldXYZ.begin() : nullptr;
    }
    void DamageTarget(int32_t id)
    {
        object::BuildingApplyLightningDamage(static_cast<object::Building*>(object::object_manager::ObjectById(m_objects, id)));
    }
    int Rand() { return game::Rand(); }
    void CreateBolt() { runtime::CreateLightningBolt(&m_weather); }
    weather::StrikeTile PointToTile(const float* point)
    {
        const grid_math::TileXY tile = game::GetGrid()->WorldPointToTile(point[0], point[1]);
        return {static_cast<int32_t>(tile.x), static_cast<int32_t>(tile.y)};
    }
    uint32_t NearObjects(weather::StrikeTile tile, int32_t radius, object::Base* const** list)
    {
        m_near = object::object_manager::GetObjectsNearTile(m_objects, tile.x, tile.y, radius);
        *list = m_near.data();
        return static_cast<uint32_t>(m_near.size());
    }
    int ObjectState(const object::Base* object) { return object->m_state; }
    void SetActionState(object::Base* guest, int32_t value) { static_cast<Guest*>(guest)->m_ActionState = value; }
    void Post(messages::Message& message) { messages::message_facade::Post(GetMessageHandler(), &message); }
    uint32_t BuildingCount() { return m_objects->m_buildings.size(); }
    object::Base* Building(uint32_t i) { return m_objects->m_buildings[i]; }
    uint32_t GridWidth(uint32_t i) { return static_cast<uint32_t>(Definition(i).m_GridWidth); }
    uint32_t GridHeight(uint32_t i) { return static_cast<uint32_t>(Definition(i).m_GridHeight); }
    weather::StrikeTile TilePosition(object::Base* object)
    {
        int tile[2] = {};
        kTilePosition(object, tile);
        return {tile[0], tile[1]};
    }
    weather::StrikeTile Neighbour(weather::StrikeTile tile, int direction)
    {
        const grid_math::TileXY next =
                game::GetGrid()->GetNeighbour(static_cast<uint32_t>(tile.x), static_cast<uint32_t>(tile.y), direction);
        return {static_cast<int32_t>(next.x), static_cast<int32_t>(next.y)};
    }
    void DamageBuilding(object::Base* building) { object::BuildingApplyLightningDamage(static_cast<object::Building*>(building)); }
    uint32_t OtherCount() { return m_objects->m_others.size(); }
    object::Base* Other(uint32_t i) { return m_objects->m_others[i]; }
    bool Repairable(const object::Base* scenery) { return static_cast<const object::Scenery*>(scenery)->m_definition->m_repair != 0; }
    void Vandalise(object::Base* scenery) { static_cast<object::Scenery*>(scenery)->Vandalise(); }
    int FindSound(const char* name) { return audio::CurrentSound()->FindChannel(name); }
    void StartSound(int channel) { audio::CurrentSound()->StartChannel(channel); }
    int PlaySound(const char* name) { return audio::CurrentSound()->Play(name, weather::strike::kThunderFlags, 0.0f); }
    void SetSoundVolume(int channel, float volume) { audio::CurrentSound()->SetChannelVolume(channel, volume); }

protected:
    const tables::BuildingDef& Definition(uint32_t i) const
    {
        return *static_cast<const object::Building*>(m_objects->m_buildings[i])->m_definition;
    }

    Weather& m_weather;
    object::Manager* m_objects;
    object::object_manager::ObjectList m_near; // the last NearObjects result, read through the pointer it returned
};

struct LightingClock {
    float hour, gameTime;
    bool rgb555;
};
LightingClock ReadLightingClock()
{
    const World* world = game::GetWorld();
    return {world->m_gameDayTime, world->m_gameTime, game::GetRenderer()->m_rgb555 == 1};
}

bool StrikeTargetFound(const Weather& weather)
{
    return weather.m_hasStrikeTarget && object::object_manager::ObjectById(game::GetObjectManager(), weather.m_strikeBuildingId) != nullptr;
}

} // namespace

namespace runtime {

void UpdateLighting(Weather* weatherState)
{
    const LightingClock clock = ReadLightingClock();
    if (weather::LightingUnsupported(*weatherState, clock.hour, clock.gameTime, StrikeTargetFound(*weatherState))) {
        fatal::UnhandledInput("weather.update-lighting-driver", "state out of range or strike area empty");
    }
    g_answeredLighting.Count();
    if (weather::lighting::StrikeDue(*weatherState, clock.hour, clock.gameTime)) g_answeredStrike.Count();
    GameLightingEnv env(*weatherState);
    weather::UpdateLighting(*weatherState, clock.hour, clock.gameTime, clock.rgb555, env);
}

} // namespace runtime

namespace {

// The game side of the Env contract in src/game/weather/weather_update.h: the schedule entries behind the deque, the guest list of the object
// manager, the message handler's post, the lighting and the sound functions - all the game's own.
class ScheduleEnv {
public:
    using Message = messages::Message;
    explicit ScheduleEnv(Weather& weather) : m_weather(weather) {}

    WeatherEntry& Entry(uint32_t i) { return *m_weather.m_schedule[i]; }
    uint32_t GuestCount()
    {
        m_guests = object::object_manager::GetListByClass(game::GetObjectManager(), kGuestClass);
        return static_cast<uint32_t>(m_guests.size());
    }
    int GuestId(uint32_t i) { return static_cast<int>(m_guests[i]->m_id); }
    void ApplyModifiers(uint32_t i) { kApplyModifiers(static_cast<Guest*>(m_guests[i])); }
    void Post(messages::Message& message) { messages::message_facade::Post(game::GetWorld()->m_messageHandler, &message); }
    void UpdateLighting() { runtime::UpdateLighting(&m_weather); }
    void FadeOutRain(int channel) { audio::CurrentSound()->FadeChannelOut(channel, kRainFadeSeconds); }
    int PlayRainLoop(const char* name)
    {
        return audio::CurrentSound()->PlayScaled(name, weather::kRainVolume, kRainLoopFlags, kRainStartFadeSeconds);
    }
    void SetRainVolume(int channel) { audio::CurrentSound()->SetChannelVolume(channel, weather::kRainVolume); }

protected:
    Weather& m_weather;
    object::object_manager::ObjectList m_guests;
};

test::CallCounter g_answeredSchedule("weather.update-schedule");

} // namespace

namespace runtime {

ScheduleProbe g_scheduleProbe = nullptr;

void RunSchedule(Weather* weatherState)
{
    g_answeredSchedule.Count();
    const World* world = game::GetWorld();
    ScheduleEnv env(*weatherState);
    weather::UpdateSchedule(*weatherState, static_cast<uint32_t>(world->m_gameDay), world->m_gameDayTime, env);
}

void UpdateSchedule(Weather* weatherState)
{
    if (g_scheduleProbe) {
        g_scheduleProbe(weatherState);
        return;
    }
    RunSchedule(weatherState);
}

} // namespace runtime

namespace {

// ---- the weather entry insertion -----------------------------------------------------------------------------------------------------

test::CallCounter g_answeredInsert("weather.insert-entry");

class InsertEnv {
public:
    using Message = messages::Message;
    explicit InsertEnv(Weather& weather) : m_weather(weather) {}

    uint32_t Count() { return static_cast<uint32_t>(m_weather.m_schedule.size()); }
    WeatherEntry& Entry(uint32_t i) { return *m_weather.m_schedule[i]; }
    WeatherEntry* NewEntry(const WeatherEntry& entry) { return new WeatherEntry(entry); }
    void InsertAt(uint32_t position, WeatherEntry* stored)
    {
        m_weather.m_schedule.insert(m_weather.m_schedule.begin() + position, std::unique_ptr<WeatherEntry>(stored));
    }

private:
    Weather& m_weather;
};

} // namespace

namespace runtime {

void InsertEntry(Weather* weatherState, const WeatherEntry& entry)
{
    g_answeredInsert.Count();
    InsertEnv env(*weatherState);
    weather::InsertEntry(*weatherState, entry, env);
}

} // namespace runtime

namespace {

// ---- weather.randomize-rain-drops-driver ----------------------------------------------------------------------------------------------

test::CallCounter g_answeredRain("weather.randomize-rain-drops");
test::CallCounter g_anchorRain("weather.randomize-rain-drops-driver"); // `call-drive weather.randomize-rain-drops-driver`

RainDropBuffer& Drops()
{
    return runtime::RainDrops();
}

} // namespace

namespace runtime {

void RandomizeRainDrops()
{
    const ui::MapView* view = ui::GetMapView();
    g_answeredRain.Count();
    weather::RandomizeRainDrops(Drops(), view->m_viewWidth, view->m_viewHeight, [] { return game::Rand(); });
}

} // namespace runtime

namespace {

// ---- the small methods: colour override, lightning controls, modifiers ------------------------------------------------------

struct MethodCounters {
    test::CallCounter m_anchor, m_observed;
    MethodCounters(const char* anchorName, const char* observedName) : m_anchor(anchorName), m_observed(observedName) {}
};
MethodCounters g_setRed("weather.set-override-red-driver", "weather.set-override-red");
MethodCounters g_setGreen("weather.set-override-green-driver", "weather.set-override-green");
MethodCounters g_setBlue("weather.set-override-blue-driver", "weather.set-override-blue");
MethodCounters g_getRed("weather.get-override-red-driver", "weather.get-override-red");
MethodCounters g_getGreen("weather.get-override-green-driver", "weather.get-override-green");
MethodCounters g_getBlue("weather.get-override-blue-driver", "weather.get-override-blue");
MethodCounters g_thunder("weather.set-thunder-level-driver", "weather.set-thunder-level");
MethodCounters g_area("weather.set-lightning-area-driver", "weather.set-lightning-area");
MethodCounters g_target("weather.set-lightning-target-by-name-driver", "weather.set-lightning-target-by-name");
MethodCounters g_modifiers("weather.get-current-modifiers-driver", "weather.get-current-modifiers");

} // namespace

namespace runtime {

void SetOverrideRed(Weather* weatherState, float value)
{
    g_setRed.m_observed.Count();
    weather::SetOverrideRed(*weatherState, value);
}
void SetOverrideGreen(Weather* weatherState, float value)
{
    g_setGreen.m_observed.Count();
    weather::SetOverrideGreen(*weatherState, value);
}
void SetOverrideBlue(Weather* weatherState, float value)
{
    g_setBlue.m_observed.Count();
    weather::SetOverrideBlue(*weatherState, value);
}

double GetOverrideRed(Weather* weatherState)
{
    g_getRed.m_observed.Count();
    return weather::GetOverrideRed(*weatherState);
}
double GetOverrideGreen(Weather* weatherState)
{
    g_getGreen.m_observed.Count();
    return weather::GetOverrideGreen(*weatherState);
}
double GetOverrideBlue(Weather* weatherState)
{
    g_getBlue.m_observed.Count();
    return weather::GetOverrideBlue(*weatherState);
}

void SetThunderLevel(Weather* weatherState, int level)
{
    g_thunder.m_observed.Count();
    weather::SetThunderLevel(*weatherState, level, game::GetWorld()->m_gameTime);
}

void SetLightningArea(Weather* weatherState, int left, int top, int right, int bottom)
{
    g_area.m_observed.Count();
    weather::SetLightningArea(*weatherState, left, top, right, bottom);
}

} // namespace runtime

namespace {

// The building list of the object manager: the key of a building's definition (BuildingDef+4) is what `Disaster Strike Target` names.
class TargetEnv {
public:
    using Message = messages::Message;
    TargetEnv() : m_objects(game::GetObjectManager()) {}
    uint32_t BuildingCount() { return m_objects->m_buildings.size(); }
    bool KeyEquals(uint32_t i, const char* key)
    {
        const auto* building = static_cast<const object::Building*>(m_objects->m_buildings[i]);
        return ascii::EqualsNoCase(building->m_definition->m_key, key);
    }
    int32_t BuildingId(uint32_t i) { return static_cast<int32_t>(m_objects->m_buildings[i]->m_id); }
    int Rand() { return game::Rand(); }

private:
    const object::Manager* m_objects;
};

} // namespace

namespace runtime {

void SetLightningTargetByName(Weather* weatherState, const char* key)
{
    g_target.m_observed.Count();
    TargetEnv env;
    weather::SetLightningTargetByName(*weatherState, key, env);
}

float* GetCurrentModifiers(Weather* weatherState)
{
    g_modifiers.m_observed.Count();
    return weather::CurrentModifiers(*weatherState);
}

} // namespace runtime

namespace {

// ---- drivers -----------------------------------------------------------------------------------------------------------------

struct Lcg {
    uint32_t seed;
    uint32_t operator()() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
};

// `call-drive weather.set-override-red-driver|Green|Blue <rounds>`: values around the byte boundaries and far outside 0..1 (wrapping, NaN, infinity) on
// colours of every pattern; the colour is put back.
void DriveOverrideSet(int rounds, void (*setOverride)(Weather*, float))
{
    Weather* weather = game::GetWeather();
    if (!weather) {
        OBL_LOG("autotest: FAILED call-drive weather.set-override-*-driver: no weather");
        return;
    }
    const uint32_t saved = weather->m_manualColor;
    static const float kValues[] = {0.0f, 1.0f, 0.5f, 0.999f, 1.0f / 255.0f, 128.0f / 255.0f, 254.9f / 255.0f, 1.001f, 2.0f, -0.001f, -1.0f,
            300.0f, 1.0e10f, -1.0e10f, 1.0e30f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    Lcg next{0x51ed};
    for (int round = 0; round < rounds; ++round)
        for (float value : kValues) {
            weather->m_manualColor = next() ^ (next() << 12);
            setOverride(weather, value);
        }
    weather->m_manualColor = saved;
}

// `call-drive weather.get-override-red-driver|Green|Blue <rounds>`: every byte value in every channel, on random other bytes (the top byte too).
void DriveOverrideGet(int rounds, double (*getOverride)(Weather*))
{
    Weather* weather = game::GetWeather();
    if (!weather) {
        OBL_LOG("autotest: FAILED call-drive weather.get-override-*-driver: no weather");
        return;
    }
    const uint32_t saved = weather->m_manualColor;
    Lcg next{0xc01a};
    for (int round = 0; round < rounds; ++round)
        for (uint32_t byte = 0; byte < 256; ++byte)
            for (int shift = 0; shift < 32; shift += 8) {
                weather->m_manualColor = ((next() ^ (next() << 12)) & ~(0xffu << shift)) | (byte << shift);
                getOverride(weather);
            }
    weather->m_manualColor = saved;
}

void DriveLighting(int rounds)
{
    Weather* weather = game::GetWeather();
    World* world = game::GetWorld();
    ui::Renderer* renderer = game::GetRenderer();
    if (!weather || !world || !renderer) {
        OBL_LOG("autotest: FAILED call-drive weather.update-lighting-driver: no weather, world or renderer");
        return;
    }
    Weather saved;
    saved.m_lightIndex = weather->m_lightIndex;
    saved.m_previousLightIndex = weather->m_previousLightIndex;
    saved.m_currentState = weather->m_currentState;
    saved.m_previousState = weather->m_previousState;
    saved.m_weatherTime = weather->m_weatherTime;
    saved.m_manualLighting = weather->m_manualLighting;
    saved.m_manualColor = weather->m_manualColor;
    saved.m_strikes = weather->m_strikes;
    saved.m_lightFlash = weather->m_lightFlash;
    saved.m_flashTime = weather->m_flashTime;
    saved.m_lightningLightmap = weather->m_lightningLightmap;
    saved.m_lastColor = weather->m_lastColor;
    std::memcpy(saved.m_lightKeys, weather->m_lightKeys, sizeof(saved.m_lightKeys));
    std::memcpy(saved.m_stateColors, weather->m_stateColors, sizeof(saved.m_stateColors));
    std::memcpy(saved.m_toneTables, weather->m_toneTables, sizeof(saved.m_toneTables));
    const float savedHour = world->m_gameDayTime, savedTime = world->m_gameTime;
    const int savedFormat = renderer->m_rgb555;
    const size_t bitmapSize = 200 * static_cast<size_t>(renderer->m_height > 0 ? renderer->m_height : 600) + 64;
    Lcg next{0x1161};
    auto unit = [&] { return static_cast<float>(next() & 0xffffff) / 16777216.0f; };
    static const float kFlashPhases[] = {
            -0.1f, 0.0f, 0.05f, 0.1499f, 0.15f, 0.2f, 0.2999f, 0.3f, 0.35f, 0.4499f, 0.45f, 0.5f, 0.5999f, 0.6f, 0.7f, 5.0f};
    for (int round = 0; round < rounds; ++round) {
        for (weather::LightKey& key : weather->m_lightKeys) key.m_color = next() ^ (next() << 12);
        for (uint32_t& color : weather->m_stateColors) color = next() ^ (next() << 12);
        const uint32_t pick = next();
        weather->m_lightIndex = pick % 24;
        weather->m_previousLightIndex = (weather->m_lightIndex + 23) % 24;
        float hour = unit() * 24.0f;
        if (pick & 0x100) hour = static_cast<float>((pick >> 12) % 24) + (pick & 0x200 ? 0.0f : 0.999f);
        world->m_gameDayTime = hour;
        world->m_gameTime = 10.0f + unit() * 1000.0f;
        weather->m_currentState = (pick >> 4) % 6;
        weather->m_previousState = (pick & 0x400) ? weather->m_currentState : (pick >> 8) % 6;
        // a change time just behind the hour (inside the blend), well behind it, ahead of it (a day back), and exactly on the blend end
        const float ago[] = {0.0f, 0.05f, 0.2499f, 0.25f, 0.2501f, 1.0f, -1.0f};
        weather->m_weatherTime = hour - ago[(pick >> 13) % 7] + (unit() < 0.1f ? unit() : 0.0f);
        weather->m_manualLighting = (pick & 0x7000) == 0;
        weather->m_manualColor = next() ^ (next() << 12);
        weather->m_strikes = 0;
        weather->m_lightFlash = (pick & 0x30000) != 0;
        weather->m_flashTime = world->m_gameTime - kFlashPhases[(pick >> 20) % (sizeof(kFlashPhases) / sizeof(kFlashPhases[0]))];
        weather->m_lightningLightmap = nullptr;
        if (weather->m_lightFlash && (pick & 0x40000)) {
            weather->m_lightningLightmap = ::operator new(bitmapSize);
            std::memset(weather->m_lightningLightmap, 0, bitmapSize);
        }
        renderer->m_rgb555 = (pick & 0x80000) ? 1 : 0;
        runtime::UpdateLighting(weather);
        if (weather->m_lightningLightmap) {
            ::operator delete(weather->m_lightningLightmap);
            weather->m_lightningLightmap = nullptr;
        }
    }
    weather->m_lightIndex = saved.m_lightIndex;
    weather->m_previousLightIndex = saved.m_previousLightIndex;
    weather->m_currentState = saved.m_currentState;
    weather->m_previousState = saved.m_previousState;
    weather->m_weatherTime = saved.m_weatherTime;
    weather->m_manualLighting = saved.m_manualLighting;
    weather->m_manualColor = saved.m_manualColor;
    weather->m_strikes = saved.m_strikes;
    weather->m_lightFlash = saved.m_lightFlash;
    weather->m_flashTime = saved.m_flashTime;
    weather->m_lightningLightmap = saved.m_lightningLightmap;
    weather->m_lastColor = saved.m_lastColor;
    std::memcpy(weather->m_lightKeys, saved.m_lightKeys, sizeof(saved.m_lightKeys));
    std::memcpy(weather->m_stateColors, saved.m_stateColors, sizeof(saved.m_stateColors));
    std::memcpy(weather->m_toneTables, saved.m_toneTables, sizeof(saved.m_toneTables));
    world->m_gameDayTime = savedHour;
    world->m_gameTime = savedTime;
    renderer->m_rgb555 = savedFormat;
}

// `call-drive weather.set-thunder-level-driver <rounds>`: the levels a script can ask for (0 stops the storm, 1..10) in orders that start a storm,
// change it and stop it, with the world time moved around. The strike state and the clock are put back.
void DriveThunderLevel(int rounds)
{
    Weather* weather = game::GetWeather();
    World* world = game::GetWorld();
    if (!weather || !world) {
        OBL_LOG("autotest: FAILED call-drive weather.set-thunder-level-driver: no weather or world");
        return;
    }
    const int savedLevel = weather->m_strikeLevel;
    const uint8_t savedStrikes = weather->m_strikes;
    const float savedStrikeTime = weather->m_nextStrikeTime;
    const float savedTime = world->m_gameTime;
    Lcg next{0x7a11};
    for (int round = 0; round < rounds; ++round) {
        world->m_gameTime = static_cast<float>(next() % 100000) * 0.25f;
        for (int level : {0, 1, 5, 10, 3, 0, 7, 0, 0, 2, 10, 0}) {
            weather->m_nextStrikeTime = static_cast<float>(next() % 1000);
            runtime::SetThunderLevel(weather, level);
        }
    }
    weather->m_strikeLevel = savedLevel;
    weather->m_strikes = savedStrikes;
    weather->m_nextStrikeTime = savedStrikeTime;
    world->m_gameTime = savedTime;
}

void DriveLightningArea(int rounds)
{
    Weather* weather = game::GetWeather();
    if (!weather) {
        OBL_LOG("autotest: FAILED call-drive weather.set-lightning-area-driver: no weather");
        return;
    }
    const auto saved = weather->m_strikeArea;
    Lcg next{0xa4ea};
    for (int round = 0; round < rounds; ++round) {
        const int left = static_cast<int>(next()), top = -static_cast<int>(next());
        runtime::SetLightningArea(weather, left, top, static_cast<int>(next() % 4000), static_cast<int>(next() % 3000));
    }
    weather->m_strikeArea = saved;
}

// `call-drive weather.set-lightning-target-by-name-driver <rounds>`: the key of every building in the world (several share one, so the draw picks among
// the matches), a key that exists nowhere, the empty key and a prefix of each real key. The strike flags and target are put back.
void DriveLightningTarget(int rounds)
{
    Weather* weather = game::GetWeather();
    const object::Manager* objects = game::GetObjectManager();
    if (!weather || !objects) {
        OBL_LOG("autotest: FAILED call-drive weather.set-lightning-target-by-name-driver: no weather or objects");
        return;
    }
    const uint8_t savedStrikes = weather->m_strikes, savedTarget = weather->m_hasStrikeTarget;
    const int savedId = weather->m_strikeBuildingId;
    std::vector<std::string> keys = {"", "NO_SUCH_BUILDING", "x"};
    for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
        const std::string key = static_cast<const object::Building*>(objects->m_buildings[i])->m_definition->m_key;
        keys.push_back(key);
        if (key.size() > 1) keys.push_back(key.substr(0, key.size() - 1));
    }
    for (int round = 0; round < rounds; ++round)
        for (const std::string& key : keys) {
            weather->m_strikes = static_cast<uint8_t>(round & 1);
            weather->m_hasStrikeTarget = static_cast<uint8_t>(round & 1);
            runtime::SetLightningTargetByName(weather, key.c_str());
        }
    weather->m_strikes = savedStrikes;
    weather->m_hasStrikeTarget = savedTarget;
    weather->m_strikeBuildingId = savedId;
}

void DriveModifiers(int rounds)
{
    Weather* weather = game::GetWeather();
    if (!weather) {
        OBL_LOG("autotest: FAILED call-drive weather.get-current-modifiers-driver: no weather");
        return;
    }
    for (int round = 0; round < rounds; ++round) runtime::GetCurrentModifiers(weather);
}

// `call-drive weather.randomize-rain-drops-driver <rounds>`: the game calls it on a zoom or resolution change only, so the driver feeds it view
// rectangles of different shapes (equal sides, one pixel, wide, tall) and restores the real one and the drop buffer afterwards.
void DriveRain(int rounds)
{
    ui::MapView* view = ui::GetMapView();
    if (!view) {
        OBL_LOG("autotest: FAILED call-drive weather.randomize-rain-drops-driver: no map view");
        return;
    }
    const auto savedDrops = std::make_unique<RainDropBuffer>(Drops());
    const int32_t width = view->m_viewWidth, height = view->m_viewHeight;
    static const int32_t kSizes[][2] = {{800, 600}, {1, 1}, {1, 7}, {1024, 768}, {3840, 2160}, {2, 600}, {4096, 4096}, {641, 479}};
    for (int round = 0; round < rounds; ++round)
        for (const auto& size : kSizes) {
            view->m_viewWidth = size[0];
            view->m_viewHeight = size[1];
            runtime::RandomizeRainDrops();
        }
    view->m_viewWidth = width;
    view->m_viewHeight = height;
    Drops() = *savedDrops;
}

} // namespace

bool RegisterTestDrivers()
{
    g_anchorRain.SetDriver(&DriveRain);
    g_setRed.m_anchor.SetDriver([](int n) { DriveOverrideSet(n, &runtime::SetOverrideRed); });
    g_setGreen.m_anchor.SetDriver([](int n) { DriveOverrideSet(n, &runtime::SetOverrideGreen); });
    g_setBlue.m_anchor.SetDriver([](int n) { DriveOverrideSet(n, &runtime::SetOverrideBlue); });
    g_getRed.m_anchor.SetDriver([](int n) { DriveOverrideGet(n, &runtime::GetOverrideRed); });
    g_getGreen.m_anchor.SetDriver([](int n) { DriveOverrideGet(n, &runtime::GetOverrideGreen); });
    g_getBlue.m_anchor.SetDriver([](int n) { DriveOverrideGet(n, &runtime::GetOverrideBlue); });
    g_thunder.m_anchor.SetDriver(&DriveThunderLevel);
    g_area.m_anchor.SetDriver(&DriveLightningArea);
    g_target.m_anchor.SetDriver(&DriveLightningTarget);
    g_modifiers.m_anchor.SetDriver(&DriveModifiers);
    g_anchorLighting.SetDriver(&DriveLighting);
    return true;
}

// Test commands of the weather (tests/weather_strike_s3.txt); they call the game's Weather methods.
//   lightning <level> <left> <top> <right> <bottom>   a storm of `level` (1..10) striking at random points of the rectangle (world units)
//   lightning <level> building <index> <radius>       the same around the world point of the index-th building
//   lightning-target <KEY> | building <index>          the next strike hits a random building of that type (Disaster Strike Target)
//   lightning-state                                   logs the strike fields (key=value, for expect-last)
bool ExecuteCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "lightning" && name != "lightning-target" && name != "lightning-state") return false;
    Weather* weather = game::GetWeather();
    const object::Manager* objects = game::GetObjectManager();
    if (!weather || !objects || !game::GetWorld()) {
        OBL_LOG("autotest: FAILED %s needs a world", name.c_str());
        return true;
    }
    if (name == "lightning-target") {
        std::string key = args.size() == 1 ? args[0] : std::string();
        if (args.size() == 2 && args[0] == "building" && static_cast<uint32_t>(std::atoi(args[1].c_str())) < objects->m_buildings.size())
            key = static_cast<const object::Building*>(objects->m_buildings[std::atoi(args[1].c_str())])->m_definition->m_key;
        if (key.empty()) {
            OBL_LOG("autotest: FAILED lightning-target <KEY> | building <index>");
            return true;
        }
        runtime::SetLightningTargetByName(weather, key.c_str());
        OBL_LOG("autotest: lightning-target %s target=%d armed=%d", key.c_str(),
                weather->m_hasStrikeTarget ? weather->m_strikeBuildingId : -1, weather->m_strikes);
        return true;
    }
    if (name == "lightning") {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (args.size() == 5) {
            left = std::atoi(args[1].c_str());
            top = std::atoi(args[2].c_str());
            right = std::atoi(args[3].c_str());
            bottom = std::atoi(args[4].c_str());
        } else if (args.size() == 4 && args[1] == "building" &&
                static_cast<uint32_t>(std::atoi(args[2].c_str())) < objects->m_buildings.size()) {
            const object::Base* building = objects->m_buildings[std::atoi(args[2].c_str())];
            const int radius = std::atoi(args[3].c_str());
            left = static_cast<int>(building->m_WorldXYZ[0]) - radius;
            top = static_cast<int>(building->m_WorldXYZ[1]) - radius;
            right = static_cast<int>(building->m_WorldXYZ[0]) + radius;
            bottom = static_cast<int>(building->m_WorldXYZ[1]) + radius;
        } else {
            OBL_LOG("autotest: FAILED lightning <level> (<left> <top> <right> <bottom> | building <index> <radius>)");
            return true;
        }
        runtime::SetLightningArea(weather, left, top, right, bottom);
        if (weather->m_strikeLevel != 0)
            runtime::SetThunderLevel(weather, 0); // a running storm: stop it, so the new level arms the first strike
        runtime::SetThunderLevel(weather, std::atoi(args[0].c_str()));
    }
    OBL_LOG("autotest: lightning-state strikes=%d level=%d next=%.3f flash=%d target=%d point=%.1f,%.1f area=%d,%d,%d,%d",
            weather->m_strikes, weather->m_strikeLevel, weather->m_nextStrikeTime, weather->m_lightFlash,
            weather->m_hasStrikeTarget ? weather->m_strikeBuildingId : -1, weather->m_strikePoint[0], weather->m_strikePoint[1],
            weather->m_strikeArea.left, weather->m_strikeArea.top, weather->m_strikeArea.right, weather->m_strikeArea.bottom);
    return true;
}

} // namespace obl::weather
