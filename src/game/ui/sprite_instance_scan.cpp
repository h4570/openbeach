#include "game/ui/sprite_anim.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/ui/sprite_scan.h"
#include "core/call_counter.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using Sprite = ui::Sprite;
constexpr int kTypeBuilding = 5;

// Call counters (`call-report ui.sprite-instance-scan`).
test::CallCounter g_calls("ui.sprite-instance-scan");

const uint8_t* FrameOf(const Instance* instance, Anim* anim)
{
    return anim->GetFrame(instance->m_frameIndex, instance->m_direction);
}

void FindFirstOpaque(const Instance* instance, int* outX, int* outY, bool* found = nullptr)
{
    const Sprite* sprite = instance->m_sprite;
    Anim* frameAnim = sprite->m_anims[sprite->m_type == kTypeBuilding ? 0 : instance->m_animIndex];
    const uint8_t* frame = FrameOf(instance, frameAnim);
    const Anim* size = sprite->m_anims[instance->m_animIndex];
    sprite_scan::Opaque opaque;
    const bool hit = sprite_scan::FindFirstOpaque(frame, size->m_width, size->m_height, &opaque);
    if (hit) {
        *outX = opaque.x;
        *outY = opaque.y;
    }
    if (found) *found = hit;
}

// The first animation whose name is exactly "pick" (-1 when the sprite has none).
int FindPickAnim(const Sprite* sprite)
{
    for (int i = 0; i < sprite->m_animCount; ++i)
        if (std::strcmp(sprite->m_anims[i]->m_name, sprite_scan::kPickAnimName) == 0) return i;
    return -1;
}

// What a HitTest call will do to the instance and what it answers; computed without changing the instance.
struct HitPlan {
    int animIndex;
    bool maskOnly;
    bool result;
};

HitPlan PlanHit(const Instance* instance, int x, int y)
{
    const Sprite* sprite = instance->m_sprite;
    HitPlan plan{instance->m_animIndex, false, false};
    if (sprite->m_type == kTypeBuilding) {
        const int pick = FindPickAnim(sprite);
        if (pick >= 0) {
            plan.animIndex = pick;
            plan.maskOnly = true;
        }
    }
    Anim* anim = sprite->m_anims[plan.animIndex];
    plan.result = sprite_scan::HitTest(FrameOf(instance, anim), anim->m_width, x, y, plan.maskOnly);
    return plan;
}

bool HitTestImpl(Instance* instance, int x, int y)
{
    const HitPlan plan = PlanHit(instance, x, y);
    instance->m_animIndex = plan.animIndex;
    if (instance->m_sprite->m_type == kTypeBuilding)
        instance->m_sprite->m_anims[plan.animIndex]->m_lastUsedTime = game::GetWorld()->m_gameTime;
    return plan.result;
}

} // namespace

void SpriteInstance::FindFirstOpaque(int* x, int* y) const
{
    g_calls.Count();
    ::obl::ui::FindFirstOpaque(this, x, y);
}

bool SpriteInstance::HitTest(int x, int y)
{
    g_calls.Count();
    return HitTestImpl(this, x, y);
}

} // namespace obl::ui
