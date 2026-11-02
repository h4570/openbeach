#pragma once

#include "game/events/event_state.h"
#include "game/weather/weather_state.h"

#include "game/ferry/ferry_state.h"
#include "game/messages/message_state.h"
#include "game/world/world_script_types.h"
#include "game/objects/script_message.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// The hand-over commands of the scenario script (WorldScriptExecute opcodes 0x14 `BoatEvent`, 0x15 `CircuitPoint`, 0x16 `Disaster`,
// 0x17 `Weather`, and 0xc `Create`, 0xf `LoadScript`, 0x10 `Area`, 0x11..0x13 `BoatPoint`/`BoatEntry`/`BoatExit`): each turns the parser's
// payload into the argument of one call and makes that call; the owner of the call (sim-ferry, sim-world, the water-vehicle circuit, the
// world's place handler, the object scripts, the grid) does the work. Only the translation is the interpreter's business, so only it is here;
// the calls are in game/world_script.cpp. Checked against recorded behavior.
namespace obl::script {

// `CircuitPoint <JetSki|PowerBoat> x y;` -> World::AddCircuitPoint(world, circuit, x, y); the parser's kind (0 / 1) is the circuit number.
struct CircuitCall {
    int32_t circuit, x, y;
    bool operator==(const CircuitCall&) const = default;
};

inline CircuitCall CircuitPointCall(const world_script::WorldScriptCircuitData& data)
{
    return {data.m_kind, data.m_x, data.m_y};
}

// `BoatEvent ...;` -> the FerryManager's record (the ferry manager copies it). The parser keeps keywords before counts, the record the
// other way round: its `GuestType[i]` is the parser's `m_random[i]` and its `GuestKeywordNumbers[i]` the parser's `m_counts[i]`. Each keyword
// is copied up to and including its terminator; the rest of the 64 bytes is not defined, the record built here is zero
// there (the comparison reads keywords as texts, see NormalizeBoatEvent).
inline ferry::Boat BoatEventRecord(const world_script::WorldScriptBoatEventData& data)
{
    ferry::Boat record{};
    record.GuestInfo = data.m_guestInfo;
    record.NumGuests = data.m_number;
    record.Time = data.m_hour;
    record.Day = data.m_day;
    record.TimeFlags = data.m_timeFlags;
    for (int i = 0; i < 20; ++i) {
        record.GuestType[i] = data.m_random[i];
        record.GuestKeywordNumbers[i] = data.m_counts[i];
        const size_t length = strnlen(data.m_keywords[i], sizeof(data.m_keywords[i]) - 1);
        std::memcpy(record.GuestKeyword[i], data.m_keywords[i], length + 1);
    }
    return record;
}

// What a recorded call keeps of a keyword's 64 bytes: the text and its terminator, then zeros.
inline void NormalizeBoatEvent(ferry::Boat& record)
{
    for (auto& keyword : record.GuestKeyword) {
        const size_t length = strnlen(keyword, sizeof(keyword) - 1);
        std::memset(keyword + length + 1, 0, sizeof(keyword) - length - 1);
    }
}

// `Disaster ...;` -> disaster::event_facade::AddEvent(manager, event) with the event by value. The fields are the parser's payload as it is; the
// target building name is copied into the event (the manager owns it), so the script's own buffer is not kept.
inline disaster::Event DisasterEventRecord(const world_script::WorldScriptDisasterData& data)
{
    disaster::Event event{};
    event.type = data.m_kind;
    std::memcpy(event.rect, data.m_rectangle, sizeof(event.rect));
    event.duration = data.m_duration;
    event.time = data.m_hour;
    event.day = static_cast<uint32_t>(data.m_day);
    event.scale = data.m_scale;
    event.disasterValue = data.m_value;
    event.beerType = data.m_beer;
    event.replacementBeer = data.m_replacementBeer;
    event.replacementStrength = data.m_strength;
    if (data.m_target) event.target = data.m_target;
    return event;
}

// `Weather ...;` -> weather::runtime::InsertEntry(weather, entry) with the entry by value: kind, hour, day and the 26 desire modifiers, the payload as it is.
inline weather::WeatherEntry WeatherEntryRecord(const world_script::WorldScriptWeatherData& data)
{
    weather::WeatherEntry entry{};
    entry.m_kind = data.m_kind;
    entry.m_hour = data.m_hour;
    entry.m_day = data.m_day;
    for (int i = 0; i < 26; ++i) entry.m_desireModifiers[i] = data.m_modifiers[i];
    return entry;
}

// `Path x y n <direction>;` and `Grass x y n <direction>;` paint a run of `n` tiles: the tile's flags get the paving (0x10000080) or grass (0x7000)
// bits, the tile is rebuilt, the next tile is the neighbour in the direction. Both are silenced by the world's silence byte and do nothing for n <= 0.
constexpr uint32_t kPathFlags = 0x10000080, kGrassFlags = 0x7000;

// `Create <Scenery|Building|Guest> <type> ...;` (0xc): world::PostPlaceCommand(g_MessageHandler, message, placement) per object. Silenced by the byte
// the world's silence byte, like Path. A building is placed once; scenery and guests `m_number` times (none for m_number <= 0). Scenery and guests whose x and y
// are both negative (`Random`) go to a random point of the map, redrawn until its tile is usable (IsCreateTile). Another class ends the game
// (FatalError, which the parser never lets through).
constexpr int32_t kCreateScenery = 1, kCreateBuilding = 2, kCreateGuest = 3;

// From the world to the world, type 0, the class in the data word; world::PostPlaceCommand stamps the time. The remaining
// words (extra, floatData) are not set here.
inline messages::Message CreateMessage(int32_t createClass)
{
    messages::Message message{};
    message.sender = -2;
    message.receiver = -2;
    message.data = createClass;
    return message;
}

inline bool IsRandomCreate(const world_script::WorldScriptCreateData& data)
{
    return data.m_x < 0 && data.m_y < 0;
}

// A random world point: x in [1, 3999], y in [1, 7999], one draw each (x first), every product exact in a double like a double, one rounding.
struct CreatePoint {
    float x, y;
};
constexpr float kCreateSpanX = 3998.0f, kCreateSpanY = 7998.0f;

template<class Draw> CreatePoint RandomCreatePoint(Draw draw)
{
    const double x = static_cast<double>(draw()) * static_cast<double>(kRandUnit) * kCreateSpanX + 1.0;
    const double y = static_cast<double>(draw()) * static_cast<double>(kRandUnit) * kCreateSpanY + 1.0;
    return {static_cast<float>(x), static_cast<float>(y)};
}

// The tile of a random point is usable when it has a land/paving bit (0x10000002) and not the 0x10 bit.
inline bool IsCreateTile(uint32_t flags)
{
    return (flags & 0x10000002u) != 0 && (flags & 0x10u) == 0;
}

// strcpy into the placement buffer (the terminator included); a text longer than its field runs on into the next one (no bounds check).
inline void CopyPlacementText(world_script::ScriptPlacement& placement, size_t offset, const char* text)
{
    if (!text) text = "";
    const size_t size = std::min(std::strlen(text) + 1, sizeof(placement) - offset);
    std::memcpy(reinterpret_cast<char*>(&placement) + offset, text, size);
}

// The placement apart from a random position (the caller sets x/y per object then): the class's own words, zero elsewhere.
inline world_script::ScriptPlacement CreatePlacement(const world_script::WorldScriptCreateData& data)
{
    world_script::ScriptPlacement placement{};
    CopyPlacementText(placement, offsetof(world_script::ScriptPlacement, m_name), data.m_type);
    switch (data.m_class) {
    case kCreateScenery: {
        CopyPlacementText(placement, offsetof(world_script::ScriptPlacement, m_sprite), data.m_sprite);
        placement.m_x = data.m_x;
        placement.m_y = data.m_y;
        placement.m_layer = 1 | (data.m_layer & 0xff) << 8;
        const int32_t minusTwo = -2;
        std::memcpy(&placement.m_prices[0], &minusTwo, sizeof(minusTwo));
        break;
    }
    case kCreateBuilding:
        placement.m_x = data.m_x;
        placement.m_y = data.m_y;
        placement.m_layer = data.m_layer;
        std::memcpy(placement.m_prices, data.m_prices, sizeof(placement.m_prices));
        placement.m_beer = data.m_beer;
        placement.m_beerPrice = data.m_beerPrice;
        placement.m_age = data.m_age;
        placement.m_broken = data.m_broken;
        break;
    case kCreateGuest: { // a world point: the given tile numbers as floats
        const float x = static_cast<float>(data.m_x), y = static_cast<float>(data.m_y);
        std::memcpy(&placement.m_x, &x, sizeof(x));
        std::memcpy(&placement.m_y, &y, sizeof(y));
        break;
    }
    default: break;
    }
    return placement;
}

// What a recorded placement of `createClass` keeps: the texts up to their terminators and the words the class writes, zero elsewhere (the
// buffer holds no defined data there).
inline world_script::ScriptPlacement NormalizePlacement(const world_script::ScriptPlacement& placement, int32_t createClass)
{
    world_script::ScriptPlacement kept{};
    auto text = [&](const char (&from)[32], char (&to)[32]) {
        std::memcpy(to, from, std::min(strnlen(from, sizeof(from)) + 1, sizeof(from)));
    };
    text(placement.m_name, kept.m_name);
    switch (createClass) {
    case kCreateScenery:
        text(placement.m_sprite, kept.m_sprite);
        kept.m_x = placement.m_x;
        kept.m_y = placement.m_y;
        kept.m_layer = placement.m_layer & 0xffff;
        kept.m_prices[0] = placement.m_prices[0];
        break;
    case kCreateBuilding:
        std::memcpy(&kept.m_x, &placement.m_x,
                offsetof(world_script::ScriptPlacement, m_broken) + 1 - offsetof(world_script::ScriptPlacement, m_x));
        break;
    case kCreateGuest:
        kept.m_x = placement.m_x;
        kept.m_y = placement.m_y;
        break;
    default: break;
    }
    return kept;
}

// `Area <Beach|Sea> n x1 y1 ... xn yn  4 x (x y);` (0x10): a GridArea (built with the n points copied, the four corner points, a last
// argument of 10) appended to the grid's beach chain (kind 1, Grid::AddBeachArea) or sea chain (any other kind, Grid::AddSeaArea).
constexpr int32_t kAreaBeach = 1;
constexpr int32_t kAreaLastArgument = 10;

// `BoatPoint x y [Unload|Destroy];` (0x11): Grid::AddBoatPoint(grid, x, y, z, flags). `BoatEntry`/`BoatExit x y n dx dy;` (0x12/0x13):
// Grid::SetBoatEntry/SetBoatExit(grid, x, y, z, n, dx, dy). The payloads are the arguments in order (the point by value).
// `LoadScript <logic> <path>;` (0xf): WorldScriptHost::AddScriptProgram(host, logic, path).

} // namespace obl::script
