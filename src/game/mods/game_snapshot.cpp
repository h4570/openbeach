#include "game/mods/game_snapshot.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_manager_state.h"
#include "game/player/player_state.h"
#include "game/ui/map_view.h"
#include "game/weather/weather_state.h"
#include "game/world/world_state.h"

namespace obl::mods {

namespace {

// The names of the weather kinds as the scenario scripts write them (world_script_parse.cpp), in lowercase.
constexpr const char* kWeather[] = {"heat_wave", "normal", "overcast", "light_rain", "heavy_rain", "thunderstorm"};

const char* KindName(object::ObjectType type)
{
    using object::ObjectType;
    switch (type) {
    case ObjectType::Building: return "building";
    case ObjectType::Guest: return "guest";
    case ObjectType::Scenery: return "scenery";
    case ObjectType::Vehicle: return "vehicle";
    case ObjectType::ServiceStaff:
    case ObjectType::Lifeguard:
    case ObjectType::Mechanic:
    case ObjectType::Rep:
    case ObjectType::Builder:
    case ObjectType::Cleaner:
    case ObjectType::SecurityGuard: return "staff";
    default: return "other";
    }
}

ObjectInfo Describe(const object::Base& object)
{
    ObjectInfo info;
    info.id = object.m_id;
    info.kind = KindName(object.m_type);
    if (object.m_type == object::ObjectType::Building) {
        const auto& building = static_cast<const object::Building&>(object);
        if (building.m_definition) info.key = building.m_definition->m_key;
    }
    // The static conversion, not Base::GetTilePosition: that one is counted for the call-counter tests.
    const auto tile = ui::MapView::WorldToTile(object.m_WorldXYZ.begin());
    info.x = static_cast<int>(tile.x);
    info.y = static_cast<int>(tile.y);
    info.state = object.m_state;
    return info;
}

// The selection levels the objects keep (guest_selection.h): 1 under the cursor, 2 selected.
constexpr int kHovered = 1, kSelected = 2;

void FindSelection(const std::vector<object::Base*>& objects, GameSnapshot& snapshot)
{
    for (const object::Base* object : objects) {
        if (!object) continue;
        if (object->m_SelectLevel == kSelected && !snapshot.selected) snapshot.selected = Describe(*object);
        if (object->m_SelectLevel == kHovered && !snapshot.hovered) snapshot.hovered = Describe(*object);
    }
}

} // namespace

std::optional<GameSnapshot> TakeGameSnapshot()
{
    const world::World* world = game::GetWorld();
    const object::Manager* manager = game::GetObjectManager();
    if (!world || !manager) return std::nullopt;
    GameSnapshot snapshot;
    if (world->m_player) snapshot.money = world->m_player->m_cash;
    snapshot.day = world->m_gameDay;
    snapshot.hour = world->m_gameDayTime;
    snapshot.speedLevel = world->m_gameSpeedLevel;
    snapshot.paused = world->m_paused != 0;
    if (const weather::Weather* weather = game::GetWeather())
        if (weather->m_currentState < std::size(kWeather)) snapshot.weather = kWeather[weather->m_currentState];
    double happiness = 0;
    for (const object::Base* guest : manager->m_guests)
        if (guest) {
            happiness += static_cast<const object::Guest*>(guest)->m_happiness;
            ++snapshot.guests;
        }
    if (snapshot.guests > 0) snapshot.mood = happiness / snapshot.guests;
    for (const object::Base* building : manager->m_buildings)
        if (building) snapshot.buildings.push_back(Describe(*building));
    FindSelection(manager->m_buildings, snapshot);
    FindSelection(manager->m_guests, snapshot);
    FindSelection(manager->m_others, snapshot);
    return snapshot;
}

} // namespace obl::mods
