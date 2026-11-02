#pragma once

#include "core/ascii_case.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// The pure rules of the guest's PlayAnimation (guest animation, docs/architecture/objects.md): which animation a name stands for, the sprite's animation type, the
// copy the guest keeps for "go back to" and the two messages. The game side (the sprite, the sends) is in src/game/objects/guest_animation.cpp.
namespace obl::guest_anim {

// The names the script and the guest functions pass instead of an animation. The first four are compared exactly, the two fidgets without regard to case.
constexpr char kMessData[] = "MessData"; // the animation named by the message being handled (its data)
constexpr char kWalk[] = "WalkAnimation";
constexpr char kStand[] = "StandAnimation";
constexpr char kReturn[] = "ReturnAnimation";
constexpr char kFidget1[] = "FidgetAnimation1";
constexpr char kFidget2[] = "FidgetAnimation2";
constexpr char kStandNormal[] = "D_STAND_NORMAL"; // a stand animation that has no fidgets of its own: they hang on "D_STAND"
constexpr char kStandBase[] = "D_STAND";
constexpr char kFidgetSuffix1[] = "_FIDGET1";
constexpr char kFidgetSuffix2[] = "_FIDGET2";

// SpriteInstance::m_animType values PlayAnimation sets (reverse wins over looping).
constexpr int32_t kTypeLoop = 0;
constexpr int32_t kTypeReverse = 1;
constexpr int32_t kTypeOnce = 2;

constexpr int32_t kEndMessageType = 3; // MT_STATECHANGE
constexpr int32_t kEndStateDefault = 0x15; // the state the guest changes to when the animation is replaced by another and is not in it yet
constexpr int32_t kNoState = -1; // `exitState` meaning "the current state"
constexpr float kNoDuration = 1.0f; // what a zero (or unordered) duration of the sprite becomes

// The longest result of a fidget name: the 40-byte stand animation, "_FIDGET1", the terminator.
constexpr size_t kScratchSize = 40 + 8 + 1;

inline bool Equal(const char* a, const char* b)
{
    return std::strcmp(a, b) == 0;
}

// The guest's three animation names and the name carried by the message being handled.
struct Names {
    const char* messData;
    const char* walk;
    const char* stand;
    const char* ret;
};

// The animation `name` stands for. The fidget names are built in `scratch` (one static buffer); `ReturnAnimation` is the guest's own
// buffer, which the caller then copies onto itself.
inline const char* Resolve(const char* name, const Names& names, char (&scratch)[kScratchSize])
{
    if (Equal(name, kMessData)) name = names.messData;
    if (Equal(name, kWalk)) return names.walk;
    if (Equal(name, kStand)) return names.stand;
    if (Equal(name, kReturn)) return names.ret;
    const char* suffix = ascii::EqualsNoCase(name, kFidget1) ? kFidgetSuffix1
            : ascii::EqualsNoCase(name, kFidget2)            ? kFidgetSuffix2
                                                             : nullptr;
    if (!suffix) return name;
    const char* base = ascii::EqualsNoCase(names.stand, kStandNormal) ? kStandBase : names.stand;
    std::strcpy(scratch, base);
    std::strcat(scratch, suffix);
    return scratch;
}

// The animation to come back to when the played one is not looped: the name itself unless a `finish` is given, which may name the walk or stand animation.
inline const char* ReturnTo(const char* name, const char* finish, const Names& names)
{
    if (!finish) return name;
    if (Equal(finish, kWalk)) return names.walk;
    if (Equal(finish, kStand)) return names.stand;
    return finish;
}

inline int32_t AnimType(bool reverse, bool loop)
{
    return reverse ? kTypeReverse : loop ? kTypeLoop : kTypeOnce;
}

// Compared with zero for equality, unordered included: a zero duration and a NaN (unordered) both become one.
inline float Duration(float fromSprite)
{
    return (fromSprite < 0.0f || fromSprite > 0.0f) ? fromSprite : kNoDuration;
}

// The state the guest is brought to when the animation ends (`exitState` -1 = where it is now).
inline int32_t LeaveState(int32_t exitState, int32_t currentState)
{
    return exitState == kNoState ? currentState : exitState;
}

// Not looped, and the return animation is no longer the played one (and the guest is not in state 0x15 yet): an immediate state change to 0x15.
inline bool SendsEndState(bool sameAnimation, int32_t currentState)
{
    return !sameAnimation && currentState != kEndStateDefault;
}

// Not looped, and either the guest is leaving for another state or the return animation differs: a delayed state change to `leaveState` after the animation.
inline bool SendsDelayedLeave(int32_t leaveState, int32_t currentState, bool sameAnimation)
{
    return leaveState != currentState || !sameAnimation;
}

} // namespace obl::guest_anim
