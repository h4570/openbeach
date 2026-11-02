#pragma once

#include "game/objects/performer_states.h"
#include "game/objects/performer_variant.h"

namespace obl::performer {

// ReceiveMessage's external calls use the state Env plus Rand, HumanReceive and PostGuests.
template<class Env> uint8_t ReceiveMessage(object::Aerobic& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    SpriteInstance* sprite = detail::Sprite(performer);
    if (message.type == MT_MOVEIMHERE) {
        if (performer.m_state == 0) {
            const float length = env.SetAnim(sprite, "stand_to_aerobic", performer.m_WorldXYZ);
            sprite->m_animType = 2;
            performer.m_actionState = 0;
            auto& building = env.Building(performer.m_OwnerID);
            sprite->m_direction = detail::ClampDirection(*sprite, (11 - 2 * detail::Rotation(building)) % 8);
            env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
        } else if (performer.m_state == 6) {
            performer.m_waitingForDeath = 1;
            auto done = detail::SelfMessage(performer, MT_EVENT_FINISHED, 0, 0);
            done.receiver = performer.m_OwnerID;
            env.Post(done);
        }
        return 1;
    }
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finish = 1;
        return 1; // No defined answer here; callers ignore it.
    }
    if (message.type != MT_DO_NEXT_ACTION) return env.HumanReceive(performer, message);
    if (performer.m_finish && performer.m_actionState != 0) {
        env.Post(detail::SelfMessage(performer, MT_STATECHANGE, 0, 6));
        return 1;
    }
    int animation;
    const char* name;
    if (performer.m_finish) {
        animation = 4;
        name = "Aerobic_To_Stand";
    } else {
        animation = env.Rand() % 3;
        constexpr const char* names[] = {"Aerobic1", "Aerobic2", "Aerobic3"};
        name = names[animation];
    }
    const float length = env.SetAnim(sprite, name, performer.m_WorldXYZ);
    sprite->m_animType = 1;
    if (performer.m_finish) performer.m_actionState = 1;
    env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
    auto guests = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, animation);
    guests.extra = 0x40;
    env.PostGuests(env.Building(performer.m_OwnerID), guests, 0.0f);
    return 1;
}

