#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/weather/weather_strike.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The Env of weather::Strike() over plain containers (tests/unit/weather_strike_test.cpp, weather_lighting_test.cpp): a grid where direction 0
// is x + 1 and direction 6 is y + 1, rand() from a list, and every effect recorded.
namespace obl::unit {

struct FakeStrikeEnv {
    using Message = messages::Message;
    struct Building {
        object::Base* object;
        uint32_t width, height;
    };

    // the world
    std::map<int32_t, std::array<float, 3>> points; // ObjectPoint by id
    std::map<const object::Base*, weather::StrikeTile> tiles;
    weather::StrikeTile pointTile{}; // what every PointToTile returns
    std::vector<object::Base*> near;
    std::vector<Building> buildings;
    std::vector<object::Base*> others;
    std::set<const object::Base*> repairable;
    std::vector<int> rands;
    int findResult = -1, playResult = 7;

    // what the strike did
    size_t randCalls = 0;
    int bolts = 0;
    std::vector<int32_t> targets;
    std::vector<const object::Base*> damaged, vandalised;
    std::vector<std::pair<object::Base*, int32_t>> actions;
    std::vector<messages::Message> posts;
    std::vector<std::string> sound;
    weather::StrikeTile nearTile{-1, -1};
    int32_t nearRadius = 0;

    const float* ObjectPoint(int32_t id)
    {
        const auto it = points.find(id);
        return it != points.end() ? it->second.data() : nullptr;
    }
    void DamageTarget(int32_t id) { targets.push_back(id); }
    int Rand() { return randCalls < rands.size() ? rands[randCalls++] : (++randCalls, 0); }
    void CreateBolt() { ++bolts; }
    weather::StrikeTile PointToTile(const float*) { return pointTile; }
    uint32_t NearObjects(weather::StrikeTile tile, int32_t radius, object::Base* const** list)
    {
        nearTile = tile;
        nearRadius = radius;
        *list = near.data();
        return static_cast<uint32_t>(near.size());
    }
    int ObjectState(const object::Base* object) { return object->m_state; }
    void SetActionState(object::Base* guest, int32_t value) { actions.push_back({guest, value}); }
    void Post(messages::Message& message) { posts.push_back(message); }
    uint32_t BuildingCount() { return static_cast<uint32_t>(buildings.size()); }
    object::Base* Building(uint32_t i) { return buildings[i].object; }
    uint32_t GridWidth(uint32_t i) { return buildings[i].width; }
    uint32_t GridHeight(uint32_t i) { return buildings[i].height; }
    weather::StrikeTile TilePosition(object::Base* object) { return tiles.at(object); }
    weather::StrikeTile Neighbour(weather::StrikeTile tile, int direction)
    {
        return direction == 0 ? weather::StrikeTile{tile.x + 1, tile.y} : weather::StrikeTile{tile.x, tile.y + 1};
    }
    void DamageBuilding(object::Base* building) { damaged.push_back(building); }
    uint32_t OtherCount() { return static_cast<uint32_t>(others.size()); }
    object::Base* Other(uint32_t i) { return others[i]; }
    bool Repairable(const object::Base* scenery) { return repairable.count(scenery) != 0; }
    void Vandalise(object::Base* scenery) { vandalised.push_back(scenery); }
    int FindSound(const char* name)
    {
        sound.push_back(std::string("find ") + name);
        return findResult;
    }
    void StartSound(int channel) { sound.push_back("start " + std::to_string(channel)); }
    int PlaySound(const char* name)
    {
        sound.push_back(std::string("play ") + name);
        return playResult;
    }
    void SetSoundVolume(int channel, float volume) { sound.push_back("volume " + std::to_string(channel) + " " + std::to_string(volume)); }
};

} // namespace obl::unit
