#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/map/path_queries.h"
#include "game/map/path_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_moving.h"
#include "game/objects/mover_lifecycle.h"
#include "game/objects/object_base.h"
#include "core/call_counter.h"

#include <cstdint>
#include "game/objects/script_value_convert.h"

// Construction, teardown and script values of every moving class (mover_lifecycle.h has the logic and the contract of the Env).
namespace obl::object {
using game::Rand;

namespace {
using script::ScriptValue;
namespace life = moving::lifecycle;

test::CallCounter g_construct("objects.moving-construct"), g_destruct("objects.moving-destruct"), g_getValue("objects.moving-get-value"),
        g_setValue("objects.moving-set-value");

class LifeEnv {
public:
    int Rand() { return game::Rand(); }
    void ReleaseCursor(path::RouteCursor* cursor) { path::ReleaseCursor(cursor); }
    object::Base* Object(int32_t id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    void ServePosition(object::Base* building, float out[2])
    {
        float point[3] = {0, 0, 0};
        const float* at = object::building_api::GetServePosition(static_cast<const Building*>(building), point);
        out[0] = at[0];
        out[1] = at[1];
    }
    ScriptValue Base(Mover& object, int32_t key, intptr_t data)
    {
        ScriptValue out{};
        object.object::Base::GetValue(&out, key, data);
        return out;
    }
    void Base(Mover& object, int32_t key, intptr_t data, int32_t type, int32_t bits)
    {
        object.object::Base::SetValue(key, data, type, bits);
    }
    int32_t ToInt(int32_t type, int32_t bits) { return script_value::ToInt(type, static_cast<uint32_t>(bits)); }
};

} // namespace

Mover::Mover(const char* name) : Base(name)
{
    g_construct.Count();
    LifeEnv env;
    life::Construct(*this, env);
}

Mover::~Mover()
{
    g_destruct.Count();
    LifeEnv env;
    life::Destruct(*this, env);
}

ScriptValue* Mover::GetValue(ScriptValue* out, int key, intptr_t data)
{
    g_getValue.Count();
    LifeEnv env;
    *out = life::GetValue(*this, key, data, env);
    return out;
}

void Mover::SetValue(int key, intptr_t data, int type, int bits)
{
    g_setValue.Count();
    LifeEnv env;
    life::SetValue(*this, key, data, type, bits, env);
}
} // namespace obl::object