template<class Env> uint8_t ReceiveMessage(object::Catwalk& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    auto* sprite = detail::Sprite(performer);
    auto timer = [&](const char* name, int type) {
        const float length = env.SetAnim(sprite, name, performer.m_WorldXYZ);
        sprite->m_animType = 2;
        env.PostDelayed(detail::SelfMessage(performer, type, 0, 0), length);
    };
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finished = 1;
        return 1; // No defined answer; callers ignore it.
    }
    if (message.type == MT_MOVEIMHERE && performer.m_state == 6) {
        if (performer.m_variant == 0) {
            auto done = detail::SelfMessage(performer, MT_EVENT_FINISHED, 0, 0);
            done.receiver = performer.m_OwnerID;
            env.Post(done);
        }
        performer.m_waitingForDeath = 1; // After Post: the owner's immediate handler can observe the old flag.
        return 1;
    }
    if ((message.type == MT_MOVEIMHERE || message.type == MT_PLAYANIMATION) && performer.m_state == 0) {
        constexpr const char* names[] = {"D_CATWALK_WAIT1", "D_CATWALK_WAIT2", "D_CATWALK_WAIT3"};
        const float length = env.SetAnim(sprite, names[env.Rand() % 3], performer.m_WorldXYZ);
        sprite->m_direction = detail::ClampDirection(*sprite, (11 - 2 * detail::Rotation(env.Building(performer.m_OwnerID))) % 8);
        sprite->m_animType = 2;
        env.PostDelayed(detail::SelfMessage(performer, MT_PLAYANIMATION, 0, 0), length);
        return 1;
    }
    if (message.type == MT_PLAYANIMATION) return 1;
    if (message.type == MT_MOVEIMHERE) {
        if (performer.m_actionState == 0) {
            const float length = env.SetAnim(sprite, "D_CATWALK_POSE", performer.m_WorldXYZ);
            sprite->m_animType = 2;
            sprite->m_direction = detail::ClampDirection(*sprite, (11 - 2 * detail::Rotation(env.Building(performer.m_OwnerID))) % 8);
            performer.m_actionState = 1;
            env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
        }
        return 1;
    }
    if (message.type != MT_DO_NEXT_ACTION) return env.HumanReceive(performer, message);
    if (performer.m_actionState == 1) {
        const int draw = env.Rand() % 7;
        performer.m_actionState = draw == 6 ? 3 : 2;
        timer(draw < 3 ? "D_CATWALK_POSE1" : draw < 6 ? "D_CATWALK_POSE2" : "D_CATWALK_TRIP1", MT_DO_NEXT_ACTION);
    } else if (performer.m_actionState == 2) {
        const int draw = env.Rand() % 2;
        const char* name = draw == 0 ? "D_CATWALK_POSE1" : "D_CATWALK_POSE2";
        const float length = env.SetAnim(sprite, name, performer.m_WorldXYZ);
        sprite->m_animType = 2;
        performer.m_actionState = 3;
        env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
    } else if (performer.m_finished) {
        auto state = detail::SelfMessage(performer, MT_STATECHANGE, 0, 6);
        for (auto* actor : env.Catwalks()) {
            if (actor->m_OwnerID != performer.m_OwnerID) continue;
            state.receiver = static_cast<int32_t>(actor->m_id);
            env.Post(state);
        }
    } else {
        auto state = detail::SelfMessage(performer, MT_STATECHANGE, 0, 0);
        env.Post(state);
        for (auto* actor : env.Catwalks()) {
            if (actor->m_OwnerID == performer.m_OwnerID && actor->m_variant == (performer.m_variant + 1) % 3) {
                state.receiver = static_cast<int32_t>(actor->m_id);
                state.data = 4;
                break;
            }
        }
        env.Post(state); // No next actor: state 0 goes to itself a second time.
    }
    return 1;
}

