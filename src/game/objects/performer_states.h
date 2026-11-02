#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_performers.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/performer_attach.h"

#include <cstdint>

// Performer construction, teardown, updates and state transitions use an injected environment.
// Singer and WeightLifter also draw attached stage sprites. src/game/objects/performer_virtuals.cpp supplies game services;
// unit tests use a fake environment to check the order of calls.
//   Construct:   SpriteInstance* CreateSprite(const char* name)               sprite creation from the bank
//                float SetAnim(SpriteInstance*, const char* name, float* position)   starting the named sprite animation at the world's game time; the length in seconds
//   Destruct:    void ReleaseSprite(SpriteInstance*)                          returning the sprite to its bank (a null sprite is allowed)
//   Update:      bool HumanUpdate(Mover&)                              the human base update
//                void Fatal(const char* format, const char* name)             FatalError
//   EnterState:  SetAnim, CreateSprite and
//                Building& Building(int id)                             the object manager's lookup
//                void FootprintPoint(Building&, float offsetX, float offsetY, float (&out)[2])   as in performer_attach.h
//                void Post(const Message&)  void PostDelayed(const Message&, float seconds)             the message handler's post / delayed post
//                void Teleport(Mover&, float x, float y, float z)      the object's virtual Teleport
//   Draw:        void Draw(SpriteInstance*, float* position)                  drawing the sprite in world space at the world's game time
// The words of a message after data are zero. `flags` carries the first payload word: x of a move, 0 of a
// state change (whose new state is data).
namespace obl::performer {

enum class Kind { Aerobic, Cabaret, Catwalk, Singer, WeightLifter, WetTShirt };

namespace detail {
// Offsets from the show building's footprint (the rotated footprint offset point): where a performer leaves the stage, and the three (four) marks of a catwalk,
// a weight lifter and a wet T-shirt contestant by act.
constexpr float kLeaveOffsetX = -122.0f, kLeaveOffsetY = -286.0f;
constexpr float kMarkOffsets[4][2] = {{-58.0f, -290.0f}, {-30.0f, -320.0f}, {-3.0f, -348.0f}, {-28.0f, -266.0f}};
constexpr float kCabaretSideOffsetX = -25.0f, kCabaretSideOffsetY = -282.0f;
constexpr int kAnimLoop = 0, kAnimOnce = 2; // SpriteInstance::m_animType

inline ui::SpriteInstance* Sprite(object::Base& performer)
{
    return static_cast<ui::SpriteInstance*>(performer.m_sprite);
}

inline messages::Message SelfMessage(const object::Base& performer, int type, int first, int second)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(performer.m_id);
    message.receiver = static_cast<int32_t>(performer.m_id);
    message.type = type;
    message.flags = first;
    message.data = second;
    return message;
}
inline messages::Message MoveMessage(const object::Base& performer, int type, float x, float y)
{
    return SelfMessage(performer, type, FloatToIntWrap(static_cast<double>(x)), FloatToIntWrap(static_cast<double>(y)));
}
// MT_MOVE2POINT to the point `offset` away from the building's footprint.
template<class Env> void WalkToOffset(object::Base& performer, object::Building& building, float offsetX, float offsetY, Env& env)
{
    float point[2];
    env.FootprintPoint(building, offsetX, offsetY, point);
    env.Post(MoveMessage(performer, messages::MT_MOVE2POINT, point[0], point[1]));
}
inline void HideBehindStage(object::Base& performer, const object::Building& building)
{
    const int32_t rotation = Rotation(building);
    if (rotation == 2 || rotation == 1) performer.m_visible = 0;
}
// The last direction of the animation the sprite plays when `direction` is beyond it.
inline int32_t ClampDirection(const ui::SpriteInstance& sprite, int32_t direction)
{
    const int directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions & 0xff;
    return direction < directions ? direction : directions - 1;
}
// A state change to itself: act 0 goes on to state 4 (the show), any other back to state 0.
inline messages::Message NextState(const object::Base& performer, uint32_t type)
{
    return SelfMessage(performer, messages::MT_STATECHANGE, 0, type == 0 ? 4 : 0);
}
} // namespace detail

