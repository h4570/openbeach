#include "game/ui/sprite_bank.h"
#include "game/map/grid_state.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/fatal.h"
#include "game/objects/guest_pool.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>

// The pool and slide guest functions (src/game/objects/guest_pool.h, guest pool and slide walks, docs/architecture/objects.md): GetNextPoolPoint (by the guest's
// m_ActionState: face the pool building, teleport next to it, post a walk to the entry/exit spot, or pick the next tile of the pool),
// ChooseFunSlideAction (swap the guest's sprite for its funpool sprite, teleport it to the top of the slide, face the slide and send itself the
// delayed 0x39 message) and SetupFunPoolSwim (the way back: swap to the swimming sprite, put the guest at the foot of the slide, send it to the
// landing spot). The script reaches them through the guest's CallFunction. The building is m_DestObjectID.
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using ui::SpriteInstance;

constexpr auto kCreateSprite = [](auto* owner, auto... args) {
    return static_cast<ui::SpriteBank*>(owner)->CreateObservedInstance(args...);
};
constexpr auto kReleaseSprite = [](auto* owner, void* instance) { static_cast<ui::SpriteBank*>(owner)->ReleaseInstance(instance); };

test::CallCounter g_pool("objects.guest-get-next-pool-point");
test::CallCounter g_slide("objects.guest-choose-fun-slide-action");
test::CallCounter g_swim("objects.guest-setup-fun-pool-swim");

SpriteInstance* SpriteOf(object::Base& object)
{
    return static_cast<SpriteInstance*>(object.m_sprite);
}
void* SpriteBank()
{
    return ui::GetSpriteBank();
}

messages::Handler* Handler()
{
    return messages::message_facade::Current();
}

messages::Message SelfMessage(const Guest& guest, int32_t type)
{
    messages::Message message{};
    message.sender = static_cast<int>(guest.m_id);
    message.receiver = static_cast<int>(guest.m_id);
    message.type = type;
    return message;
}

void PostToSelf(const Guest& guest, int32_t type, const guest_walk::Goal& goal)
{
    messages::Message message = SelfMessage(guest, type);
    message.flags = goal.x;
    message.data = goal.y;
    messages::message_facade::Post(Handler(), &message);
}

void Teleport(Guest& guest, float x, float y, float z)
{
    guest.Teleport(x, y, z);
}

// The sprite directions of the animation the guest's sprite plays; false when the sprite's animation chain is not there.
bool DirectionCount(const SpriteInstance* sprite, uint8_t& count)
{
    if (!sprite || !sprite->m_sprite || !sprite->m_sprite->m_anims) return false;
    if (static_cast<uint32_t>(sprite->m_animIndex) >= static_cast<uint32_t>(sprite->m_sprite->m_animCount)) return false;
    const ui::SpriteAnim* anim = sprite->m_sprite->m_anims[sprite->m_animIndex];
    if (!anim) return false;
    count = anim->m_directions;
    return true;
}

// The guest turns to `facing` (clamped to the directions of its current animation).
void Face(Guest& guest, int32_t facing)
{
    uint8_t count = 0;
    DirectionCount(SpriteOf(guest), count);
    SpriteOf(guest)->m_direction = guest_pool::ClampFacing(facing, count);
}

// ---- GetNextPoolPoint ------------------------------------------------------------------------------------------------------

bool UsesBuildingDirection(int32_t state)
{
    return state == guest_pool::kStateEnter || state == guest_pool::kStateSwimIn || state == guest_pool::kStateFace ||
            state == guest_pool::kStateLeave || state == guest_pool::kStateWalkOut;
}

// The building, its sprite and the guest's animation chain must exist; a call without them ends in UnhandledInput.
bool PoolDeclined(Guest& guest)
{
    object::Base* building = object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID);
    if (!building) return true;
    const int32_t state = guest.m_ActionState;
    if (!UsesBuildingDirection(state)) return false;
    if (!building->m_sprite) return true;
    uint8_t count = 0;
    return (state == guest_pool::kStateEnter || state == guest_pool::kStateFace) && !DirectionCount(SpriteOf(guest), count);
}