template<class Env> uint8_t ReceiveMessage(object::WeightLifter& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    auto* sprite = detail::Sprite(performer);
    constexpr const char* wait[] = {"D_WEIGHT_THIN_WAIT", "D_WEIGHT_FAT_WAIT", "D_WEIGHT_BIG_WAIT"};
    constexpr const char* pose[] = {"D_WEIGHT_THIN_POSE", "D_WEIGHT_FAT_POSE", "D_WEIGHT_BIG_POSE"};
    constexpr const char* lift[] = {"D_WEIGHT_THIN_WEIGHT", "D_WEIGHT_FAT_WEIGHT", "D_WEIGHT_BIG_WEIGHT"};
    constexpr const char* bar[] = {"D_WEIGHT_THINBAR_WEIGHT", "D_WEIGHT_FATBAR_WEIGHT", "D_WEIGHT_BIGBAR_WEIGHT"};
    constexpr const char* walk[] = {"D_WEIGHT_THIN_TOWALK", "D_WEIGHT_FAT_TOWALK", "D_WEIGHT_BIG_TOWALK"};
    auto guestAnimation = [&](int animation, float delay) {
        auto guests = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, animation);
        guests.extra = 0x3e;
        env.PostGuests(env.Building(performer.m_OwnerID), guests, delay);
    };
    auto face = [&] {
        sprite->m_direction = detail::ClampDirection(*sprite, (11 - 2 * detail::Rotation(env.Building(performer.m_OwnerID))) % 8);
    };
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finished = 1;
        return 1; // No defined answer; callers ignore it.
    }
    if (message.type == MT_MOVEIMHERE) {
        if (performer.m_state == 0) {
            if (performer.m_variant < 3) env.SetAnim(sprite, wait[performer.m_variant], performer.m_WorldXYZ);
            face();
            sprite->m_animType = 0;
        } else if (performer.m_state == 6) {
            if (performer.m_variant == 0) {
                auto done = detail::SelfMessage(performer, MT_EVENT_FINISHED, 0, 0);
                done.receiver = performer.m_OwnerID;
                env.Post(done);
            }
            performer.m_waitingForDeath = 1;
        } else if (performer.m_actionState == 0 && performer.m_variant < 3) {
            const float length = env.SetAnim(sprite, pose[performer.m_variant], performer.m_WorldXYZ);
            sprite->m_animType = 2;
            face();
            performer.m_actionState = 1;
            env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
        }
        return 1;
    }
    if (message.type != MT_DO_NEXT_ACTION) return env.HumanReceive(performer, message);
    float length;
    if (performer.m_actionState == 0) {
        performer.m_actionState = 2;
        length = env.SetAnim(sprite, "D_WEIGHT_BIG_JOY", performer.m_WorldXYZ);
        guestAnimation(2, 0.0f);
        // Unlike the other action branches, JOY leaves the actor's animation type untouched.
    } else if (performer.m_actionState == 1 && performer.m_variant < 3) {
        if (performer.m_variant != 2)
            performer.m_actionState = 2;
        else
            performer.m_actionState = 0;
        length = env.SetAnim(sprite, lift[performer.m_variant], performer.m_WorldXYZ);
        env.SetAnim(static_cast<SpriteInstance*>(performer.m_attachedSprite), bar[performer.m_variant], performer.m_WorldXYZ);
        if (performer.m_variant != 2) guestAnimation(1, length);
        sprite->m_animType = 2;
    } else if (performer.m_actionState == 2 && performer.m_variant < 3) {
        length = env.SetAnim(sprite, walk[performer.m_variant], performer.m_WorldXYZ);
        sprite->m_animType = 2;
        performer.m_actionState = 3;
    } else if (performer.m_actionState == 1 || performer.m_actionState == 2) {
        return 1; // Invalid variants have no delay; factories create only 0..2.
    } else {
        auto state = detail::SelfMessage(performer, MT_STATECHANGE, 0, performer.m_finished ? 6 : 0);
        if (performer.m_finished) {
            for (auto* actor : env.WeightLifters()) {
                if (actor->m_OwnerID != performer.m_OwnerID) continue;
                state.receiver = static_cast<int32_t>(actor->m_id);
                env.Post(state);
            }
        } else {
            env.Post(state);
            for (auto* actor : env.WeightLifters()) {
                if (actor->m_OwnerID == performer.m_OwnerID && actor->m_variant == (performer.m_variant + 1) % 3) {
                    state.receiver = static_cast<int32_t>(actor->m_id);
                    state.data = 4;
                    break;
                }
            }
            env.Post(state);
        }
        return 1;
    }
    env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
    return 1;
}

