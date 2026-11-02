#include "game/objects/staff_values.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/staff_roster.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "game/objects/staff_script_values.h"
#include "core/call_counter.h"
#include "game/objects/script_value_convert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;

namespace {

using object::Building;
using object::Staff;
using script::ScriptValue;
namespace ex = script_program::exec;

// Call counters (`call-report <name>`).
test::CallCounter g_get("objects.staff-get-value");
test::CallCounter g_set("objects.staff-set-value");

// ---- The game side of the Env of staff_script_values.h -----------------------------------------------------------------------------

using OpenFunc = bool (*)(const Building*);
using ServeFunc = float* (*)(const Building*, float*);
const OpenFunc kIsOpen{&building_api::IsOpen};
const ServeFunc kServePosition{&building_api::GetServePosition};

class GameEnv {
public:
    object::Base* Object(int32_t id) const { return object_manager::ObjectById(GetObjectManager(), id); }
    bool IsOpen(object::Base* building) const { return kIsOpen(static_cast<const Building*>(building)); }
    void ServePosition(object::Base* building, float& x, float& y) const
    {
        float point[3];
        const float* at = kServePosition(static_cast<const Building*>(building), point);
        x = at[0];
        y = at[1];
    }
    float GameDayTime() const { return GetWorld()->m_gameDayTime; }
    ScriptValue Base(Staff& member, int32_t key, intptr_t data) const
    {
        ScriptValue out{};
        member.Person::GetValue(&out, key, data);
        return out;
    }
    void Base(Staff& member, int32_t key, intptr_t data, int32_t type, int32_t bits) const
    {
        member.Person::SetValue(key, data, type, bits);
    }
    // A float converts to int through script_value::ToInt, the same conversion every script value uses.
    int32_t FloatToIntWrap(int32_t floatBits) const { return script_value::ToInt(ex::kTypeFloat, static_cast<uint32_t>(floatBits)); }
};

// ---- GetValue of the staff ----------------------------------------------------------------------------------------------------------

// A byte value defines only its low byte (the rest of the word is left as it was).
ScriptValue* RunGetValue(Staff* employee, ScriptValue* out, int32_t key, intptr_t data)
{
    g_get.Count();
    GameEnv env;
    *out = staff::GetValue(*employee, key, data, env);
    return out;
}

// ---- SetValue of the staff ----------------------------------------------------------------------------------------------------------

void RunSetValue(Staff* employee, int32_t key, intptr_t data, int32_t type, int32_t bits)
{
    g_set.Count();
    GameEnv env;
    staff::SetValue(*employee, key, data, type, bits, env);
}

} // namespace

script::ScriptValue* Staff::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return RunGetValue(this, out, key, data);
}
void Staff::SetValue(int key, intptr_t data, int type, int bits)
{
    RunSetValue(this, key, data, type, bits);
}
} // namespace obl::object