void Pool(Guest& guest)
{
    object::Base* building = object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID);
    const int32_t state = guest.m_ActionState;
    const int32_t direction = UsesBuildingDirection(state) ? SpriteOf(*building)->m_direction : 0;
    const float bx = building->m_WorldXYZ[0], by = building->m_WorldXYZ[1];
    switch (state) {
    case guest_pool::kStateEnter:
        Face(guest, guest_pool::Facing(guest_pool::kPoolFacingBase, direction));
        if (guest_pool::KnownDirection(direction)) {
            const guest_pool::Point at = guest_pool::TeleportPoint(bx, by, guest_pool::kEntryOffsets[direction]);
            Teleport(guest, at.x, at.y, building->m_WorldXYZ[2]);
        }
        return;
    case guest_pool::kStateSwimIn:
        PostToSelf(guest, messages::MT_SWIM2SEAPOINT, guest_pool::PostedGoal(bx, by, direction, guest_pool::kEntryOffsets));
        return;
    case guest_pool::kStateFace: Face(guest, guest_pool::Facing(guest_pool::kPoolFacingBase, direction)); return;
    case guest_pool::kStateLeave:
        if (guest_pool::KnownDirection(direction)) {
            const guest_pool::Point at = guest_pool::TeleportPoint(bx, by, guest_pool::kExitOffsets[direction]);
            Teleport(guest, at.x, at.y, building->m_WorldXYZ[2]);
        }
        return;
    case guest_pool::kStateWalkOut:
        PostToSelf(guest, messages::MT_MOVE2POINT, guest_pool::PostedGoal(bx, by, direction, guest_pool::kExitOffsets));
        return;
    default: {
        int tile[2];
        ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
        grid_math::PathPoint goal{};
        if (!guest_pool::NextPoolTile(
                    *GetGrid(), static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), [] { return Rand(); }, goal))
            return;
        guest.m_walkpoints[0][0] = goal.x;
        guest.m_walkpoints[0][1] = goal.y;
        guest.m_walkpoints[0][2] = 0.0f;
        PostToSelf(guest, messages::MT_SWIM2SEAPOINT, guest_walk::TruncatedGoal(guest.m_walkpoints[0][0], guest.m_walkpoints[0][1]));
        return;
    }
    }
}

// ---- ChooseFunSlideAction --------------------------------------------------------------------------------------------------

// A direction past 3 reads stale stack words for the offset, and the guest's sprite name and the building are read without a check.
bool SlideDeclined(Guest& guest)
{
    object::Base* building = object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID);
    if (!building || !building->m_sprite || !guest_pool::KnownDirection(SpriteOf(*building)->m_direction)) return true;
    const SpriteInstance* sprite = SpriteOf(guest);
    return !sprite || !sprite->m_name;
}

void Slide(Guest& guest)
{
    const int32_t draw = Rand() & (guest_pool::kSlideActions - 1);
    object::Base* building = object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID);
    SpriteInstance* old = SpriteOf(guest);
    const int32_t previousPalette = old->m_paletteIndex;
    const int kind = guest_pool::SlideSpriteIndex(old->m_name);
    kReleaseSprite(SpriteBank(), old);
    SpriteInstance* sprite = kCreateSprite(SpriteBank(), guest_pool::kSlideSprites[kind]);
    guest.m_sprite = sprite;
    sprite->m_keyline = GetObjectManager()->m_globalKeyline;
    sprite->m_paletteIndex = guest_pool::SlidePalette(previousPalette, sprite->m_sprite->m_paletteCount);
    guest.m_visible = 0;
    const int32_t direction = SpriteOf(*building)->m_direction;
    const guest_pool::Point top =
            guest_pool::TeleportPoint(building->m_WorldXYZ[0], building->m_WorldXYZ[1], guest_pool::kSlideOffsets[draw][direction]);
    Teleport(guest, top.x, top.y, 0.0f);
    Face(guest, guest_pool::SlideFacing(guest_pool::kSlideModes[draw], direction));
    messages::Message message = SelfMessage(guest, guest_pool::kSlideMessageType);
    message.extra = guest_pool::kSlideMessageMember14;
    messages::message_facade::PostDelayed(Handler(), &message, guest_pool::SlideDelay(draw));
    guest.m_ActionState = draw + 1;
}

// ---- SetupFunPoolSwim ------------------------------------------------------------------------------------------------------