template<class Env> uint8_t ReceiveMessage(object::Singer& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    auto* sprite = detail::Sprite(performer);
    auto* mic = static_cast<SpriteInstance*>(performer.m_attachedSprite);
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finish = 1;
        return 1; // No defined answer; callers ignore it.
    }
    if (message.type == MT_MOVEIMHERE) {
        if (performer.m_state == 0) {
            const float length = env.SetAnim(sprite, "SING_WAVE", performer.m_WorldXYZ);
            sprite->m_animType = mic->m_animType = 2;
            performer.m_actionState = 0;
            sprite->m_direction = detail::ClampDirection(*sprite, (11 - 2 * detail::Rotation(env.Building(performer.m_OwnerID))) % 8);
            env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
        } else if (performer.m_state == 6) {
            performer.m_waitingForDeath = 1;
            auto done = detail::SelfMessage(performer, MT_EVENT_FINISHED, 0, 0);
            done.receiver = performer.m_OwnerID;
            env.Post(done);
        }
        return 1;
    }
    if (message.type != MT_DO_NEXT_ACTION) return env.HumanReceive(performer, message);
    const char* body;
    const char* microphone = "SINGMIC_STILL";
    switch (performer.m_actionState) {
    case 0:
        performer.m_actionState = performer.m_finish ? 8 : 1;
        body = performer.m_finish ? "SING_NORM_BOW" : "SING_NORM";
        break;
    case 1: {
        const int draw = env.Rand() % 4;
        constexpr const char* names[] = {"SING_NORM", "SING_NORM_CROUCH", "SING_NORM_LIFT", "SING_NORM_KNEE"};
        constexpr int states[] = {1, 2, 4, 6};
        body = names[draw];
        performer.m_actionState = states[draw];
        if (draw == 1) microphone = "SINGMIC_NORM_CROUCH";
        break;
    }
    case 2:
        performer.m_actionState = 3;
        body = "SING_CROUCH";
        microphone = "SINGMIC_CROUCH";
        break;
    case 3:
        performer.m_actionState = 0;
        body = "SING_CROUCH_NORM";
        microphone = "SINGMIC_CROUCH_NORM";
        break;
    case 4:
        performer.m_actionState = 5;
        body = "SING_LIFT";
        microphone = "SINGMIC_LIFT";
        break;
    case 5:
        performer.m_actionState = 0;
        body = "SING_LIFT_NORM";
        microphone = "SINGMIC_LIFT_NORM";
        break;
    case 6:
        performer.m_actionState = 7;
        if (env.Rand() % 2 == 0) {
            body = "SING_KNEE_POINT";
            microphone = "SINGMIC_KNEE_POINT";
        } else {
            body = "SING_KNEE_LEAN";
            microphone = "SINGMIC_KNEE_LEAN";
        }
        break;
    case 7:
        performer.m_actionState = 0;
        body = "SING_KNEE_NORM";
        microphone = "SINGMIC_KNEE_NORM";
        break;
    case 8:
        performer.m_actionState = 0;
        env.Post(detail::SelfMessage(performer, MT_STATECHANGE, 0, 6));
        return 1;
    default: return 1;
    }
    const float length = env.SetAnim(sprite, body, performer.m_WorldXYZ);
    env.SetAnim(mic, microphone, performer.m_WorldXYZ);
    env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
    return 1;
}

template<class Env> uint8_t ReceiveMessage(object::Cabaret& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    auto* sprite = detail::Sprite(performer);
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finish = 1;
        return 1; // No defined answer; callers ignore it.
    }
    if (message.type == MT_MOVEIMHERE) {
        if (performer.m_actionState == 3) {
            auto next = detail::SelfMessage(performer, MT_STATECHANGE, 0, 1);
            if (performer.m_finish) {
                performer.m_waitingForDeath = 1;
                next.receiver = performer.m_OwnerID;
                next.type = MT_EVENT_FINISHED;
                next.data = 0;
            } else {
                env.ReleaseSprite(sprite);
                performer.m_sprite = nullptr;
                const int variant = env.Rand() % 2;
                SetVariant(performer, variant, env);
            }
            env.Post(next);
        } else if (performer.m_actionState == 0) {
            auto& building = env.Building(performer.m_OwnerID);
            const float length = env.SetAnim(sprite, "D_CABARET_TO_WALK", performer.m_WorldXYZ);
            const int rotation = detail::Rotation(building);
            sprite->m_direction = detail::ClampDirection(*sprite, rotation == 0 ? 4 : rotation == 3 ? 6 : ((6 - rotation) * 2) % 8);
            sprite->m_animType = 1;
            env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
        }
        return 1;
    }
    if (message.type != MT_DO_NEXT_ACTION) return env.HumanReceive(performer, message);
    const int action = performer.m_actionState;
    if (action < 0 || action > 2) return 1;
    auto& building = env.Building(performer.m_OwnerID);
    const int rotation = detail::Rotation(building);
    float point[2];
    if (rotation == 0 || rotation == 3) {
        const double x = action == 2 ? (rotation == 0 ? 5.0 : 102.0) : (rotation == 0 ? -21.0 : 33.0);
        const double y = action == 2 ? (rotation == 0 ? -364.0 : -286.0) : (rotation == 0 ? -322.0 : -308.0);
        point[0] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[0]) + x);
        point[1] = static_cast<float>(static_cast<double>(building.m_WorldXYZ[1]) + y);
    } else {
        env.FootprintPoint(building, action == 2 ? 5.0f : -25.0f, action == 2 ? -364.0f : -282.0f, point);
    }
    if (action == 2) {
        env.SetAnim(sprite, "D_CABARET_WALK_CYCLE", performer.m_WorldXYZ);
        sprite->m_direction = detail::ClampDirection(*sprite, rotation == 0 ? 1 : 3);
        sprite->m_animType = 0;
        performer.m_actionState = 3;
        env.Post(detail::MoveMessage(performer, MT_MOVE2SEAPOINT, point[0], point[1]));
    } else {
        // Clamp against the previous animation, before selecting the next one.
        sprite->m_direction = detail::ClampDirection(*sprite,
                rotation == 0           ? 4
                        : rotation == 3 ? 6
                        : action == 0   ? (sprite->m_direction + 7) % 8
                                        : ((4 - rotation) * 2) % 8);
        const float length = env.SetAnim(sprite, action == 0 ? "D_CABARET" : "D_CABARET_TO_WALK", performer.m_WorldXYZ);
        sprite->m_animType = 2;
        env.Teleport(performer, point[0], point[1], 0.0f);
        performer.m_actionState = action + 1;
        env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length);
    }
    return 1;
}

