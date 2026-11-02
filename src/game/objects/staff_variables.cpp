#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/staff_roster.h"
#include "game/objects/staff_text.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/staff_set_variable.h"
#include "game/objects/staff_wages.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"
#include "game/objects/object_building.h"
#include "game/objects/object_building.h"

#include <cstdint>

namespace obl::object {
using game::FatalError;
using game::GetObjectManager;

namespace {

// The macros paste the class names.
using object::Builder;
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::Staff;

// ---- The game side of the Env of staff_set_variable.h ------------------------------------------------------------------------------

struct GameEnv {
    Staff& member;

    void PushThought(uint32_t flags) { RunPushThought(member, flags); }
    void RecalculateHappiness() { staff::RecalculateHappinessTarget(member); }
    void Fatal(const char* key) { FatalError("A script set unrecognized staff variable \"%s\"", key); }
    // The lifeguard tower's beach mode (SetBeachOpenMode of the tower). A lifeguard whose tower is gone (no object
    // with that id) changes nothing.
    void ApplyBeachMode(int32_t home, uint32_t mode)
    {
        object::Base* tower = object_manager::ObjectById(GetObjectManager(), home);
        if (!tower) return;
        static_cast<object::Building*>(tower)->SetBeachOpenMode(static_cast<int32_t>(mode));
    }
};

// ---- One descriptor per class ------------------------------------------------------------------------------------------------------

#define OBL_STAFF_SETVAR(Name, ObjectName, Subject, TypeValue, Function)                                                                   \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        static constexpr object::ObjectType kType = object::ObjectType::TypeValue;                                                         \
        inline static test::CallCounter counter{"objects." Subject "-set-variable"};                                                       \
        template<class Env> static void Run(Object& object, const char* key, const void* value, Env& env)                                  \
        {                                                                                                                                  \
            staff::Function(object, key, value, env);                                                                                      \
        }                                                                                                                                  \
    }

struct StaffBase {
    using Object = Staff;
    static constexpr object::ObjectType kType = object::ObjectType::Builder; // the driver borrows a builder for the base class
    inline static test::CallCounter counter{"objects.staff-set-variable"};
    template<class Env> static void Run(Object& object, const char* key, const void* value, Env& env)
    {
        staff::SetVariable(object, key, value, env);
    }
};
struct BuilderVar : StaffBase {
    using Object = object::Builder;
    inline static test::CallCounter counter{"objects.builder-set-variable"};
    // The builder has no keyword of its own: it hands every key to the base class.
    template<class Env> static void Run(Object& object, const char* key, const void* value, Env& env)
    {
        staff::SetVariable(object, key, value, env);
    }
};
OBL_STAFF_SETVAR(CleanerVar, Cleaner, "cleaner", Cleaner, SetVariableCleaner);
OBL_STAFF_SETVAR(LifeguardVar, Lifeguard, "lifeguard", Lifeguard, SetVariableLifeguard);
OBL_STAFF_SETVAR(MechanicVar, Mechanic, "mechanic", Mechanic, SetVariableMechanic);
OBL_STAFF_SETVAR(RepVar, Rep, "rep", Rep, SetVariableRep);
OBL_STAFF_SETVAR(SecurityVar, SecurityGuard, "security-guard", SecurityGuard, SetVariableSecurity);
#undef OBL_STAFF_SETVAR

template<class D> struct VarSlots {
    using T = typename D::Object;

    // The thought of a wage change draws at most twice (the base class's and the profession's text); nothing is posted.
    static void Run(T* employee, const char* key, const void* value)
    {
        D::counter.Count();
        GameEnv env{*employee};
        D::Run(*employee, key, value, env);
    }
};

} // namespace

void Staff::SetVariable(const char* key, const void* value)
{
    VarSlots<StaffBase>::Run(this, key, value);
}
void Builder::SetVariable(const char* key, const void* value)
{
    VarSlots<BuilderVar>::Run(this, key, value);
}
void Cleaner::SetVariable(const char* key, const void* value)
{
    VarSlots<CleanerVar>::Run(this, key, value);
}
void Lifeguard::SetVariable(const char* key, const void* value)
{
    VarSlots<LifeguardVar>::Run(this, key, value);
}
void Mechanic::SetVariable(const char* key, const void* value)
{
    VarSlots<MechanicVar>::Run(this, key, value);
}
void Rep::SetVariable(const char* key, const void* value)
{
    VarSlots<RepVar>::Run(this, key, value);
}
void SecurityGuard::SetVariable(const char* key, const void* value)
{
    VarSlots<SecurityVar>::Run(this, key, value);
}
} // namespace obl::object
