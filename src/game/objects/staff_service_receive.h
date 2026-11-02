#pragma once

#include "game/messages/message_state.h"
#include "core/ascii_case.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// The service staff `ReceiveMessage`. The member behind a counter is a small
// animation machine driven by messages it mostly posts to itself: `m_actionState` is the step, the delay of the next message is the length of the
// animation just started (starting an animation answers it). Which machine runs depends on the sprite of the staff definition (bar,
// kitchen) or, for MT_DO_NEXT_ACTION, on nothing at all (only a DJ is ever sent it). The base class is not called: the script messages a profession
// gets (the staff `ReceiveMessage`) never reach service staff, and what the switch does not know goes to `ReceiveMovementMessage`.
//
// The answer byte is the low byte of the message pointer, or of the last value the handler stored in its place (the frame count of
// `catch_burger`, the bits of an animation's length or of the game clock); tests pin it.
//
// Env:
//   env.PlayAnim(name)             start the named animation at the game time and position: the animation's length
//   env.Send(message)              virtual Send
//   env.SendDelayed(message, s)    virtual SendDelayed
//   env.Rand()                     the game's rand()
//   env.Cash()                     Player::m_cash
//   env.GameTime()                 World::m_gameTime
//   env.Movement(message)          ReceiveMovementMessage, its answer
namespace obl::staff::service {

// m_actionState of the bar: 0 idle, 1 serving, 3..5 the glass polishing started by MT_LOOK_AROUND.
constexpr int32_t kBarIdle = 0, kBarServing = 1, kBarPickedGlass = 3, kBarCleanedGlass = 4, kBarReplacedGlass = 5;
constexpr float kServeDelay = 1.0f, kLookAroundPeriod = 20.0f;
constexpr double kDjDanceRepeats = 20.0; //: the dance loop is held for twenty lengths of the animation

enum class Counter { None, Bar, Kitchen };

// `strcmp`, so the case matters; the female cook is asked for with three F's, which no sprite has (the constructor's "CHEFFETTE" has two).
inline Counter CounterOf(const char* sprite)
{
    if (ascii::EqualsNoCase(sprite, "BARMAN") || ascii::EqualsNoCase(sprite, "BARWOMAN")) return Counter::Bar;
    if (ascii::EqualsNoCase(sprite, "CHEF") || ascii::EqualsNoCase(sprite, "CHEFFFETTE")) return Counter::Kitchen;
    return Counter::None;
}

// One step of the DJ's round: the sprite's animation type, the animation, and whether its message waits twenty lengths.
struct DjStep {
    int32_t animType;
    const char* anim;
    bool held;
};
inline constexpr DjStep kDjSteps[] = {
        {0, "Stand_dance1", true},
        {1, "Stand_to_dance1", false},
        {2, "Stand_to_mic", false},
        {2, "Stand_que_deck1", false},
        {2, "Stand_mix", false},
        {2, "Stand_flip_deck2", false},
        {2, "Stand_to_dance1", false},
        {0, "Stand_dance1", true},
        {1, "Stand_to_dance1", false},
        {2, "Stand_to_mic", false},
        {2, "Stand_que_deck2", false},
        {2, "Stand_mix", false},
        {2, "Stand_flip_deck1", false},
        {2, "Stand_to_dance1", false},
};
constexpr uint32_t kDjStepCount = sizeof(kDjSteps) / sizeof(kDjSteps[0]);

inline uint8_t LowByte(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<uint8_t>(bits);
}
inline uint8_t LowByte(const messages::Message& message)
{
    return static_cast<uint8_t>(reinterpret_cast<uintptr_t>(&message));
}

// The words after data are zero, but for MT_SERVE_GUEST, which copies floatData.
inline messages::Message ToSelf(const object::ServiceStaff& employee, int type, int data)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(employee.m_id);
    message.receiver = static_cast<int32_t>(employee.m_id);
    message.type = type;
    message.data = data;
    return message;
}

// The frame count of the sprite's `catch_burger`, 0 when it has none.
inline uint16_t CatchBurgerFrames(const ui::Sprite& sprite)
{
    for (int32_t i = 0; i < sprite.m_animCount; ++i)
        if (!std::strcmp("catch_burger", sprite.m_anims[i]->m_name)) return sprite.m_anims[i]->m_frames;
    return 0;
}

// Starts `anim` and posts MT_PLAYANIMATION with `data` to the member itself when the animation ends; the answer is the low byte of the length.
template<class Env> uint8_t PlayThenContinue(object::ServiceStaff& employee, const char* anim, int data, Env& env)
{
    const float length = static_cast<float>(env.PlayAnim(anim));
    env.SendDelayed(ToSelf(employee, messages::MT_PLAYANIMATION, data), length);
    return LowByte(length);
}