// ---- Construct / Destruct (the performer's constructor after Mover's, its destructor before Mover's) ----------------------------------------------------------------------------------------------------------------

template<class Env> void Construct(object::Aerobic& performer, Env& env)
{
    performer.m_finish = 0;
}
template<class Env> void Construct(object::Cabaret& performer, Env& env)
{
    performer.m_finish = 0;
    performer.m_maxVel = 60.0f;
    performer.m_maxForce = 20.0f;
}
template<class Env> void Construct(object::Catwalk& performer, Env& env)
{
    performer.m_sprite = env.CreateSprite("Gail");
    performer.m_finished = 0;
}
template<class Env> void Construct(object::Singer& performer, Env& env)
{
    performer.m_sprite = env.CreateSprite("Stage_Events");
    ui::SpriteInstance* mic = env.CreateSprite("Stage_Mic");
    performer.m_attachedSprite = mic;
    env.SetAnim(mic, "SINGMIC_STILL", performer.m_WorldXYZ);
    performer.m_finish = 0;
}
template<class Env> void Construct(object::WeightLifter& performer, Env& env)
{
    performer.m_sprite = env.CreateSprite("Stage_Events");
    performer.m_attachedSprite = nullptr;
    performer.m_finished = 0;
}
template<class Env> void Construct(object::WetTShirt& performer, Env& env)
{
    performer.m_finished = 0;
}

namespace detail {
template<class Env> void ReleaseOwn(object::Mover& performer, Kind kind, Env& env)
{
    env.ReleaseSprite(Sprite(performer));
    performer.m_sprite = nullptr;
}
} // namespace detail

template<class Env> void Destruct(object::Aerobic& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::Aerobic, env);
}
template<class Env> void Destruct(object::Cabaret& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::Cabaret, env);
}
template<class Env> void Destruct(object::Catwalk& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::Catwalk, env);
}
// The microphone is released without a null check, the weights only when there are any.
template<class Env> void Destruct(object::Singer& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::Singer, env);
    env.ReleaseSprite(static_cast<ui::SpriteInstance*>(performer.m_attachedSprite));
    performer.m_attachedSprite = nullptr;
}
template<class Env> void Destruct(object::WeightLifter& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::WeightLifter, env);
    if (performer.m_attachedSprite) {
        env.ReleaseSprite(static_cast<ui::SpriteInstance*>(performer.m_attachedSprite));
        performer.m_attachedSprite = nullptr;
    }
}
template<class Env> void Destruct(object::WetTShirt& performer, Env& env)
{
    detail::ReleaseOwn(performer, Kind::WetTShirt, env);
}

// ---- Update ------------------------------------------------------------------------------------------------------------------------------

inline const char* UpdateErrorFormat(Kind kind)
{
    switch (kind) {
    case Kind::Aerobic: return "Update of aerobics instructor \"%s\" failed";
    case Kind::Cabaret: return "Update of cabaret dancer \"%s\" failed";
    case Kind::Catwalk: return "Update of catwalk model \"%s\" failed";
    case Kind::Singer: return "Update of singer \"%s\" failed";
    case Kind::WeightLifter: return "Update of weight lifter \"%s\" failed";
    default: return "Update of wet T-shirt performer \"%s\" failed";
    }
}
template<class Env> bool Update(object::Mover& performer, Kind kind, Env& env)
{
    if (!env.HumanUpdate(performer)) env.Fatal(UpdateErrorFormat(kind), performer.m_Name);
    return true;
}

// ---- OnExitState -------------------------------------------------------------------------------------------------------------------------

// Aerobic state exit (also OnExitState of Cabaret and Singer) and Catwalk state exit (also WetTShirt): the answer for the current state.
inline bool StageExitState(int32_t state)
{
    return state == 0 || state == 1;
}
inline bool ShowExitState(int32_t state)
{
    return state == 0 || state == 1 || state == 4;
}
// WeightLifter state exit: leaving the show (state 4) drops the weights (no null check).
template<class Env> bool ExitState(object::WeightLifter& performer, Env& env)
{
    if (performer.m_state == 0 || performer.m_state == 1) return true;
    if (performer.m_state != 4) return false;
    env.ReleaseSprite(static_cast<ui::SpriteInstance*>(performer.m_attachedSprite));
    performer.m_attachedSprite = nullptr;
    return true;
}

