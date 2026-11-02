#pragma once

#include "core/char16.h"
#include "game/tables/world_params_leaves.h"
#include "core/rect.h"
#include "game/ui/sprite.h"
#include "game/objects/object_fields.h"
#include "game/objects/object_static.h"

#include <cstdint>

// Scenery (docs/architecture/objects.md): bins, benches, street lamps and the
// other decorations. The bin fill level and the vandalism claim are read by the CleaningManager/RepairManager and by the guest
// vandalism script (docs/architecture/objects.md). The logic is scenery_*.h. The field order follows the save records.
namespace obl::object {

struct Scenery : Prop {
    Scenery() = default;
    // Builds the scenery of the given key (scenery_lifecycle.cpp).
    explicit Scenery(const char* key);
    // Marks the scenery as claimed (or freed) by a vandal and tells the world (prop.cpp).
    void SetVandalClaim(uint32_t claimed);
    // scenery.cpp: the name, the free seat count (returns the seats left), the bin fill and its animation, where a guest stands
    // to use it (fills `out` and returns it), the player's removal and a vandal's attack.
    void SetName(const char* name);
    int32_t UpdateFreeSeat();
    void SetBinFill(uint32_t fill);
    float* GetApproachPosition(float* out);
    void RequestRemoval();
    void Vandalise();
    // The slots that differ from the parent's (the unit tests' trap seam).
    ~Scenery() override;
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void Draw() override;
    void OnSelect(uint32_t clicked, uint32_t centerCamera) override;
    bool IsSelectable() override;
    const char* GetName() override;

    // The construction key copied by scenery initialization (strlen + 1 bytes, unbounded); what the scenery name query returns (not saved)
    char m_ObjectName[0x20] = {};
    int32_t m_AttachedGuest[2] = {};
    int32_t m_AttachedGuestState[2] = {}; // set to -1 by scenery initialization like m_AttachedGuest, never saved
    int32_t m_NumItems = 0; // bin fill level
    int32_t m_NumAttachedGuests = 0;
    int32_t m_MaxNumAttachedGuests = 0;
    int32_t m_FreeSeat = 0;
    uint8_t m_vandalised = 0; // vandal claim
    uint32_t m_RepairState = 0;
    uint8_t m_flashLight = 0;
    uint32_t m_PreVandlismAnim = 0;
    screen::Rect m_ReburnRect = {};
    screen::Rect m_ScrXYRect = {};
    uint8_t m_bakePending =
            0; // Scenery initialization sets it; baking scenery into the background clears it once the background is baked (not saved)
    // The light map of lamps, fountains and fires (scenery initialization; released by Destruct; not saved)
    ui::SpriteInstance* m_lightSprite = nullptr;
    float m_lightsOn = 0;
    float m_lightsOff = 0;
    uint8_t m_soundPlaying = 0;
    int32_t m_OwnerBuildID = 0;
    uint8_t m_canSelect = 0;
    uint8_t m_hasMenu =
            0; // Scenery initialization: the definition's MENU is not "None"; read by the scenery selectability query, not saved or loaded
    // A stored draw routine: a plain function pointer (not saved or hashed).
    using DrawFunc = void (*)(Scenery&);
    // Scenery drawing calls it; restoring saved scenery picks SceneryDraw{Preview,Bench,Lounger,Burned,Generic} (not saved)
    DrawFunc m_draw = nullptr;
    DrawFunc m_drawLight = nullptr; // SceneryDrawLight or SceneryDrawNothing, set by scenery initialization
    const tables::SceneryDef* m_definition = nullptr; // WorldParams scenery record (not saved)
};

// Scenery save records, then Prop's (the caller). Visitor: Int/UInt/Float/Bool(key, field, hashed),
// Ints(key, pointer, count, hashed) and Rect(key, rect, hashed): four UInt records (left, top, right, bottom) that all
// carry the one key (the obl.save writer adds ".left" and so on). Only the repair state is hashed.
template<SelfOf<Scenery> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Ints("scenery.attached_guests", o.m_AttachedGuest, 2, Hashed::No);
    v.Int("scenery.items", o.m_NumItems, Hashed::No);
    v.Int("scenery.attached_count", o.m_NumAttachedGuests, Hashed::No);
    v.Int("scenery.max_attached", o.m_MaxNumAttachedGuests, Hashed::No);
    v.Int("scenery.free_seat", o.m_FreeSeat, Hashed::No);
    v.Bool("scenery.vandalised", o.m_vandalised, Hashed::No);
    v.UInt("scenery.repair_state", o.m_RepairState, Hashed::Yes);
    v.Bool("scenery.flash_light", o.m_flashLight, Hashed::No);
    v.UInt("scenery.pre_vandalism_anim", o.m_PreVandlismAnim, Hashed::No);
    v.Rect("scenery.reburn_rect", o.m_ReburnRect, Hashed::No);
    v.Rect("scenery.screen_rect", o.m_ScrXYRect, Hashed::No);
    v.Float("scenery.lights_on", o.m_lightsOn, Hashed::No);
    v.Float("scenery.lights_off", o.m_lightsOff, Hashed::No);
    v.Bool("scenery.sound_playing", o.m_soundPlaying, Hashed::No);
    v.Int("scenery.owner_building", o.m_OwnerBuildID, Hashed::No);
    v.Bool("scenery.can_select", o.m_canSelect, Hashed::No);
}

} // namespace obl::object
