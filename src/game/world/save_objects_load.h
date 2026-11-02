#pragma once

#include "game/world/world_load_json.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The `objects` entries of obl.save read into objects the manager has constructed (docs/architecture/data-formats/save-switch.md,
// step 3b): the inverse of WriteObjectJson over the same VisitFields tables and keys, layer by layer in the order of the load
// chain (class fields, the moving or static layer, the base with its sprite). A missing key reads as the bridge's default (0, false).
namespace obl::map {
struct Area;
} // namespace obl::map

namespace obl::object_manager {
struct SavedHeader;
} // namespace obl::object_manager

namespace obl::object {
struct Base;
struct Building;
struct Scenery;
} // namespace obl::object

namespace obl::world_save {

// A building's show schedule as the building load reads it (the record order; the catalog event keys already as ids, -1 none).
struct BuildingEventsJson {
    struct Slot {
        int32_t event = -1;
        uint32_t actorCount = 0;
        std::vector<int32_t> actors;
        uint32_t guestCount = 0;
        std::vector<int32_t> guests;
        float start = 0.0f;
        float duration = 0.0f;
        float price = 0.0f;
    };
    int32_t owner = 0;
    uint8_t active = 0;
    Slot slots[3];
};

// A building's guest queue as the building load reads it (the tail and points are dropped fields, applied after the load).
struct BuildingQueueJson {
    int32_t owner = 0;
    int32_t maxLength = 0;
    float servePoint[2] = {};
    float direction[2] = {};
    std::vector<int32_t> guests;
    std::vector<int32_t> commodities;
};

// One entry of a building's thought list: the thinking object and the text.
struct BuildingThoughtJson {
    int32_t object = 0;
    std::u16string text;
};

// What the load does besides storing fields, done by the caller at the same point of the walk.
struct ObjectLoadCallbacks {
    // The three position floats (bits) handed to the object's virtual SetPosition (grid cells, terrain height).
    std::function<void(object::Base&, const uint32_t xyz[3])> setPosition;
    // A sprite made from the bank by name, for an object that has none yet.
    std::function<void*(const char* name)> createSprite;
    // A ferry's or boat's circuit from the world (the vehicle load's circuit restore): the point count and the points.
    std::function<uint32_t(int32_t vehicleType)> circuitPointCount;
    std::function<void*(int32_t vehicleType)> circuitPoints;
    // The scenery load's steps around the base: the draw routine (before it) and the footprint cells (after it).
    std::function<void(object::Scenery&)> sceneryDraw;
    std::function<void(object::Scenery&)> sceneryFootprint;
    // A staff class-wide count read from a member's record (staff_serial.h CounterValue; the pre-queue check leaves the globals).
    std::function<void(const char* key, int32_t value)> staffCounter;
    // A lifeguard's beach area by index (Grid::AreaAt over the beach areas; past the list none).
    std::function<const map::Area*(int32_t index)> beachArea;
    // The building load's allocations: the schedule and the queue (only when the entry has them; constructed for the
    // building, then filled) and the thought list (cleared, then the entries appended; always called).
    std::function<void(object::Building&, const BuildingEventsJson&)> buildingEvents;
    std::function<void(object::Building&, const BuildingQueueJson&)> buildingQueue;
    std::function<void(object::Building&, const std::vector<BuildingThoughtJson>&)> buildingThoughts;
};

// Whether ReadObjectJson reads the class of this object type yet (the families done so far).
bool HasObjectReader(uint8_t type);

// Fills `object` (constructed by type and name, indexed by id) from its `objects` entry. False with `error` naming the key on a value
// the object cannot hold or a type without a reader.
bool ReadObjectJson(const nlohmann::json& in, const ReadContext& context, const ObjectLoadCallbacks& callbacks, object::Base& object,
        std::string& error);

// The object manager's object header from an `objects` entry: the type byte it constructs by (the entry's `kind`; staff by the
// group object it carries, performers by their `type` key) and the creation name (`created_as`, else `type`) in
// the pack's code page. False with `error` on an unknown kind or a name outside the code page.
bool ReadObjectHeaderJson(const nlohmann::json& in, const ReadContext& context, uint8_t& type, std::string& name, std::string& error);

// The object manager's own block from `managers.objects` and the `objects` list: the counts and id slots per list in the
// document's order (buildings, guests, others; another order or a full list is refused), `next_id` (missing: one past the
// largest id) and `keyline`.
bool ReadObjectManagerJson(const nlohmann::json& section, const nlohmann::json& objects, const ReadContext& context,
        object_manager::SavedHeader& out, std::string& error);

} // namespace obl::world_save