// ---- OnEnterState ------------------------------------------------------------------------------------------------------------------------
// The answer is a byte: 1 for a handled state. For any other state Aerobic, Singer, Catwalk and WetTShirt answer with the low byte of the state number;
// Cabaret and WeightLifter answer 0 (HasState keeps such states away).

namespace detail {
// Aerobic state entry and Singer state entry differ in the walk animation only.
template<class T, class Env> uint8_t EnterStageAct(T& performer, uint32_t state, const char* walk, Env& env)
{
    if (state == 0) return 1;
    if (state == 1) {
        env.Post(SelfMessage(performer, messages::MT_STATECHANGE, 0, 0));
        object::Building& building = env.Building(performer.m_OwnerID);
        HideBehindStage(performer, building);
        env.SetAnim(Sprite(performer), walk, performer.m_WorldXYZ);
        WalkToOffset(performer, building, kStageOffsetX, kStageOffsetY, env);
        return 1;
    }
    if (state == 6) {
        Sprite(performer)->m_animType = kAnimLoop;
        env.SetAnim(Sprite(performer), walk, performer.m_WorldXYZ);
        WalkToOffset(performer, env.Building(performer.m_OwnerID), kLeaveOffsetX, kLeaveOffsetY, env);
        return 1;
    }
    return static_cast<uint8_t>(state);
}
inline const char* WeightWalk(uint32_t type)
{
    return type == 0 ? "D_WEIGHT_THIN_WALK" : type == 1 ? "D_WEIGHT_FAT_WALK" : type == 2 ? "D_WEIGHT_BIG_WALK" : nullptr;
}
} // namespace detail

template<class Env> uint8_t EnterState(object::Aerobic& performer, uint32_t state, Env& env)
{
    return detail::EnterStageAct(performer, state, "d_walk_normal", env);
}
template<class Env> uint8_t EnterState(object::Singer& performer, uint32_t state, Env& env)
{
    return detail::EnterStageAct(performer, state, "SING_WALK", env);
}

// Cabaret state entry. State 1: the dancer appears at the stage exit and walks (MT_MOVE2SEAPOINT, the straight-line move) to a mark that
// depends on the building's rotation: next to the building for rotations 0 and 3, behind the stage (hidden) for 1 and 2.
template<class Env> uint8_t EnterState(object::Cabaret& performer, uint32_t state, Env& env)
{
    using namespace detail;
    if (state == 0) return 1;
    if (state != 1) return 0;
    ui::SpriteInstance* sprite = Sprite(performer);
    env.SetAnim(sprite, "D_CABARET_WALK_CYCLE", performer.m_WorldXYZ);
    sprite->m_animType = kAnimLoop;
    performer.m_actionState = 0;
    object::Building& building = env.Building(performer.m_OwnerID);
    float start[2];
    env.FootprintPoint(building, kLeaveOffsetX, kLeaveOffsetY, start);
    env.Teleport(performer, start[0], start[1], 0.0f);
    const int32_t rotation = Rotation(building);
    float target[2];
    int32_t direction;
    if (rotation == 0) {
        target[0] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[0]) - 21.0);
        target[1] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[1]) - 322.0);
        direction = 4;
    } else if (rotation == 3) {
        target[0] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[0]) + 33.0);
        target[1] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[1]) - 308.0);
        direction = 6;
    } else {
        performer.m_visible = 0;
        env.FootprintPoint(building, kCabaretSideOffsetX, kCabaretSideOffsetY, target);
        direction = ((4 - Rotation(building)) * 2) % 8;
    }
    sprite->m_direction = ClampDirection(*sprite, direction);
    env.Post(MoveMessage(performer, messages::MT_MOVE2SEAPOINT, target[0], target[1]));
    return 1;
}

