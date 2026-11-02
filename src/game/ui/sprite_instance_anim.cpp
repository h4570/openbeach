#include "game/ui/scripted_sound_runtime.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/ui/sprite_animation.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using Sprite = ui::Sprite;
namespace anim = sprite_animation;

test::CallCounter g_calls("ui.sprite-instance-anim");

Anim* CurrentAnim(const Instance* instance)
{
    return instance->m_sprite->m_anims[instance->m_animIndex];
}

struct SoundSink {
    int calls = 0;
};

void PlaySound(Instance* instance, int animIndex, const float* world, SoundSink* sink)
{
    if (sink) {
        ++sink->calls;
        return;
    }
    instance->m_triggeredSound = game::GetScriptedSound()->PlaySpriteSound(instance->m_name, animIndex, world);
}

double SetAnimByNameImpl(Instance* instance, const char* name, float startTime, const float* world, SoundSink* sink)
{
    const Sprite* sprite = instance->m_sprite;
    const int index = anim::FindAnim(sprite->m_animCount, [&](int i) { return sprite->m_anims[i]->m_name; }, name);
    if (instance->m_animIndex != index) {
        instance->m_animIndex = index;
        instance->m_frameIndex = 0;
    }
    if (world) PlaySound(instance, index, world, sink);
    instance->m_startTime = startTime;
    return anim::Duration(sprite->m_anims[index]->m_frames, instance->m_fps);
}

// Both Update variants; `world` null (and `sink` unused) for the variant without sound.
void UpdateAnimImpl(Instance* instance, float time, const float* world, bool withSound, SoundSink* sink)
{
    Anim* current = CurrentAnim(instance);
    const anim::Step step =
            anim::Advance(instance->m_startTime, time, instance->m_animType, instance->m_fps, instance->m_frameIndex, current->m_frames);
    instance->m_startTime = step.startTime;
    if (withSound && step.wrapped && instance->m_triggeredSound && world) PlaySound(instance, instance->m_animIndex, world, sink);
    instance->m_frameIndex = step.frame;
    if (anim::StampsAnimation(time)) current->m_lastUsedTime = time;
}

void SetDirectionImpl(Instance* instance, float x, float y)
{
    instance->m_direction = anim::DirectionFromVector(x, y, CurrentAnim(instance)->m_directions);
}

void FaceTowardsImpl(Instance* instance, float targetX, float targetY, float fromX, float fromY)
{
    const anim::Vector2 v = anim::FaceVector(targetX, targetY, fromX, fromY);
    SetDirectionImpl(instance, v.x, v.y);
}

} // namespace

float SpriteInstance::SetAnimByName(const char* name, float startTime, const float* world)
{
    g_calls.Count();
    return static_cast<float>(SetAnimByNameImpl(this, name, startTime, world, nullptr));
}

void SpriteInstance::UpdateAnimNoSound(float time)
{
    g_calls.Count();
    UpdateAnimImpl(this, time, nullptr, false, nullptr);
}

void SpriteInstance::UpdateAnim(float time, const float* world)
{
    g_calls.Count();
    UpdateAnimImpl(this, time, world, true, nullptr);
}

void SpriteInstance::SetDirectionFromVector(float x, float y)
{
    g_calls.Count();
    SetDirectionImpl(this, x, y);
}

void SpriteInstance::FaceTowards(float targetX, float targetY, float, float fromX, float fromY, float)
{
    g_calls.Count();
    FaceTowardsImpl(this, targetX, targetY, fromX, fromY);
}

} // namespace obl::ui
