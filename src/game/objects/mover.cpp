#include "game/objects/object_moving.h"

#include "game/objects/mover_access.h"
#include "game/objects/mover_env.h"
#include "game/objects/mover_events.h"
#include "game/objects/mover_lifecycle.h"
#include "core/call_counter.h"

#include <bit>
#include <cstdint>

// The per-frame update and the teleport of every moving class (the logic is mover_events.h and
// mover_lifecycle.h; mover_env.h binds it to the game).
namespace obl::object {

namespace {
test::CallCounter g_update("objects.moving-update"), g_teleport("objects.moving-teleport");
} // namespace

bool Mover::Update()
{
    object::MoverEnv env;
    g_update.Count();
    return moving::Update(*this, env);
}

// Floats travel as raw bits so a signalling NaN is never quieted on the way.
void Mover::Teleport(float x, float y, float z)
{
    g_teleport.Count();
    object::MoverEnv env;
    moving::lifecycle::Teleport(*this, std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y), std::bit_cast<uint32_t>(z), env);
}

void SetMovingFlag(Base& object, bool moving)
{
    moving::SetMoving(static_cast<Mover&>(object), moving);
}
} // namespace obl::object