// Catwalk state entry. An act outside 0..2 walks (state 0) to the footprint itself (no offset is defined for it).
template<class Env> uint8_t EnterState(object::Catwalk& performer, uint32_t state, Env& env)
{
    using namespace detail;
    ui::SpriteInstance* sprite = Sprite(performer);
    switch (state) {
    case 0: {
        object::Building& building = env.Building(performer.m_OwnerID);
        env.SetAnim(sprite, "D_CATWALK", performer.m_WorldXYZ);
        const float* mark = performer.m_variant <= 2 ? kMarkOffsets[performer.m_variant] : nullptr;
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, building, mark ? mark[0] : 0.0f, mark ? mark[1] : 0.0f, env);
        return 1;
    }
    case 1:
        performer.m_actionState = 0;
        env.Post(NextState(performer, performer.m_variant));
        return 1;
    case 4: {
        const float length = env.SetAnim(sprite, "D_CATWALK", performer.m_WorldXYZ);
        sprite->m_animType = kAnimLoop;
        performer.m_actionState = 0;
        env.PostDelayed(MoveMessage(performer, messages::MT_MOVE2POINT, performer.m_ActionPoint[0], performer.m_ActionPoint[1]), length);
        return 1;
    }
    case 6:
        env.SetAnim(sprite, "D_CATWALK", performer.m_WorldXYZ);
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, env.Building(performer.m_OwnerID), kLeaveOffsetX, kLeaveOffsetY, env);
        return 1;
    default: return static_cast<uint8_t>(state);
    }
}

// WeightLifter state entry. An act outside 0..2 has no walk animation; its mark (state 0) and the delay of its show (state 4) are
// zero. State 4 makes the weights ("STAGE_EVENTS", D_WEIGHT_STILL played once) facing the audience.
template<class Env> uint8_t EnterState(object::WeightLifter& performer, uint32_t state, Env& env)
{
    using namespace detail;
    ui::SpriteInstance* sprite = Sprite(performer);
    const char* walk = WeightWalk(performer.m_variant);
    switch (state) {
    case 0: {
        object::Building& building = env.Building(performer.m_OwnerID);
        if (walk) env.SetAnim(sprite, walk, performer.m_WorldXYZ);
        const float* mark = performer.m_variant <= 2 ? kMarkOffsets[performer.m_variant] : nullptr;
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, building, mark ? mark[0] : 0.0f, mark ? mark[1] : 0.0f, env);
        return 1;
    }
    case 1:
        HideBehindStage(performer, env.Building(performer.m_OwnerID));
        performer.m_actionState = 0;
        env.Post(NextState(performer, performer.m_variant));
        return 1;
    case 4: {
        const float length = walk ? env.SetAnim(sprite, walk, performer.m_WorldXYZ) : 0.0f;
        sprite->m_animType = kAnimLoop;
        ui::SpriteInstance* weights = env.CreateSprite("STAGE_EVENTS");
        performer.m_attachedSprite = weights;
        env.SetAnim(weights, "D_WEIGHT_STILL", performer.m_WorldXYZ);
        weights->m_animType = kAnimOnce;
        const int32_t direction = (11 - 2 * Rotation(env.Building(performer.m_OwnerID))) % 8;
        weights->m_direction = ClampDirection(*weights, direction);
        performer.m_actionState = 0;
        env.PostDelayed(MoveMessage(performer, messages::MT_MOVE2POINT, performer.m_ActionPoint[0], performer.m_ActionPoint[1]), length);
        return 1;
    }
    case 6:
        if (walk) env.SetAnim(sprite, walk, performer.m_WorldXYZ);
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, env.Building(performer.m_OwnerID), kLeaveOffsetX, kLeaveOffsetY, env);
        return 1;
    default: return 0;
    }
}

