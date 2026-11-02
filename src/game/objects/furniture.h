#pragma once

#include "game/objects/script_types.h"

#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"
#include "game/messages/message_state.h"
#include "game/objects/object_table.h"

#include <cstdint>
#include <cstring>

// Furniture (docs/architecture/objects.md): everything of the class except ReceiveMessage and Draw. Pure logic; whatever touches the game goes
// through an `Env` that the game side maps onto the world, object and sprite services and the unit test onto a fake.
//
// Env:
//   Construct:        const tables::TableDef* Definition(const char* key)       table definition of the world parameters
//                     void* Allocate(uint32_t bytes)                            operator new
//                     void ResetWorldPosition(Furniture&)                     world position (0, 0, 0)
//                     SpriteInstance* CreateSprite(const char* name)            sprite from the sprite bank
//                     void StartAnim(SpriteInstance*, const char* anim, float startTime)   start an animation by name
//                     float WorldTime                                         current world time
//   Destruct:         void ReleaseSprite(SpriteInstance*)                       return the sprite to the bank
//                     void Free(void*)                                          operator delete
//                     object::Base* Object(int32_t id)                          object by id (null = no such object)
//   SeatGuest:        void SetGuestActionState(int32_t guest, int32_t value)    the guest's action state
//                     void ClearDelayed(int32_t receiver)                       drop the receiver's delayed messages
//                     void Post(const messages::Message&)                       post a message (also OnEnterState)
//   AttachToBuilding: void WaiterSpawnPoint(int32_t building, float out[2])     x and y of the building's last walk point
//                     void PlaceWaiter(const messages::Message&, const object::StaffCreateParams&)   post the place command
//   Update:           bool BaseUpdate(Furniture&)                             static object update
//   Update/SetVariable/GetVariableText:  void Fatal(const char* format, const char* argument)   fatal error
//   GetValue:         const script::ScriptValue* BaseGetValue(Furniture&, script::ScriptValue* scratch, int32_t kind, intptr_t data)   base value getter
//   SetValue:         uint32_t BaseSetValue(Furniture&, int32_t kind, intptr_t data, int32_t sourceKind, int32_t sourceData)   base value setter
namespace obl::table {

using messages::Message;
using object::Furniture;
using object::TableChair;
using ui::SpriteInstance;

constexpr int32_t kFreeChair = -1;
constexpr int32_t kPlaceWaiter = 0x13; // Message::data of the place command: class 19, SnootyStaff
constexpr int32_t kGuestStateSitAtTable = 0x2e; // the state the seated guest is sent to
constexpr char kTestVariable[] = "TABLE_TEST_VAR";

// The inlined setter of SpriteInstance::m_direction: a direction the current animation does not have becomes its last one.
inline void SetDirection(SpriteInstance& sprite, int32_t direction)
{
    const int32_t count = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
    if (direction >= count) direction = count - 1;
    sprite.m_direction = direction;
}

// The first animation of the chair sprite tells whether the chairs have eight directions (otherwise the chair directions of the definition are halved).
inline bool HasEightDirections(const SpriteInstance& sprite)
{
    return sprite.m_sprite->m_anims[0]->m_directions == 8;
}

// Construct (Furniture's constructor, after Prop's; the table key). Not written (they keep the zero of their member
// initializers): m_TestVar, the unnamed word after it, m_WaiterPos, m_WaiterID, m_ownerId and, without chairs or plates, the plate pointers.
template<class Env> Furniture* Construct(Furniture& table, const char* name, Env& env)
{
    const tables::TableDef* definition = env.Definition(name);
    table.m_definition = definition;
    table.m_chairs = static_cast<TableChair*>(env.Allocate(definition->m_NumChairs * sizeof(TableChair)));
    for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
        TableChair& chair = table.m_chairs[i];
        chair.m_ChairSeated = 0;
        chair.m_waitingForWaiter = 0;
        chair.m_ChairOccupier = kFreeChair;
        chair.m_ChairDir = definition->m_chairDirections[i];
    }
    env.ResetWorldPosition(table);
    table.m_sprite = env.CreateSprite(definition->m_TableSprite.c_str());
    if (ascii::EqualsNoCase(definition->m_ChairSprite, "NONE")) {
        table.m_chairSprites = nullptr;
    } else {
        table.m_chairSprites = static_cast<SpriteInstance**>(env.Allocate(definition->m_NumChairs * sizeof(SpriteInstance*)));
        if (definition->m_hasPlates) {
            table.m_plateSprites = static_cast<SpriteInstance**>(env.Allocate(definition->m_NumChairs * sizeof(SpriteInstance*)));
            table.m_pbDrawPlate = static_cast<uint8_t*>(env.Allocate(definition->m_NumChairs));
        }
        for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
            SpriteInstance* chair = env.CreateSprite(definition->m_ChairSprite.c_str());
            table.m_chairSprites[i] = chair;
            const int32_t direction = definition->m_chairDirections[i];
            SetDirection(*chair, HasEightDirections(*chair) ? direction : direction / 2);
            if (definition->m_hasPlates) {
                SpriteInstance* plate = env.CreateSprite("Waiter");
                table.m_plateSprites[i] = plate;
                env.StartAnim(plate, "d_Static_plate", env.WorldTime());
                SetDirection(*plate, direction);
                table.m_pbDrawPlate[i] = 0;
            }
        }
    }
    table.m_rotation = 0;
    table.m_GuestsRemoved = 0;
    table.m_GuestsAdded = 0;
    table.m_serveTable = definition->m_serveTable;
    table.m_WaiterServing = -1;
    table.m_drawWaiter = 0;
    table.m_waiterReady = 1;
    return &table;
}