// The building is read without a check, and so are its sprite, its definition (state 6), the guest's sprite name (states 1..4) and its animation chain
// (state 5); a direction past 3 selects offsets the code never set, and so does state 0 (read as one of the swapping states). Such a call ends in
// UnhandledInput (nothing was changed or drawn before). Any other state does nothing.
bool SwimDeclined(Guest& guest)
{
    auto* building = static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID));
    if (!building) return true;
    const int32_t state = guest.m_ActionState;
    if (state == 0) return true;
    const bool swaps = guest_pool::SwimSwapsSprite(state);
    if (!swaps && state != guest_pool::kSwimStateLanding && state != guest_pool::kSwimStateFootprint) return false;
    if (!building->m_sprite || !guest_pool::KnownDirection(SpriteOf(*building)->m_direction)) return true;
    if (swaps) {
        const SpriteInstance* sprite = SpriteOf(guest);
        return !sprite || !sprite->m_name;
    }
    uint8_t count = 0;
    if (state == guest_pool::kSwimStateLanding) return !DirectionCount(SpriteOf(guest), count);
    return !building->m_definition;
}

void Swim(Guest& guest)
{
    object::Base* building = object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID);
    const int32_t state = guest.m_ActionState;
    const bool swaps = guest_pool::SwimSwapsSprite(state);
    if (!swaps && state != guest_pool::kSwimStateLanding && state != guest_pool::kSwimStateFootprint) return;
    const int32_t direction = SpriteOf(*building)->m_direction;
    const float bx = building->m_WorldXYZ[0], by = building->m_WorldXYZ[1];
    if (swaps) {
        SpriteInstance* old = SpriteOf(guest);
        const int32_t previousPalette = old->m_paletteIndex;
        const int kind = guest_pool::SwimSpriteIndex(old->m_name);
        kReleaseSprite(SpriteBank(), old);
        SpriteInstance* sprite = kCreateSprite(SpriteBank(), guest_pool::kSwimSprites[kind]);
        guest.m_sprite = sprite;
        sprite->m_keyline = GetObjectManager()->m_globalKeyline;
        sprite->m_paletteIndex = guest_pool::SlidePalette(previousPalette, sprite->m_sprite->m_paletteCount);
        const guest_pool::Point start = guest_pool::TeleportPoint(bx, by, guest_pool::kSwimStartOffsets[state - 1][direction]);
        Teleport(guest, start.x, start.y, 0.0f);
        Face(guest, guest_pool::SlideFacing(guest_pool::kSwimStartModes[state - 1], direction));
        PostToSelf(guest, messages::MT_MOVE2POINT, guest_pool::PostedGoal(bx, by, direction, guest_pool::kSwimLandingOffsets));
        return;
    }
    if (state == guest_pool::kSwimStateLanding) {
        Face(guest, guest_pool::Facing(guest_pool::kSwimFacingBase, direction));
        const guest_pool::Point at = guest_pool::TeleportPoint(bx, by, guest_pool::kSwimLandingOffsets[direction]);
        Teleport(guest, at.x, at.y, 0.0f);
        return;
    }
    const guest_pool::Point at = guest_pool::TeleportPoint(bx, by, guest_pool::kSwimFootprintStart[direction]);
    Teleport(guest, at.x, at.y, 0.0f);
    const tables::BuildingDef* def = static_cast<Building*>(building)->m_definition;
    int tile[2];
    ui::MapView::WorldToTile(tile, building->m_WorldXYZ);
    const grid_math::PathPoint spot = GetGrid()->GetFootprintOffsetPoint(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]),
            def->m_GridWidth, def->m_GridHeight, guest_pool::kSwimFootprintOffsetX, guest_pool::kSwimFootprintOffsetY,
            static_cast<uint32_t>(direction));
    PostToSelf(guest, messages::MT_MOVE2POINT, guest_walk::TruncatedGoal(spot.x, spot.y));
}

} // namespace

void Guest::GetNextPoolPoint()
{
    if (PoolDeclined(*this)) fatal::UnhandledInput("objects.guest-get-next-pool-point", "no building, sprite or animation chain");
    g_pool.Count();
    Pool(*this);
}

void Guest::ChooseFunSlideAction()
{
    if (SlideDeclined(*this)) fatal::UnhandledInput("objects.guest-choose-fun-slide-action", "no building, sprite or animation chain");
    g_slide.Count();
    Slide(*this);
}

void Guest::SetupFunPoolSwim()
{
    if (SwimDeclined(*this))
        fatal::UnhandledInput("objects.guest-setup-fun-pool-swim", "no building, sprite, direction or animation chain");
    g_swim.Count();
    Swim(*this);
}

} // namespace obl::object
