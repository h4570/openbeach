// The generic functions of mod object scripts (mod_script_functions.h) over the game state; the pure checks are in mod_script_functions.cpp.
#include "game/objects/mod_script_functions.h"

#include "core/ascii_case.h"
#include "game/game/game_accessors.h"
#include "game/messages/message_handler.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_create_params.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/script_text.h"
#include "game/objects/script_value_convert.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"
#include "game/ui/map_view.h"
#include "game/world/world_create.h"
#include "game/world/world_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obl::mod_script {
namespace {

using object::Base;

constexpr int32_t kTypeText = 1;

double Number(const script::ScriptValue& value)
{
    return script_value::ToFloat(value.type, static_cast<uint32_t>(value.bits));
}

// The text of a register; null when the register holds no text.
const char* Text(const script::ScriptValue& value)
{
    return value.type == kTypeText ? script::text::Of(value.bits) : nullptr;
}

script::ScriptValue Int(int32_t value)
{
    return {2, value};
}

Base* Find(double id)
{
    if (!std::isfinite(id) || id < 0 || id > 2147483647.0) return nullptr;
    return object::object_manager::ObjectById(game::GetObjectManager(), static_cast<uint32_t>(id));
}

bool IsBuilding(const Base& object)
{
    return object.m_type == object::ObjectType::Building;
}

// The point an object is reached at: the entrance of a building that has one, else its position.
const float* ReachPoint(const Base& object)
{
    if (IsBuilding(object)) {
        const auto& building = static_cast<const object::Building&>(object);
        if (building.m_Entrance) return building.m_EntrancePoint;
    }
    return object.m_WorldXYZ;
}

void TileOf(const float* world, int tile[2])
{
    ui::MapView::WorldToTile(tile, world);
}

double Distance2(const Base& a, const Base& b)
{
    const double dx = static_cast<double>(a.m_WorldXYZ[0]) - b.m_WorldXYZ[0];
    const double dy = static_cast<double>(a.m_WorldXYZ[1]) - b.m_WorldXYZ[1];
    return dx * dx + dy * dy;
}

// The guests and staff around the object within `radius` tiles, without the object itself.
object::object_manager::ObjectList Around(const Base& self, int radius)
{
    int tile[2] = {};
    TileOf(self.m_WorldXYZ, tile);
    auto list = object::object_manager::GetObjectsNearTile(game::GetObjectManager(), tile[0], tile[1], radius);
    list.erase(std::remove(list.begin(), list.end(), const_cast<Base*>(&self)), list.end());
    return list;
}

void Post(int sender, int receiver, int type, int data, int flags)
{
    messages::Message message{};
    message.sender = sender;
    message.receiver = receiver;
    message.type = type;
    message.data = data;
    message.flags = flags;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

// The nearest building by its catalog key, or by the family of one of its commodities (Arg1 is then a family key).
std::string FindNearest(Base& self, Registers& r, bool byFamily)
{
    const char* wanted = Text(r.arg[0]);
    if (!wanted || !*wanted) return "Arg1 must be a text";
    auto* params = game::GetWorldParams();
    int family = -1;
    if (byFamily) {
        for (const auto& row : params->m_commodityFamilies)
            if (ascii::EqualsNoCase(row.m_key, wanted)) family = row.m_id;
        if (family < 0) return std::string("unknown commodity family \"") + wanted + "\"";
    }
    Base* best = nullptr;
    double bestDistance = 0;
    for (Base* object : game::GetObjectManager()->m_buildings) {
        const auto* definition = static_cast<const object::Building*>(object)->m_definition;
        if (!definition) continue;
        bool match = false;
        if (!byFamily) {
            match = ascii::EqualsNoCase(definition->m_key, wanted);
        } else {
            for (int slot = 0; slot < definition->m_NumCommodities && !match; ++slot) {
                const int commodity = definition->m_Commodity[slot];
                for (const auto& row : params->m_commodities)
                    if (row.m_id == commodity && row.m_family == family) match = true;
            }
        }
        if (!match) continue;
        const double distance = Distance2(self, *object);
        if (!best || distance < bestDistance) {
            best = object;
            bestDistance = distance;
        }
    }
    r.result = Int(best ? static_cast<int32_t>(best->m_id) : -1);
    return {};
}

std::string WalkToObject(Base& self, Registers& r)
{
    const Base* target = Find(Number(r.arg[0]));
    if (!target) return "Arg1 is not an object";
    int tile[2] = {};
    TileOf(ReachPoint(*target), tile);
    Post(static_cast<int>(self.m_id), static_cast<int>(self.m_id), messages::MT_MOVE2POINT, tile[0], tile[1]);
    return {};
}

const tables::DesireSlotDef* DesireSlot(const char* key)
{
    for (const auto& slot : game::GetWorldParams()->m_desireSlots)
        if (ascii::EqualsNoCase(slot.m_key, key)) return &slot;
    return nullptr;
}

std::string ChangeGuests(Base& self, Registers& r, bool desire)
{
    if (std::string error = CheckRadius(Number(r.arg[0])); !error.empty()) return "Arg1: " + error;
    const tables::DesireSlotDef* slot = nullptr;
    if (desire) {
        const char* key = Text(r.arg[1]);
        slot = key ? DesireSlot(key) : nullptr;
        if (!slot || slot->m_id < 0 || slot->m_id >= 26) return "Arg2 must be the key of a desire (for example \"SNACKS\")";
    }
    const double delta = Number(r.arg[desire ? 2 : 1]);
    if (std::string error = CheckUnitDelta(delta); !error.empty()) return std::string(desire ? "Arg3: " : "Arg2: ") + error;
    int32_t changed = 0;
    for (Base* object : Around(self, static_cast<int>(Number(r.arg[0])))) {
        if (object->m_type != object::ObjectType::Guest) continue;
        auto& guest = *static_cast<object::Guest*>(object);
        float& field = desire ? guest.m_desires[slot->m_id] : guest.m_happiness;
        field = AddClamped(field, static_cast<float>(delta));
        ++changed;
    }
    r.result = Int(changed);
    return {};
}

// Income goes to the cash and to the day's income (as attractions); a cost to the cash and the day's costs, like the other finance lines.
std::string Pay(Registers& r, bool income)
{
    const double amount = Number(r.arg[0]);
    if (std::string error = CheckMoney(amount); !error.empty()) return "Arg1: " + error;
    auto* world = game::GetWorld();
    const float money = static_cast<float>(amount);
    if (income) {
        world->m_player->m_cash += money;
        world->m_incomeToday += money;
        world->m_attractIncomeToday += money;
    } else {
        world->m_player->m_cash -= money;
        world->m_costsToday += money;
    }
    return {};
}

// A guest type a mod added carries its own script; only those can be created and removed by a script.
const tables::GuestDef* ModGuestType(const char* key)
{
    for (const auto& row : game::GetWorldParams()->m_guests)
        if (ascii::EqualsNoCase(row.m_key, key) && !row.m_Script.empty()) return &row;
    return nullptr;
}

std::string Create(Base& self, Registers& r)
{
    const char* key = Text(r.arg[0]);
    const tables::GuestDef* type = key ? ModGuestType(key) : nullptr;
    if (!type) return "Arg1 must be the key of a guest type with its own script";
    const Base* at = &self;
    if (r.arg[1].type != 2 || r.arg[1].bits != 0) {
        at = Find(Number(r.arg[1]));
        if (!at) return "Arg2 is not an object";
    }
    // Both limits are counted from the guests in the resort, so they need no saved state and a reload counts the same.
    const int today = game::GetWorld()->m_gameDay;
    int sameType = 0, arrivedToday = 0;
    for (const Base* object : game::GetObjectManager()->m_guests) {
        const auto& guest = *static_cast<const object::Guest*>(object);
        if (!guest.m_definition || guest.m_definition->m_Script.empty()) continue;
        sameType += guest.m_definition->m_key == type->m_key;
        arrivedToday += guest.m_DayArrived == today;
    }
    if (arrivedToday >= kMaxCreatedPerDay)
        return "the limit of " + std::to_string(kMaxCreatedPerDay) + " mod objects created a day is reached";
    if (sameType >= kMaxObjectsPerType)
        return "the limit of " + std::to_string(kMaxObjectsPerType) + " objects of the type \"" + type->m_key + "\" is reached";
    object::GuestCreateParams params{};
    std::strncpy(params.m_key, type->m_key.c_str(), sizeof(params.m_key) - 1);
    const float* point = ReachPoint(*at);
    for (int i = 0; i < 3; ++i) params.m_pos[i] = point[i];
    const Base* created = world::CreateObject(*game::GetWorld(), object::ObjectType::Guest, &params);
    r.result = Int(created ? static_cast<int32_t>(created->m_id) : -1);
    return {};
}

std::string Remove(Registers& r)
{
    Base* target = Find(Number(r.arg[0]));
    if (!target || target->m_type != object::ObjectType::Guest) return "Arg1 is not a guest";
    const auto* definition = static_cast<const object::Guest*>(target)->m_definition;
    if (!definition || definition->m_Script.empty()) return "Arg1 is not an object of a mod type";
    Post(static_cast<int>(target->m_id), messages::kMessageReceiverWorld, messages::MT_KILLOBJECT, static_cast<int>(target->m_id), 0);
    return {};
}

std::string SendNearby(Base& self, Registers& r)
{
    if (std::string error = CheckRadius(Number(r.arg[0])); !error.empty()) return "Arg1: " + error;
    const double type = Number(r.arg[1]);
    if (!std::isfinite(type) || type < 0 || type > 100000) return "Arg2 must be a message (MT_...)";
    const int data = script_value::ToInt(r.arg[2].type, static_cast<uint32_t>(r.arg[2].bits));
    int32_t sent = 0;
    for (Base* object : Around(self, static_cast<int>(Number(r.arg[0])))) {
        Post(static_cast<int>(self.m_id), static_cast<int>(object->m_id), static_cast<int>(type), data, 0);
        ++sent;
    }
    r.result = Int(sent);
    return {};
}

} // namespace

std::string Call(Base& self, int32_t id, Registers& registers)
{
    switch (id) {
    case kFindNearestBuilding: return FindNearest(self, registers, false);
    case kFindNearestCommodity: return FindNearest(self, registers, true);
    case kWalkToObject: return WalkToObject(self, registers);
    case kChangeGuestsHappiness: return ChangeGuests(self, registers, false);
    case kChangeGuestsDesire: return ChangeGuests(self, registers, true);
    case kPayMoney: return Pay(registers, true);
    case kChargeMoney: return Pay(registers, false);
    case kCreateObject: return Create(self, registers);
    case kRemoveObject: return Remove(registers);
    case kSendToNearby: return SendNearby(self, registers);
    default: return "unknown function";
    }
}

} // namespace obl::mod_script