// WetTShirt state entry. Acts 0..2 are the contestants (sprite "wts"), 3 is the compere ("wtsc"); the act is compared signed for the animation
// and unsigned for the mark, and an act above 3 walks (state 0) to the footprint itself (no offset is defined for it).
template<class Env> uint8_t EnterState(object::WetTShirt& performer, uint32_t state, Env& env)
{
    using namespace detail;
    ui::SpriteInstance* sprite = Sprite(performer);
    switch (state) {
    case 0: {
        object::Building& building = env.Building(performer.m_OwnerID);
        const bool contestant = static_cast<int32_t>(performer.m_variant) < 3;
        env.SetAnim(sprite,
                !contestant                            ? "WTSC_WALK_NB"
                        : performer.m_actionState == 6 ? "WTS_WALK_WET"
                                                       : "WTS_WALK_DRY",
                performer.m_WorldXYZ);
        performer.m_actionState = contestant ? 0 : 5;
        const float* mark = performer.m_variant <= 3 ? kMarkOffsets[performer.m_variant] : nullptr;
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, building, mark ? mark[0] : 0.0f, mark ? mark[1] : 0.0f, env);
        return 1;
    }
    case 1:
        HideBehindStage(performer, env.Building(performer.m_OwnerID));
        performer.m_actionState = 0;
        env.Post(NextState(performer, performer.m_variant));
        return 1;
    case 4: {
        const float length = env.SetAnim(sprite, "WTS_WAIT_TO_WALK_DRY", performer.m_WorldXYZ);
        sprite->m_animType = kAnimOnce;
        performer.m_actionState = 0;
        env.Post(MoveMessage(performer, messages::MT_MOVE2POINT, performer.m_ActionPoint[0], performer.m_ActionPoint[1]));
        env.PostDelayed(SelfMessage(performer, messages::MT_PLAYANIMATION, 0, 0), length);
        return 1;
    }
    case 6:
        env.SetAnim(sprite, performer.m_variant == 3 ? "WTSC_WALK_NB" : "WTS_WALK_DRY", performer.m_WorldXYZ);
        sprite->m_animType = kAnimLoop;
        WalkToOffset(performer, env.Building(performer.m_OwnerID), kLeaveOffsetX, kLeaveOffsetY, env);
        return 1;
    default: return static_cast<uint8_t>(state);
    }
}

// ---- PlayAnimation -----------------------------------------------------------------------------------------------------------------------

// PlayAnimation in all six classes ( the two flags are read as bytes, the fifth argument is unused):
// animation type 1 (countdown), 0 (loop) or 2 (once), the animation itself, and for one that ends a delayed state change to `endState` (-1 = none)
// through the object's SendDelayed, which is a no-op in every performer class, so nothing is sent. A length of zero
// (or NaN) counts as one second.
//   Env::SendDelayed(Mover&, const Message&, float seconds)   SendDelayed (sendMessageDelayed)
template<class Env>
float PlayAnimation(object::Mover& performer, const char* animation, bool countdown, int32_t endState, bool looped, Env& env)
{
    ui::SpriteInstance* sprite = detail::Sprite(performer);
    sprite->m_animType = countdown ? 1 : looped ? detail::kAnimLoop : detail::kAnimOnce;
    float length = env.SetAnim(sprite, animation, performer.m_WorldXYZ);
    if (length == 0.0f || length != length) length = 1.0f;
    if (!looped && endState != -1)
        env.SendDelayed(performer, detail::SelfMessage(performer, messages::MT_STATECHANGE, 0, endState), length);
    return length;
}

// ---- Draw --------------------------------------------------------------------------------------------------------------------------------

// Singer drawing: the microphone at the stage point, then the singer.
template<class Env> void Draw(object::Singer& performer, Env& env)
{
    env.Draw(static_cast<ui::SpriteInstance*>(performer.m_attachedSprite), performer.m_ActionPoint);
    env.Draw(detail::Sprite(performer), performer.m_WorldXYZ);
}
// WeightLifter drawing: the weights (when there are any) at the stage point, then the lifter.
template<class Env> void Draw(object::WeightLifter& performer, Env& env)
{
    if (performer.m_attachedSprite) env.Draw(static_cast<ui::SpriteInstance*>(performer.m_attachedSprite), performer.m_ActionPoint);
    env.Draw(detail::Sprite(performer), performer.m_WorldXYZ);
}

} // namespace obl::performer
