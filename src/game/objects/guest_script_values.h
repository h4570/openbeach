#pragma once

#include "core/ascii_case.h"
#include "game/tables/building_behavior.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "script_types.h"
#include "game/objects/script_program_exec.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// The script variables of a guest: `GetValue` and `SetValue`
// (guest script variables, docs/architecture/objects.md). Both are one `switch` over the variable number of the script (`Get`/`Set`/`If`, the
// numbers are the script API of script_program_api.h); a number the guest does not know goes to the base class (the human's
// GetValue / SetValue -> Mover), so the shared and the staff variables stay where they are.
// Pure logic over object::Guest: everything that reaches the game (objects by id, prices, the grid, the sprite bank) goes through an `Env`
// that the game side maps onto the game's services (src/game/objects/guest_script.cpp) and the unit test onto a fake.
//
// GetEnv:
//   object::Base* Object(int32_t id)                         object by id (a null result is not checked: ids from scripts are valid)
//   float SlotPrice(Entity*, slot) / SlotReferencePrice(...)   the building's slot price / slot reference price
//   const char* CommodityName(int32_t) / int32_t CommodityFamilyOf(int32_t) / const char* FamilyKey(int32_t family)
//   int32_t ActiveEventId(Events*)                 the active event id
//   int32_t SeaArea(float x, float y, float z)                     the sea area at the point
//   uint32_t CellFlags(float x, float y, float z)                  reads flags from the cell containing the point
//   bool SlotUsable(int32_t building, int32_t slot)                the commodity slot usability test
// bool NeighboursClear(Guest&) AreNeighbourTilesFree, stays the game's
//   object::Base* const* FindBuildings(BuildingBehavior, uint32_t& count)   object_manager::FindBuildings (list order)
//   float GameDeltaTime()                                          World::m_gameDeltaTime
//   ScriptValue Base(Guest&, int32_t key, intptr_t data)      the human's GetValue
// SetEnv adds to the ones above that it needs (Object):
//   int32_t FloatToIntWrap(int32_t floatBits) / int Rand()                   truncation / the game's rand()
//   void SetBinFill(object::Base*, int32_t)                  updating bin fill
//   void ReleaseSprite(void*) / void* CreateSprite(const char*)    releases or creates sprites through the global bank
//   void SetAnim(void* sprite, const char* name, float startTime, float* position)   starting the named sprite animation
//   float GameTime()                                               World::m_gameTime
//   void Base(Guest&, int32_t key, intptr_t data, int32_t type, int32_t bits)    the human's SetValue
namespace obl::guest_script {

using object::Guest;
using script::ScriptValue;
namespace ex = script_program::exec;

constexpr int32_t kByte = ex::kTypeByte, kText = ex::kTypeText, kInt = ex::kTypeInt, kFloat = ex::kTypeFloat;

constexpr int32_t kKeyDirection = 0x38, kKeyAttachedSprite = 0x39, kKeyAttachedAnim = 0x3a, kKeyAttachedDir = 0x3b;

// Desire variables by script number: the numbers are not in field order (the script's own list), the index is the position in
// Guest::m_desires. -1 = not a desire. Numbers 0x0b..0x24 (GetValue) and the subset of them SetValue knows.
constexpr int kDesireOfKey[0x25] = {
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1, // 0x00..0x0a
        16,
        12,
        13, // 0x0b..0x0d
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11, // 0x0e..0x19
        14,
        15, // 0x1a, 0x1b
        17,
        18,
        19,
        20,
        21,
        22,
        23,
        24,
        25, // 0x1c..0x24
};
// Desire numbers that `Set` accepts (the others fall through to the base class).
inline bool SetsDesire(int32_t key)
{
    switch (key) {
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x12:
    case 0x14:
    case 0x15:
    case 0x1a:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x22:
    case 0x23: return true;
    default: return false;
    }
}

// The bits of a text value: its script::text id.
inline int32_t TextBits(const char* text)
{
    return script::text::Id(text);
}

// The opinion bands of the "worst/best opinion" variables (0x43, 0x45): compared with 0.5, 0.9, 0.1 and 0.5 again. Thresholds are tested so that
// an unordered pair (a NaN) counts as "less" in every test, so it falls through to 0.
inline int32_t OpinionBand(float value)
{
    if (value > 0.5f) return 3;
    if (!std::isnan(value) && value >= 0.9f) return 4; // never taken for a number: 0.5 already answered every value above it
    if (!(value > 0.1f)) return 0;
    return value < 0.5f ? 1 : 2;
}

// The game's float-to-int conversion: truncation to a 64-bit integer, the low 32 bits returned. So a value past the int range wraps (4294967296
// gives 0, -2147483904 gives 0xffffff80 wrapped the way the low word of the 64-bit number reads) and NaN or a value past the 64-bit range gives the
// integer indefinite 0x8000000000000000, i.e. 0.
inline int32_t Truncate(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// The numbers each dispatcher answers itself; every other number is the base class's (the human's GetValue / SetValue).
// The unit test checks that the switches below agree with it.
inline bool HandlesGet(int32_t key)
{
    if (key < 0 || key > 0x68) return false;
    switch (key) {
    case 0x2a:
    case 0x2f:
    case 0x30:
    case 0x31:
    case 0x36:
    case 0x37:
    case 0x3a:
    case 0x3b:
    case 0x3e:
    case 0x3f:
    case 0x47:
    case 0x4a:
    case 0x54:
    case 0x58:
    case 0x59: return false;
    default: return true;
    }
}
inline bool HandlesSet(int32_t key)
{
    if (SetsDesire(key)) return true;
    switch (key) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x07:
    case 0x09:
    case 0x0a:
    case 0x26:
    case 0x28:
    case 0x29:
    case 0x2c:
    case 0x2e:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3e:
    case 0x3f:
    case 0x40:
    case 0x41:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x51:
    case 0x53:
    case 0x55:
    case 0x56:
    case 0x5a:
    case 0x5c:
    case 0x5e:
    case 0x5f:
    case 0x64:
    case 0x69: return true;
    default: return false;
    }
}
// Setting these reaches outside the guest (the sprite bank, a bin).
inline bool SetsExternal(int32_t key)
{
    return key == kKeyAttachedSprite || key == kKeyAttachedAnim || key == kKeyAttachedDir || key == 0x51;
}

template<class Env> ScriptValue GetValue(Guest& g, int32_t key, intptr_t data, Env& env)
{
    const auto asInt = [](int32_t v) { return ScriptValue{kInt, v}; };
    const auto asFloat = [](float v) { return ScriptValue{kFloat, ex::FloatToBits(v)}; };
    const auto asByte = [](uint8_t v) {
        return ScriptValue{kByte, v};
    }; // the field's byte as it is: only the low byte of `bits` is defined
    const auto asText = [](const char* v) { return ScriptValue{kText, TextBits(v)}; };
    const auto* message = static_cast<const messages::Message*>(g.m_currentMessage);
    const auto& position = g.m_WorldXYZ;

    if (key >= 0x0b && key <= 0x24) return asFloat(g.m_desires[kDesireOfKey[key]]);
    switch (key) {
    case 0x00: return asInt(message->sender);
    case 0x01: return asInt(g.m_VehicleID);
    case 0x02: return asFloat(g.m_opinion[0]);
    case 0x03: return asInt(g.m_walkpoint);
    case 0x04: return asText(g.m_WalkAnimation);
    case 0x05: return asInt(message->data);
    case 0x06: return asFloat(message->floatData);
    case 0x07: return asByte(g.m_Fidget);
    case 0x08: {
        auto* building = static_cast<object::Building*>(env.Object(g.m_DestObjectID));
        return asText(env.FamilyKey(env.CommodityFamilyOf(building->m_definition->m_Commodity[g.m_desired_commod])));
    }
    case 0x09: return asFloat(g.m_drunk);
    case 0x0a: return asFloat(g.m_cash);
    case 0x25: return asFloat(env.SlotPrice(env.Object(g.m_DestObjectID), g.m_desired_commod));
    case 0x26: return asFloat(static_cast<float>(static_cast<uint32_t>(g.m_oxygen)));
    case 0x27: {
        auto* building = static_cast<object::Building*>(env.Object(g.m_DestObjectID));
        return asText(env.CommodityName(building->m_definition->m_Commodity[g.m_desired_commod]));
    }
    case 0x28: return asInt(g.m_desired_commod);
    case 0x29: return asFloat(g.m_opinion[3]);
    case 0x2b: return asInt(message->flags);
    case 0x2c: return asFloat(g.m_happiness);
    case 0x2d: {
        const float reference = env.SlotReferencePrice(env.Object(g.m_DestObjectID), g.m_desired_commod);
        return asFloat(static_cast<float>(static_cast<double>(reference) * static_cast<double>(g.m_Tolerance[7])));
    }
    case 0x2e: return asByte(g.m_activity);
    case 0x32: return asByte(g.m_StopAction);
    // The walk point index is not checked (a script only sets 0..2).
    case 0x33: return asFloat(*(&g.m_walkpoints[0][0] + g.m_walkpoint * 3));
    case 0x34: return asFloat(*(&g.m_walkpoints[0][0] + g.m_walkpoint * 3 + 1));
    case 0x35: return asInt(g.m_EventID);
    case 0x38: return asInt(static_cast<ui::SpriteInstance*>(g.m_sprite)->m_direction);
    case 0x39: {
        const auto* attached = static_cast<const ui::SpriteInstance*>(g.m_attachedSprite);
        return asText(attached ? attached->m_name : "NONE");
    }
    case 0x3c: return asInt(g.m_RepeatAction);
    case 0x3d: return asText(g.m_CurrAnimation);
    case 0x40: return asInt(g.m_ActionState);
    case 0x41: return asInt(g.m_FightPerson);
    case 0x42: {
        float lowest = 2.0f;
        int32_t index = 7;
        for (int32_t i = 0; i < 7; ++i) {
            if (g.m_opinion[i] < lowest) {
                lowest = g.m_opinion[i];
                index = i;
            }
        }
        return asInt(index);
    }
    case 0x43: {
        float lowest = 2.0f;
        for (float opinion : g.m_opinion)
            if (opinion < lowest) lowest = opinion;
        return asInt(OpinionBand(lowest));
    }
    case 0x44: {
        float highest = -1.0f;
        int32_t index = 7;
        for (int32_t i = 0; i < 7; ++i) {
            if (!(highest >= g.m_opinion[i])) { // a NaN opinion replaces the maximum (unordered counts as "less")
                highest = g.m_opinion[i];
                index = i;
            }
        }
        return asInt(index);
    }
    case 0x45: {
        float highest = -1.0f;
        for (float opinion : g.m_opinion)
            if (!(highest >= opinion)) highest = opinion;
        return asInt(OpinionBand(highest));
    }
    case 0x46: return asInt(g.m_ActivityPerson);
    case 0x48: return asByte(g.m_justLittered);
    case 0x49: {
        auto* other = env.Object(g.m_ActivityPerson);
        const char* name = static_cast<const ui::SpriteInstance*>(other->m_sprite)->m_name;
        return asByte(ascii::EqualsNoCase(name, "SHAZ") || ascii::EqualsNoCase(name, "FAT_BLOKE"));
    }
    case 0x4b: {
        uint32_t count = 0;
        object::Base* const* found = env.FindBuildings(BuildingBehavior::ShipStop, count);
        if (count == 0) found = env.FindBuildings(BuildingBehavior::BusStop, count);
        return asInt(static_cast<int32_t>((*found)->m_id));
    }
    case 0x4c: return asInt(Truncate(static_cast<double>(g.m_definition->m_Litter) * 10.0));
    case 0x4d: {
        uint32_t count = 0;
        object::Base* const* found = env.FindBuildings(BuildingBehavior::Hotel, count);
        return asInt(static_cast<int32_t>((*found)->m_id));
    }
    case 0x4e: return asInt(g.m_StayingAt);
    case 0x4f: return ScriptValue{kText, message->data}; // a message that names a text carries its script::text id
    case 0x50: return asByte(g.m_definition->m_Gender == 0);
    case 0x51: {
        auto* other = env.Object(g.m_ActivityPerson);
        return asInt(other ? static_cast<object::Scenery*>(other)->m_NumItems : 0);
    }
    case 0x52: { // the lotion: one is used up per guest and day, the sun-protection desire is satisfied when the last one goes
        if (g.m_AmountLoition != 0 && g.m_AppliedLotion == 0) {
            --g.m_AmountLoition;
            g.m_AppliedLotion = 1;
            if (g.m_AmountLoition == 0) g.m_desires[22] = 1.0f;
            return asByte(true);
        }
        g.m_desires[22] = 1.0f;
        return asByte(false);
    }
    case 0x53: return asByte(g.m_AppliedLotion);
    case 0x55: return asFloat(g.m_foodIll);
    case 0x56: return asByte(g.m_imIll);
    case 0x57:
        return asFloat(static_cast<float>(static_cast<double>(env.GameDeltaTime()) * static_cast<double>(g.m_definition->m_IllDecay)));
    case 0x5a: return asByte(g.m_leavingEarly);
    case 0x5b: {
        uint32_t count = 0;
        object::Base* const* found = env.FindBuildings(BuildingBehavior::ComplaintsHut, count);
        return asInt(static_cast<int32_t>((*found)->m_id));
    }
    case 0x5c: return asByte(g.m_topless);
    case 0x5d: return asInt(message->extra);
    case 0x5e: return asInt(g.m_AmountLoition);
    case 0x5f: return asInt(g.m_NumTimesBlocked);
    case 0x60: {
        const uint32_t flags = env.CellFlags(position[0], position[1], position[2]);
        const bool blocked = (flags & 0x10000080u) != 0 || ((flags & 2u) != 0 && (flags & 0x10u) == 0);
        return asByte(!blocked);
    }
    case 0x61: {
        auto* building = static_cast<object::Building*>(env.Object(g.m_DestObjectID));
        return asInt(env.ActiveEventId(building->m_events));
    }
    case 0x62: {
        auto* other = env.Object(g.m_DestObjectID);
        return asInt(static_cast<ui::SpriteInstance*>(other->m_sprite)->m_direction * -2 + 7);
    }
    case 0x63: {
        // In state 0x0d (swimming out) the sea is looked up under the second walk point, otherwise under the guest.
        const float* at = g.m_state == 0x0d ? g.m_walkpoints[1] : position;
        return asInt(env.SeaArea(at[0], at[1], at[2]));
    }
    case 0x64: return asByte(g.m_missedPurchase);
    case 0x65: return asInt(static_cast<int32_t>(g.m_OffenceState));
    case 0x66: return asInt(0);
    case 0x67: return asByte(!env.SlotUsable(g.m_CurID, g.m_desired_commod));
    case 0x68: return asByte(env.NeighboursClear(g));
    default: return env.Base(g, key, data);
    }
}

// The direction a script sets on a sprite: kept below the number of directions of the animation that plays.
inline void SetSpriteDirection(ui::SpriteInstance& sprite, int32_t direction)
{
    const int32_t directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
    if (direction >= directions) direction = directions - 1;
    sprite.m_direction = direction;
}

template<class Env> void SetValue(Guest& g, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    const auto ToInt = [&] { return ex::ToInt(env, value); };
    const auto ToFloat = [&] { return ex::ToFloatWide(value); };
    const auto Store = [&](float& field) { field = static_cast<float>(ToFloat()); };
    const auto* text = ex::AsText(bits);
    const auto byte = static_cast<uint8_t>(bits);

    if (SetsDesire(key)) {
        float& field = g.m_desires[kDesireOfKey[key]];
        const double wide = ToFloat();
        field = static_cast<float>(wide);
        if (key == 0x1a && wide > 1.0) field = 1.0f; // the one desire the script cannot push above its cap
        return;
    }
    switch (key) {
    case 0x01: g.m_VehicleID = ToInt(); return;
    case 0x02: Store(g.m_opinion[0]); return;
    case 0x03: g.m_walkpoint = ToInt(); return;
    case 0x04: std::strcpy(g.m_WalkAnimation, text); return; // unbounded copy into the 40-byte field (script names are short)
    case 0x07: g.m_Fidget = byte; return;
    case 0x09: Store(g.m_drunk); return;
    case 0x0a: Store(g.m_cash); return;
    case 0x26: g.m_oxygen = ToInt(); return;
    case 0x28: g.m_desired_commod = ToInt(); return;
    case 0x29: Store(g.m_opinion[3]); return;
    case 0x2c: Store(g.m_happiness); return;
    case 0x2e: g.m_activity = byte; return;
    case 0x32: g.m_StopAction = byte; return;
    case 0x33: Store(*(&g.m_walkpoints[0][0] + g.m_walkpoint * 3)); return;
    case 0x34: Store(*(&g.m_walkpoints[0][0] + g.m_walkpoint * 3 + 1)); return;
    case 0x35: g.m_EventID = ToInt(); return;
    case 0x37: std::strcpy(g.m_StandAnimation, text); return;
    case 0x38: { // the facing of the guest's own sprite: eight directions, `(n + 8) % 8` keeps the C remainder for negative numbers
        const int32_t direction = (ToInt() + 8) % 8;
        SetSpriteDirection(*static_cast<ui::SpriteInstance*>(g.m_sprite), direction);
        return;
    }
    case 0x39: { // the sprite attached to the guest (a ball, an ice cream): `NONE` removes it, any other name replaces it
        const bool none = ascii::EqualsNoCase(text, "NONE");
        if (g.m_attachedSprite) {
            env.ReleaseSprite(g.m_attachedSprite);
            g.m_attachedSprite = nullptr;
        }
        if (!none) g.m_attachedSprite = env.CreateSprite(text);
        return;
    }
    case 0x3a: { // its animation: `Reverse_<name>` plays backwards (mode 1), `Clamp_<name>` stops on the last frame (mode 2)
        auto* attached = static_cast<ui::SpriteInstance*>(g.m_attachedSprite);
        if (!attached) return;
        const char* name = text;
        if (std::strstr(text, "Reverse_")) {
            attached->m_animType = 1;
            name = text + 8;
        } else if (std::strstr(text, "Clamp_")) {
            attached->m_animType = 2;
            name = text + 6;
        } else {
            attached->m_animType = 0;
        }
        env.SetAnim(attached, name, env.GameTime(), g.m_WorldXYZ);
        return;
    }
    case 0x3b: { // `MyDir`: the attached sprite faces where the guest does
        auto* attached = static_cast<ui::SpriteInstance*>(g.m_attachedSprite);
        if (ascii::EqualsNoCase(text, "MyDir") && attached)
            SetSpriteDirection(*attached, static_cast<ui::SpriteInstance*>(g.m_sprite)->m_direction);
        return;
    }
    case 0x3c: g.m_RepeatAction = ToInt(); return;
    case 0x3e: g.m_AttachedOffsetX = ToInt(); return;
    case 0x3f: g.m_AttachedOffsetY = ToInt(); return;
    case 0x40: g.m_ActionState = ToInt(); return;
    case 0x41: g.m_FightPerson = ToInt(); return;
    case 0x46: g.m_ActivityPerson = ToInt(); return;
    case 0x47: { // the walking speed `n` varied by up to a quarter of itself either way; the steering force follows at a third
        const int32_t spread = ToInt() / 4;
        if (spread == 0) return; // a speed under 4 is ignored (a script never sets one)
        const int32_t jitter = env.Rand() % spread - spread / 2;
        g.m_maxVel = static_cast<float>(ToFloat() + static_cast<double>(jitter));
        g.m_maxForce = static_cast<float>((ToFloat() + static_cast<double>(jitter)) * static_cast<double>(0.33333334f));
        return;
    }
    case 0x48: g.m_justLittered = byte; return;
    case 0x51: { // the bin the guest stands at: its fill level
        if (env.Object(g.m_ActivityPerson)) {
            const int32_t fill = ToInt();
            env.SetBinFill(env.Object(g.m_ActivityPerson), fill);
        }
        return;
    }
    case 0x53: g.m_AppliedLotion = byte; return;
    case 0x55: Store(g.m_foodIll); return;
    case 0x56: g.m_imIll = byte; return;
    case 0x5a: g.m_leavingEarly = byte; return;
    case 0x5c: g.m_topless = byte; return;
    case 0x5e: g.m_AmountLoition = ToInt(); return;
    case 0x5f: g.m_NumTimesBlocked = ToInt(); return;
    case 0x64: g.m_missedPurchase = byte; return;
    case 0x69: Store(g.m_sunBurnLevel); return;
    default: env.Base(g, key, data, type, bits); return;
    }
}

} // namespace obl::guest_script
