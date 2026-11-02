#pragma once

#include "core/char16.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_fields.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// Furniture (type 5; docs/architecture/objects.md): the tables of a building (Building::m_Tables) with their chairs, plates and
// waiter (docs/architecture/objects.md). The logic is furniture.h, furniture_draw.h and
// furniture_receive.h. The fields the constructor leaves unset (they keep the zero of their member
// initializers) are noted.
namespace obl::object {

// One seat of a table.
struct TableChair {
    int32_t m_ChairDir = 0;
    int32_t m_ChairOccupier = -1; // guest object id, -1 = free
    uint8_t m_ChairSeated = 0;
    uint8_t m_waitingForWaiter = 0;
    int32_t m_position = 0;
};

struct Furniture : object::Prop {
    Furniture() = default;
    // Builds the table of the given kind (furniture_runtime.cpp).
    explicit Furniture(const char* name);
    // Releases the chairs, plates and sprites (furniture_runtime.cpp).
    ~Furniture() override;
    // The slots that differ from the parent's (game/prop_virtuals.cpp, the unit tests' trap seam).
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void SetVariable(const char* key, const void* value) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    void Draw() override;

    const tables::TableDef* m_definition = nullptr; // the TABLE_<key> record of WorldParams
    int32_t m_TestVar = 0; // TABLE_TEST_VAR of SetVariable/GetVariableText; left unset, not saved
    ui::SpriteInstance** m_chairSprites = nullptr; // m_NumChairs instances of the chair sprite; null when that sprite is "NONE"
    ui::SpriteInstance** m_plateSprites = nullptr; // m_NumChairs "Waiter" instances (d_Static_plate), only with chairs and plates
    uint8_t m_drawWaiter = 0;
    uint8_t* m_pbDrawPlate = nullptr; // m_NumChairs flags, allocated together with m_plateSprites (the destructor never frees it)
    int32_t m_WaiterServing = -1;
    int32_t m_WaiterPos = 0; // left unset
    int32_t m_WaiterID = 0; // object id; the building shows/hides it together with the table. Left unset
    uint8_t m_waiterReady = 0;
    TableChair* m_chairs = nullptr; // m_definition->m_NumChairs seats
    int32_t m_rotation = 0;
    int32_t m_ownerId = 0; // building id, set by AttachToBuilding
    uint8_t m_serveTable = 0; // copy of the definition's SERVE_TABLE
    int32_t m_GuestsRemoved = 0;
    int32_t m_GuestsAdded = 0;
};

// The table's own Save records, then the static object's (the caller): per chair five records and the chair sprite, the ten scalars,
// with plates per chair the plate sprite and its flag. Ids and the owner are Int records; directions, positions and counters UInt.
// Visitor: Bool/Int/UInt(key, field, hashed), Sprite(key, SpriteInstance*) (the sprite's own records). `Self` is
// `Furniture` or `const Furniture`.
template<SelfOf<Furniture> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    const tables::TableDef* definition = o.m_definition;
    for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
        auto& chair = o.m_chairs[i];
        v.Bool("table.chairs[].seated", chair.m_ChairSeated, Hashed::No);
        v.Int("table.chairs[].occupier", chair.m_ChairOccupier, Hashed::No);
        v.UInt("table.chairs[].dir", chair.m_ChairDir, Hashed::No);
        v.UInt("table.chairs[].position", chair.m_position, Hashed::No);
        v.Bool("table.chairs[].waiting_for_waiter", chair.m_waitingForWaiter, Hashed::No);
        if (o.m_chairSprites) v.Sprite("table.chairs[].sprite", o.m_chairSprites[i]);
    }
    v.Bool("table.draw_waiter", o.m_drawWaiter, Hashed::No);
    v.Int("table.waiter_serving", o.m_WaiterServing, Hashed::No);
    v.UInt("table.waiter_pos", o.m_WaiterPos, Hashed::No);
    v.Int("table.waiter", o.m_WaiterID, Hashed::No);
    v.Bool("table.waiter_ready", o.m_waiterReady, Hashed::No);
    v.UInt("table.rotation", o.m_rotation, Hashed::No);
    v.Int("table.owner", o.m_ownerId, Hashed::Yes);
    v.Bool("table.serve_table", o.m_serveTable, Hashed::No);
    v.UInt("table.guests_removed", o.m_GuestsRemoved, Hashed::No);
    v.UInt("table.guests_added", o.m_GuestsAdded, Hashed::No);
    if (definition->m_hasPlates) {
        for (uint32_t i = 0; i < definition->m_NumChairs; ++i) {
            v.Sprite("table.plates[].sprite", o.m_plateSprites[i]);
            v.Bool("table.plates[].draw", o.m_pbDrawPlate[i], Hashed::No);
        }
    }
}

} // namespace obl::object