// Furniture's destructor body, before Prop's and the base object's. The chair sprites and their array, the chairs, then with plates the plate
// sprites and their array (m_pbDrawPlate is never freed) and the waiter: it goes invisible and is marked for deletion. A table with plates
// needs real chair sprites ("NONE" would leave the plate pointer unset); the data has no such table.
template<class Env> void Destruct(Furniture& table, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    if (table.m_chairSprites) {
        for (uint32_t i = 0; i < definition->m_NumChairs; ++i) env.ReleaseSprite(table.m_chairSprites[i]);
        env.Free(table.m_chairSprites);
    }
    if (table.m_chairs) env.Free(table.m_chairs);
    if (definition->m_hasPlates) {
        for (uint32_t i = 0; i < definition->m_NumChairs; ++i) env.ReleaseSprite(table.m_plateSprites[i]);
        env.Free(table.m_plateSprites);
        if (object::Base* waiter = env.Object(table.m_WaiterID)) {
            waiter->m_visible = 0;
            waiter->m_waitingForDeath = 1;
        }
    }
}

namespace detail {

inline Message StateChange(const Furniture& table, int32_t receiver, int32_t state)
{
    Message message{};
    message.sender = static_cast<int32_t>(table.m_id);
    message.receiver = receiver;
    message.type = messages::MT_STATECHANGE;
    message.data = state;
    return message;
}

// Takes the first free chair for the guest; false when every chair is taken.
inline bool TakeFreeChair(Furniture& table, int32_t guest)
{
    const uint32_t count = table.m_definition->m_NumChairs;
    for (uint32_t i = 0; i < count; ++i) {
        TableChair& chair = table.m_chairs[i];
        if (chair.m_ChairOccupier != kFreeChair) continue;
        ++table.m_GuestsAdded;
        chair.m_ChairOccupier = guest;
        chair.m_ChairSeated = 0;
        chair.m_position = 1;
        return true;
    }
    return false;
}

} // namespace detail

// SeatGuest (member call: the guest id; returns whether the guest got a chair). An ordinary table gives the guest the first free chair and
// sends it to the sit-at-table state. A serve table (the definition's SERVE_TABLE) first looks for the guest among its chairs: a guest who
// already sits there gets action state 2, the state change and two MT_DO_NEXT_ACTION (the same message posted twice); a new guest also loses
// its delayed messages. The unused tail of each message is zero.
template<class Env> bool SeatGuest(Furniture& table, int32_t guest, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    if (!definition->m_serveTable) {
        if (!detail::TakeFreeChair(table, guest)) return false;
        env.SetGuestActionState(guest, 0);
        env.Post(detail::StateChange(table, guest, kGuestStateSitAtTable));
        return true;
    }
    for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
        if (table.m_chairs[i].m_ChairOccupier != guest) continue;
        env.SetGuestActionState(guest, 2);
        env.Post(detail::StateChange(table, guest, kGuestStateSitAtTable));
        Message next{};
        next.sender = static_cast<int32_t>(table.m_id);
        next.receiver = guest;
        next.type = messages::MT_DO_NEXT_ACTION;
        next.extra = kGuestStateSitAtTable;
        env.Post(next);
        env.Post(next);
        return true;
    }
    if (!detail::TakeFreeChair(table, guest)) return false;
    env.ClearDelayed(guest);
    env.SetGuestActionState(guest, 0);
    env.Post(detail::StateChange(table, guest, kGuestStateSitAtTable));
    return true;
}

// AttachToBuilding (member call: the building id; table creation calls it). Remembers the owner; a table with plates asks the
// world for its waiter at the building's last walk point. The waiter key copy is bounded and the payload's z and the
// tail of the message are zeroed.
template<class Env> void AttachToBuilding(Furniture& table, int32_t building, Env& env)
{
    table.m_ownerId = building;
    const tables::TableDef* definition = table.m_definition;
    if (!definition->m_hasPlates) return;
    Message command{};
    command.sender = -2;
    command.receiver = -2;
    command.data = kPlaceWaiter;
    object::StaffCreateParams waiter{};
    std::strncpy(waiter.m_key, definition->m_waiterSprite.c_str(), sizeof(waiter.m_key) - 1);
    env.WaiterSpawnPoint(building, waiter.m_pos);
    waiter.m_homeId = static_cast<int32_t>(table.m_id);
    env.PlaceWaiter(command, waiter);
}