// WetTShirt message handling: the class-18 list retains the manager's iteration order.
template<class Env> uint8_t ReceiveMessage(object::WetTShirt& performer, const messages::Message& message, Env& env)
{
    using namespace messages;
    using ui::SpriteInstance;
    using namespace object;
    auto* sprite = detail::Sprite(performer);
    // The building's rotation steers the facing; the clamp reads the animation the sprite plays at that moment, so some branches clamp before SetAnim.
    auto face = [&](int direction) { sprite->m_direction = detail::ClampDirection(*sprite, direction); };
    auto rotation = [&] { return detail::Rotation(env.Building(performer.m_OwnerID)); };
    auto timer = [&](float length) { env.PostDelayed(detail::SelfMessage(performer, MT_DO_NEXT_ACTION, 0, 0), length); };
    if (message.type == MT_EVENT_FINISHED) {
        performer.m_finished = 1;
        return 1; // No defined answer; callers ignore it.
    }
    if (message.type == MT_PLAYANIMATION) {
        if (performer.m_state == 4) {
            if (performer.m_actionState == 1)
                performer.m_actionState = 2;
            else if (performer.m_actionState == 0 || performer.m_actionState == 5) {
                env.SetAnim(sprite, performer.m_actionState == 0 ? "WTS_WALK_DRY" : "WTS_WALK_WET", performer.m_WorldXYZ);
                sprite->m_animType = 0;
            }
        } else if (performer.m_actionState == 5) {
            performer.m_Contestent = message.sender;
            performer.m_actionState = 6;
            env.SetAnim(sprite, "WTSC_WALK_NB", performer.m_WorldXYZ);
            sprite->m_animType = 0;
            auto& building = env.Building(performer.m_OwnerID);
            float point[2];
            const int turn = detail::Rotation(building);
            if (turn == 0 || turn == 3) {
                point[0] = building.m_WorldXYZ[0] + (turn == 0 ? 78.0f : 64.0f);
                point[1] = building.m_WorldXYZ[1] - (turn == 0 ? 354.0f : 234.0f);
            } else
                env.FootprintPoint(building, 78.0f, -354.0f, point);
            env.Post(detail::MoveMessage(performer, MT_MOVE2POINT, point[0], point[1]));
        } else if (performer.m_actionState == 8) {
            performer.m_actionState = 9;
            const float length = env.SetAnim(sprite, "WTSC_THROW", performer.m_WorldXYZ);
            auto guests = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, 2);
            guests.extra = 0x3e;
            env.PostGuests(env.Building(performer.m_OwnerID), guests, length);
            sprite->m_animType = 2;
            timer(length);
        }
        return 1;
    }
    if (message.type == MT_DO_NEXT_ACTION) {
        if (performer.m_state == 4) {
            constexpr const char* waits[] = {"WTS_WAIT1", "WTS_WAIT2", "WTS_WAIT3", "WTS_WAIT4", "WTS_WAIT5", "WTS_WAIT6"};
            constexpr const char* reacts[] = {"WTS_REACT1", "WTS_REACT2", "WTS_REACT3", "WTS_REACT4", "WTS_REACT5", "WTS_REACT6"};
            auto askCarrier = [&] {
                auto play = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, 0);
                for (auto* actor : env.WetTShirts())
                    if (actor->m_OwnerID == performer.m_OwnerID && actor->m_variant == 3) {
                        play.receiver = actor->m_id;
                        env.Post(play);
                    }
            };
            float length;
            switch (performer.m_actionState) {
            case 0:
                if (performer.m_moving) return 1;
                askCarrier();
                length = env.SetAnim(sprite, waits[env.Rand() % 6], performer.m_WorldXYZ);
                sprite->m_animType = 2;
                performer.m_actionState = 1;
                timer(length);
                return 1;
            case 1:
                length = env.SetAnim(sprite, waits[env.Rand() % 6], performer.m_WorldXYZ);
                sprite->m_animType = 2;
                timer(length);
                return 1;
            case 2:
                askCarrier();
                length = env.SetAnim(sprite, reacts[env.Rand() % 6], performer.m_WorldXYZ);
                sprite->m_animType = 2;
                performer.m_actionState = 3;
                timer(length);
                if (const int opinion = env.Rand() % 10; opinion == 0) {
                    auto guests = detail::SelfMessage(performer, MT_SPOT_OPINION, 0, 5);
                    guests.extra = 0x3e;
                    guests.floatData = static_cast<float>(opinion);
                    auto& building = env.Building(performer.m_OwnerID);
                    // The intermediate product and subtraction stays in double precision until the final float store.
                    const double jitter = static_cast<double>(env.Rand()) * static_cast<double>(1.0f / 32767.0f) - 0.5;
                    env.PostGuests(building, guests, static_cast<float>(jitter + static_cast<double>(length)));
                }
                return 1;
            case 3:
                performer.m_actionState = 4;
                length = env.SetAnim(sprite, "WTS_WAIT_TO_DANCE_WET", performer.m_WorldXYZ);
                timer(length);
                sprite->m_animType = 2;
                return 1;
            case 4: {
                constexpr const char* dances[] = {"WTS_DANCE1_WET", "WTS_DANCE2_WET", "WTS_DANCE3_WET"};
                length = env.SetAnim(sprite, dances[env.Rand() % 3], performer.m_WorldXYZ);
                sprite->m_animType = 2;
                performer.m_actionState = 5;
                timer(length);
                return 1;
            }
            case 5:
                performer.m_actionState = 6;
                length = env.SetAnim(sprite, "WTS_WAIT_TO_WALK_WET", performer.m_WorldXYZ);
                timer(length);
                sprite->m_animType = 2;
                return 1;
            case 6: {
                auto state = detail::SelfMessage(performer, MT_STATECHANGE, 0, performer.m_finished ? 6 : 0);
                if (performer.m_finished) {
                    for (auto* actor : env.WetTShirts())
                        if (actor->m_OwnerID == performer.m_OwnerID) {
                            state.receiver = actor->m_id;
                            env.Post(state);
                        }
                } else {
                    env.Post(state);
                    for (auto* actor : env.WetTShirts())
                        if (actor->m_OwnerID == performer.m_OwnerID &&
                                static_cast<int32_t>(actor->m_variant) == static_cast<int32_t>(performer.m_variant + 1) % 3) {
                            state.receiver = actor->m_id;
                            state.data = 4;
                            break;
                        }
                    env.Post(state); // With no next actor, send the same state 0 to performer again.
                }
                return 1;
            }
            default: return 1;
            }
        }
        if (performer.m_state != 0) return 1;
        const char* name = nullptr;
        switch (performer.m_actionState) {
        case 1: {
            const int draw = env.Rand() % 7;
            constexpr const char* names[] = {
                    "WTS_WAIT1", "WTS_WAIT2", "WTS_WAIT3", "WTS_WAIT4", "WTS_WAIT5", "WTS_WAIT6", "WTS_WAIT_TO_DANCE_DRY"};
            name = names[draw];
            if (draw == 6) performer.m_actionState = 2;
            break;
        }
        case 2: {
            const int draw = env.Rand() % 4;
            constexpr const char* names[] = {"WTS_DANCE1_DRY", "WTS_DANCE2_DRY", "WTS_DANCE3_DRY", "WTS_DANCE_TO_WAIT_DRY"};
            name = names[draw];
            if (draw == 3) performer.m_actionState = 1;
            break;
        }
        case 5: name = env.Rand() % 2 == 0 ? "WTSC_WAIT" : "WTSC_EXCITE"; break;
        case 7:
            performer.m_actionState = 8;
            env.SetAnim(sprite, "WTSC_WALK_WB", performer.m_WorldXYZ);
            sprite->m_animType = 0;
            detail::WalkToOffset(performer, env.Building(performer.m_OwnerID), -28.0f, -266.0f, env);
            return 1;
        case 9:
            performer.m_actionState = 5;
            name = "WTSC_WATCH";
            break;
        default: return 1;
        }
        const float length = env.SetAnim(sprite, name, performer.m_WorldXYZ);
        sprite->m_animType = 2;
        timer(length);
        return 1;
    }
    if (message.type != MT_MOVEIMHERE) return env.HumanReceive(performer, message);
    if (performer.m_state == 4) { // the show: walk to the wait pose, facing the stage
        const float length = env.SetAnim(sprite, "WTS_WALK_TO_WAIT_DRY", performer.m_WorldXYZ);
        sprite->m_animType = 2;
        face((13 - 2 * rotation()) % 8);
        timer(length);
        return 1;
    }
    if (performer.m_state != 0) {
        if (performer.m_state == 6) {
            if (performer.m_variant == 3) { // a contestant of the "wtsc" sheet reports to the owner
                auto done = detail::SelfMessage(performer, MT_EVENT_FINISHED, 0, 0);
                done.receiver = performer.m_OwnerID;
                env.Post(done);
            }
            performer.m_waitingForDeath = 1;
        }
        return 1;
    }
    if (performer.m_variant != 3) {
        face((11 - 2 * rotation()) % 8); // before the animation: clamped against the previous one
        performer.m_actionState = 1;
        const float length = env.SetAnim(sprite, "WTS_WALK_TO_WAIT_DRY", performer.m_WorldXYZ);
        sprite->m_animType = 2;
        timer(length);
        return 1;
    }
    float length = 0.0f;
    switch (performer.m_actionState) {
    case 5:
        if (const int draw = env.Rand() % 2; draw == 0)
            length = env.SetAnim(sprite, "WTSC_WAIT", performer.m_WorldXYZ);
        else if (draw == 1)
            length = env.SetAnim(sprite, "WTSC_EXCITE", performer.m_WorldXYZ);
        face((5 - rotation()) * 2 % 8);
        sprite->m_animType = 2;
        timer(length);
        return 1;
    case 6: {
        performer.m_actionState = 7;
        length = env.SetAnim(sprite, "WTSC_PICKUP", performer.m_WorldXYZ);
        auto guests = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, 1);
        guests.extra = 0x3e;
        env.PostGuests(env.Building(performer.m_OwnerID), guests, length);
        sprite->m_animType = 2;
        timer(length);
        return 1;
    }
    case 8: {
        length = env.SetAnim(sprite, "WTSC_WALK_TO_POSE", performer.m_WorldXYZ);
        sprite->m_animType = 2;
        face((5 - rotation()) * 2 % 8);
        auto turn = detail::SelfMessage(performer, MT_PLAYANIMATION, 0, 0);
        turn.receiver = performer.m_Contestent;
        env.PostDelayed(turn, length);
        return 1;
    }
    default: return 1;
    }
}

} // namespace obl::performer
