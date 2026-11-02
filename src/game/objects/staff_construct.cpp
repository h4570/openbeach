#include "core/char16.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/sprite_bank.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/feedback/feedback_manager.h"

#include "core/code_page.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/construction/construction_manager.h"
#include "game/objects/staff_roster.h"
#include "game/world/world_script_host_game.h"
#include "core/log.h"
#include "game/objects/staff_construction.h"
#include "core/call_counter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include "game/ui/text_table.h"
#include "game/world/world_params.h"

namespace obl::object {
using feedback::Feedback;
using game::GetContext;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::GetWorldScriptHost;
using game::Rand;

namespace {
using namespace world_data;
using object::Builder; // object_staff.h
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::ServiceStaff; // the macros paste the class names
using object::SnootyStaff;
using object::Staff;
using ui::SpriteInstance;
using namespace tables;
namespace cons = staff::construct;
using cons::Kind;

// ---- The game side of the Env ----------------------------------------------------------------------------------------------------------

// The counter of each Kind (Staff and Service keep none, as before).
constexpr int32_t game::Context::* kCounterMember[] = {nullptr, &game::Context::staffCleanerCount, &game::Context::staffMechanicCount,
        &game::Context::staffLifeguardCount, &game::Context::staffRepCount, &game::Context::staffGuardCount,
        &game::Context::staffBuilderCount};

uint32_t BitsOf(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

class GameEnv {
public:
    void RecalculateHappiness(object::Staff& employee) { StaffRecalculateHappinessTarget(employee); }
    const StaffDef* StaffDefinition(const char* name)
    {
        WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetStaffId(params, name);
        return world::world_params::GetStaffDefinition(params, id);
    }
    void PlayStand(ServiceStaff& employee)
    {
        (static_cast<SpriteInstance*>(employee.m_sprite))->SetAnimByName("stand", GetWorld()->m_gameTime, employee.m_WorldXYZ);
    }
    uint32_t GameDay() { return static_cast<uint32_t>(GetWorld()->m_gameDay); }
    uint32_t DayTimeBits() { return BitsOf(GetWorld()->m_gameDayTime); }
    int Rand() { return game::Rand(); }
    void* ScriptLookup(const char* name) { return GetWorldScriptHost()->Find(name); }
    void* CreateSprite(const char* name) { return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name); }
    void CopyKeyline(void* sprite) { static_cast<SpriteInstance*>(sprite)->m_keyline = GetObjectManager()->m_globalKeyline; }
    void ReleaseSprite(void* sprite) { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite); }
    int32_t& Counter(Kind kind) { return GetContext().*kCounterMember[static_cast<size_t>(kind)]; }
    void UpdateStaffButtons(uint8_t type, bool present)
    {
        reinterpret_cast<ui::GuiManager*>(GetGuiManager())->UpdateStaffButtons(type, present);
    }
    void RemoveFeedbackByText(const char* key) { feedback::Feedback::OfWorld().RemoveByText(key); }
    void RemoveFeedbackByObject(uint32_t id) { feedback::Feedback::OfWorld().RemoveByObject(id); }
    void RemoveBuilder(int32_t id) { construction::runtime::RemoveBuilder(GetWorld()->m_constructionManager, id); }
    uint32_t FineBits(const char* key)
    {
        WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetFineId(params, key);
        const auto* fine = world::world_params::GetFineRecord(params, id);
        return BitsOf(fine->m_cost);
    }
    uint32_t NameCount(bool female) { return static_cast<uint32_t>(GetWorldParams()->m_nameCounts[female ? 1 : 0]); }
    const char* NameKey(bool female, uint32_t index) { return GetWorldParams()->m_names[(female ? 0x8c : 0) + index]; }
    void* New(size_t size) { return ::operator new(size); }
    void Delete(void* memory) { ::operator delete(memory); }
    void Widen(const char* source, Char16* destination, size_t count)
    {
        code_page::Widen(source, reinterpret_cast<char16_t*>(destination), count);
    }
    const Char16* Lookup(const Char16* key)
    {
        return reinterpret_cast<const Char16*>(ui::text_table::LookupW(reinterpret_cast<const Char16*>(key)));
    }
};

// ---- Per class tables ------------------------------------------------------------------------------------------------------------------

// The constructor and destructor body of one class: Staff's takes the name, a profession's builds on Staff's.
template<class T> struct Traits;
template<> struct Traits<object::Staff> {
    template<class Env> static void Construct(object::Staff& employee, const char* name, Env& env) { cons::Construct(employee, name, env); }
};
#define OBL_STAFF_TRAITS(T, CONSTRUCT, DESTRUCT)                                                                                           \
    template<> struct Traits<T> {                                                                                                          \
        template<class Env> static void Construct(T& employee, const char* name, Env& env) { CONSTRUCT(employee, name, env); }             \
        template<class Env> static void Destruct(T& employee, Env& env) { DESTRUCT(employee, env); }                                       \
    };
OBL_STAFF_TRAITS(Cleaner, cons::ConstructCleaner, cons::DestructCleaner)
OBL_STAFF_TRAITS(Mechanic, cons::ConstructMechanic, cons::DestructMechanic)
OBL_STAFF_TRAITS(Lifeguard, cons::ConstructLifeguard, cons::DestructLifeguard)
OBL_STAFF_TRAITS(Rep, cons::ConstructRep, cons::DestructRep)
OBL_STAFF_TRAITS(SecurityGuard, cons::ConstructSecurity, cons::DestructSecurity)
OBL_STAFF_TRAITS(Builder, cons::ConstructBuilder, cons::DestructBuilder)
#undef OBL_STAFF_TRAITS
template<> struct Traits<ServiceStaff> {
    template<class Env> static void Construct(ServiceStaff& employee, const char* name, Env& env)
    {
        cons::ConstructService(employee, name, env);
    }
};

// Counters of one class: the calls of its constructor and destructor.
template<class T> struct Counters;
#define OBL_STAFF_COUNTERS(T, Subject)                                                                                                     \
    test::CallCounter g_##T##Construct("objects." Subject "-construct"), g_##T##Destroy("objects." Subject "-destroy");                    \
    template<> struct Counters<T> {                                                                                                        \
        static test::CallCounter& Construct() { return g_##T##Construct; }                                                                 \
        static test::CallCounter& Destroy() { return g_##T##Destroy; }                                                                     \
    };
OBL_STAFF_COUNTERS(Staff, "staff")
OBL_STAFF_COUNTERS(Cleaner, "cleaner")
OBL_STAFF_COUNTERS(Mechanic, "mechanic")
OBL_STAFF_COUNTERS(Lifeguard, "lifeguard")
OBL_STAFF_COUNTERS(Rep, "rep")
OBL_STAFF_COUNTERS(SecurityGuard, "security-guard")
OBL_STAFF_COUNTERS(Builder, "builder")
OBL_STAFF_COUNTERS(ServiceStaff, "service-staff")
#undef OBL_STAFF_COUNTERS

template<class T> void RunConstruct(T& employee, const char* name)
{
    Counters<T>::Construct().Count();
    GameEnv env;
    Traits<T>::Construct(employee, name, env);
}

template<class T> void RunDestroy(T& employee)
{
    Counters<T>::Destroy().Count();
    GameEnv env;
    Traits<T>::Destruct(employee, env);
}

} // namespace

Staff::Staff(const char* name) : Person(name)
{
    RunConstruct(*this, name);
}
ServiceStaff::ServiceStaff(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
Lifeguard::Lifeguard(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
Mechanic::Mechanic(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
Rep::Rep(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
Builder::Builder(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
Cleaner::Cleaner(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
SecurityGuard::SecurityGuard(const char* name) : Staff(name)
{
    RunConstruct(*this, name);
}
ServiceStaff::~ServiceStaff()
{
    Counters<ServiceStaff>::Destroy().Count();
}
Builder::~Builder()
{
    RunDestroy(*this);
}
Cleaner::~Cleaner()
{
    RunDestroy(*this);
}
Lifeguard::~Lifeguard()
{
    RunDestroy(*this);
}
Mechanic::~Mechanic()
{
    RunDestroy(*this);
}
Rep::~Rep()
{
    RunDestroy(*this);
}
SecurityGuard::~SecurityGuard()
{
    RunDestroy(*this);
}
} // namespace obl::object