// OnEnterState (OnEnterState; always true). Entering state 1 tells the owner that the table exists and sends the table itself
// a state change with data 0.
template<class Env> bool OnEnterState(Furniture& table, uint32_t state, Env& env)
{
    if (state != 1) return true;
    Message created{};
    created.sender = static_cast<int32_t>(table.m_id);
    created.receiver = table.m_ownerId;
    created.type = messages::MT_TABLE_CREATED;
    env.Post(created);
    env.Post(detail::StateChange(table, static_cast<int32_t>(table.m_id), 0));
    return true;
}

// Update (no arguments; always true).
template<class Env> bool Update(Furniture& table, Env& env)
{
    if (!env.BaseUpdate(table)) env.Fatal("Update of furniture \"%s\" failed", table.m_Name);
    return true;
}

// SetVariable ( key, pointer to the value). The key is compared byte for byte.
template<class Env> void SetVariable(Furniture& table, const char* key, const void* value, Env& env)
{
    if (std::strcmp(key, kTestVariable) != 0) {
        env.Fatal("A script set unrecognized furniture variable \"%s\"", key);
        return;
    }
    std::memcpy(&table.m_TestVar, value, sizeof(table.m_TestVar));
}

// What swprintf(out, L"%d", value) writes, terminator included.
inline void FormatDecimal(Char16* out, int32_t value)
{
    Char16 digits[12];
    int count = 0;
    uint32_t magnitude = value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
    do {
        digits[count++] = static_cast<Char16>(u'0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) *out++ = u'-';
    while (count) *out++ = digits[--count];
    *out = 0;
}

// GetVariableText ( key, wide output buffer).
template<class Env> void GetVariableText(const Furniture& table, const char* key, Char16* out, Env& env)
{
    if (std::strcmp(key, kTestVariable) != 0) {
        env.Fatal("A script read unrecognized furniture variable \"%s\"", key);
        return;
    }
    FormatDecimal(out, table.m_TestVar);
}

// GetValue ( out, kind, data; returns out). The table has no values of its own: the base's two words are copied.
template<class Env> script::ScriptValue* GetValue(Furniture& table, script::ScriptValue* out, int32_t kind, intptr_t data, Env& env)
{
    script::ScriptValue scratch;
    *out = *env.BaseGetValue(table, &scratch, kind, data);
    return out;
}

// SetValue: the base's SetValue with the same arguments and its result.
template<class Env> uint32_t SetValue(Furniture& table, int32_t kind, intptr_t data, int32_t sourceKind, int32_t sourceData, Env& env)
{
    return env.BaseSetValue(table, kind, data, sourceKind, sourceData);
}

// SetOrientation (member call; table creation calls it). Orientation 0 or a table without chair sprites only stores the
// rotation. Eight-direction chairs and their plates turn by (4 - orientation) * 2 modulo 8; four-direction chairs by 4 - orientation modulo 4,
// and with plates the chair turns a second time instead of the plate (tests pin it). The remainder is C's: a negative sum stays negative.
// A table without chairs only stores the rotation.
inline void SetOrientation(Furniture& table, int32_t orientation)
{
    const tables::TableDef* definition = table.m_definition;
    if (orientation != 0 && table.m_chairSprites && definition->m_NumChairs != 0) {
        const bool plates = definition->m_hasPlates != 0;
        if (HasEightDirections(*table.m_chairSprites[0])) {
            const uint32_t step = (4u - static_cast<uint32_t>(orientation)) << 1;
            for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
                SpriteInstance& chair = *table.m_chairSprites[i];
                SetDirection(chair, static_cast<int32_t>(static_cast<uint32_t>(chair.m_direction) + step) % 8);
                if (plates) {
                    SpriteInstance& plate = *table.m_plateSprites[i];
                    SetDirection(plate, static_cast<int32_t>(static_cast<uint32_t>(plate.m_direction) + step) % 8);
                }
            }
        } else {
            for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
                SpriteInstance& chair = *table.m_chairSprites[i];
                for (int pass = 0; pass < (plates ? 2 : 1); ++pass)
                    SetDirection(chair,
                            static_cast<int32_t>(static_cast<uint32_t>(chair.m_direction) - static_cast<uint32_t>(orientation) + 4u) % 4);
            }
        }
    }
    table.m_rotation = orientation;
}

// The obl.save load (and the field table order): the class's field table (object::VisitFields), then the base
// class. Load reads into the arrays the constructor allocated and allocates nothing.
// Env: Bool/Int/UInt(label, key, field, hashed), Sprite(key, SpriteInstance*), Base(Furniture&).
template<class Env> void Fields(Furniture& table, Env& env)
{
    object::VisitFields(table, env);
    env.Base(table);
}

} // namespace obl::table