template<class Env> uint8_t ReceiveMessage(object::ServiceStaff& employee, const messages::Message& message, Env& env)
{
    auto& sprite = *static_cast<ui::SpriteInstance*>(employee.m_sprite);
    const char* const spriteName = employee.m_staffDefinition->m_Sprite.c_str();
    uint8_t answer = LowByte(message);
    switch (message.type) {
    case messages::MT_SERVE_GUEST: {
        // The guest (data) gets its goods after a second; the cook's after the burger was caught.
        messages::Message served{};
        served.sender = static_cast<int32_t>(employee.m_id);
        served.receiver = message.data;
        served.type = messages::MT_SERVED_COMMOD;
        served.flags = message.flags;
        served.data = message.data;
        served.floatData = message.floatData;
        float delay = kServeDelay;
        if (ascii::EqualsNoCase(spriteName, "CHEF")) {
            const uint16_t frames = CatchBurgerFrames(*sprite.m_sprite);
            delay = static_cast<float>(static_cast<double>(frames) / static_cast<double>(sprite.m_fps) + 1.0);
            answer = static_cast<uint8_t>(frames);
        }
        env.SendDelayed(served, delay);
        if (employee.m_actionState != 0) env.PlayAnim("stand");
        employee.m_actionState = kBarServing;
        messages::Message next = ToSelf(employee, messages::MT_PLAYANIMATION, 1);
        next.floatData = message.floatData; // the record is reused
        env.SendDelayed(next, kServeDelay);
        return answer;
    }
    case messages::MT_NEW_DAY: {
        float& cash = env.Cash();
        cash = static_cast<float>(static_cast<double>(cash) - static_cast<double>(employee.m_Wage));
        return answer;
    }
    case messages::MT_COMMOD_QUALITY: {
        messages::Message served = ToSelf(employee, messages::MT_GUEST_SERVED, 0);
        served.receiver = employee.m_HomeID;
        env.Send(served);
        return answer;
    }
    case messages::MT_DO_NEXT_ACTION: {
        const uint32_t step = static_cast<uint32_t>(employee.m_actionState);
        if (step >= kDjStepCount) return answer;
        const DjStep& dj = kDjSteps[step];
        sprite.m_animType = dj.animType;
        const double length = env.PlayAnim(dj.anim);
        const float delay = dj.held ? static_cast<float>(length * kDjDanceRepeats) : static_cast<float>(length);
        env.SendDelayed(ToSelf(employee, messages::MT_DO_NEXT_ACTION, 0), delay);
        employee.m_actionState = static_cast<int32_t>((step + 1) % kDjStepCount);
        return dj.held ? LowByte(env.GameTime()) : LowByte(delay);
    }
    case messages::MT_PLAYANIMATION:
        switch (CounterOf(spriteName)) {
        case Counter::None: return answer;
        case Counter::Kitchen:
            if (message.data == 0) return PlayThenContinue(employee, "serve_burger", 2, env);
            if (message.data == 1) return PlayThenContinue(employee, "catch_burger", 0, env);
            env.PlayAnim("stand");
            return answer;
        case Counter::Bar: break;
        }
        switch (message.data) {
        case 1: employee.m_actionState = kBarServing; return PlayThenContinue(employee, "serve_drink", 2, env);
        case 3:
            if (employee.m_actionState != kBarPickedGlass) return answer;
            answer = PlayThenContinue(employee, env.Rand() % 2 == 0 ? "clean_glass" : "blow_glass", 4, env);
            employee.m_actionState = kBarCleanedGlass;
            return answer;
        case 4:
            if (employee.m_actionState != kBarCleanedGlass) return answer;
            answer = PlayThenContinue(employee, "replace_glass", 5, env);
            employee.m_actionState = kBarReplacedGlass;
            return answer;
        case 5:
            if (employee.m_actionState != kBarReplacedGlass) return answer;
            env.PlayAnim("stand");
            employee.m_actionState = kBarIdle;
            return answer;
        default:
            employee.m_actionState = kBarIdle;
            env.PlayAnim("stand");
            return answer;
        }
    case messages::MT_LOOK_AROUND:
        // Asked of itself every twenty seconds (the barman's OnEnterState starts it): an idle member picks a glass up.
        if (employee.m_actionState == kBarIdle) {
            employee.m_actionState = kBarPickedGlass;
            answer = PlayThenContinue(employee, "pick_up_glass", 3, env);
        }
        env.SendDelayed(ToSelf(employee, messages::MT_LOOK_AROUND, 0), kLookAroundPeriod);
        return answer;
    default: return env.Movement(message);
    }
}

} // namespace obl::staff::service
