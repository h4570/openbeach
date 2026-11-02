#pragma once

#include "game/world/world_save_json.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>

// The obl.save sections after `world` written from the live state (docs/architecture/data-formats/save-switch.md), one
// writer per section. Keys and value rules are those of the converter's mapping tables (importer/src/readers/save/save_writer.cpp); while the
// game still maps its binary records, WriteOblSave checks that both give the same section.
namespace obl::player {
struct Player;
} // namespace obl::player
namespace obl::feedback {
struct Manager;
} // namespace obl::feedback
namespace obl::repair {
struct Manager;
} // namespace obl::repair
namespace obl::cleaning {
struct Manager;
} // namespace obl::cleaning
namespace obl::construction {
struct Manager;
} // namespace obl::construction
namespace obl::ferry {
struct Manager;
} // namespace obl::ferry
namespace obl::disaster {
struct Manager;
} // namespace obl::disaster
namespace obl::weather {
struct Weather;
} // namespace obl::weather
namespace obl::messages {
struct Handler;
} // namespace obl::messages
namespace obl::map {
struct Grid;
} // namespace obl::map
namespace obl::ui {
struct MapView;
} // namespace obl::ui

namespace obl::world_save {

// `player` (kPlayerFields): the name in UTF-8 up to its first NUL and the cash.
void WritePlayerJson(const player::Player& player, nlohmann::ordered_json& out);

// `view` (kMapViewFields, the "ZEngine" block): camera, position and velocity pairs, the burn queue, the scripted move. False
// with `error` naming the field on a value obl.save cannot hold.
bool WriteViewJson(const ui::MapView& view, nlohmann::ordered_json& out, std::string& error);

// The managers below return false with `error` naming the field on a value obl.save cannot hold (a bool above 1).
// `managers.messages` (kMessageFields): the delayed messages in their queue order.
bool WriteMessagesJson(const messages::Handler& handler, nlohmann::ordered_json& out, std::string& error);
// `managers.repair` (kRepairFields): the building, scenery and active jobs and the counters.
bool WriteRepairJson(const repair::Manager& manager, nlohmann::ordered_json& out, std::string& error);
// `managers.cleaning` (kCleaningFields): the waiting and active jobs and the counters.
bool WriteCleaningJson(const cleaning::Manager& manager, nlohmann::ordered_json& out, std::string& error);
// `managers.construction` (kConstructionFields): the jobs with hoardings and work points, the used builder entries (a count
// above the table is refused), then each job's work point owners, which the binary record drops.
bool WriteConstructionJson(const construction::Manager& manager, nlohmann::ordered_json& out, std::string& error);
// `managers.commodities` (not in the binary records): the complaint counter of every commodity family.
void WriteCommoditiesJson(std::span<const uint32_t> complaints, nlohmann::ordered_json& out);
// `managers.beer` (not in the binary records): the beers served by every bar since the last random effect (BeerRound); a load kept
// the counter of the session before it, so a reloaded world drew its next random beer at another time (s1_beer_counter_save).
void WriteBeerJson(int32_t served, nlohmann::ordered_json& out);
// `managers.ferry` (kFerryFields): the schedule and its cursor index, the performance boats (keywords in the pack's code page),
// the departures.
bool WriteFerryJson(const ferry::Manager& manager, const JsonContext& context, nlohmann::ordered_json& out, std::string& error);
// `managers.events` (kEventFields): the disaster schedule's cursor and the running events.
bool WriteEventsJson(const disaster::Manager& manager, nlohmann::ordered_json& out, std::string& error);
// `managers.weather` (kWeatherFields).
bool WriteWeatherJson(const weather::Weather& weather, nlohmann::ordered_json& out, std::string& error);
// `managers.objects` (kObjectManagerFields): the next object id and the global keyline; the counts and the id list follow from
// the objects.
bool WriteObjectManagerJson(uint32_t nextId, uint8_t keyline, nlohmann::ordered_json& out, std::string& error);

// `map` (Grid() of the converter): the square cell count, the island size, the beach then sea areas' open flags, the cell flags
// and heights in row order. False on a grid that is not square (the binary save cannot say its size).
bool WriteMapJson(const map::Grid& grid, nlohmann::ordered_json& out, std::string& error);

// `managers.feedback` (kFeedbackFields): the pending and history notices with their texts (wide, or a text key in the pack's
// code page), the staff message masks and the suppressed message types.
bool WriteFeedbackJson(const feedback::Manager& manager, const JsonContext& context, nlohmann::ordered_json& out, std::string& error);

} // namespace obl::world_save
