#include "game/ui/sprite_bank.h"
#include "game/ui/sprite.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/construction/construction_manager.h"
#include "game/objects/staff_roster.h"
#include "game/objects/staff_text.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "game/objects/staff_lifeguard_builder_script_values.h"
#include "game/objects/staff_profession_script_values.h"
#include "core/call_counter.h"
#include "game/objects/script_value_convert.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;
using game::NumBuildersWhistling;

namespace {

using object::Builder;
using object::Building;
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::ObjectType;
using object::Rep;
using object::SecurityGuard;
using object::Staff;
using script::ScriptValue;
using ui::Sprite;
using ui::SpriteInstance;
namespace ex = script_program::exec;

// GetValue and SetValue take the staff member as `this`; GetValue returns its value.

// ---- The game side of the Env of staff_profession_script_values.h ------------------------------------------------------------------

using OpenFunc = bool (*)(const Building*);
const OpenFunc kIsOpen{&building_api::IsOpen};
constexpr auto kCreateSprite = [](auto* owner, auto... args) {
    return static_cast<ui::SpriteBank*>(owner)->CreateObservedInstance(args...);
};
constexpr auto kReleaseSprite = [](auto* owner, void* instance) { static_cast<ui::SpriteBank*>(owner)->ReleaseInstance(instance); };

// The construction manager's side of the builder: all game functions, so the construction module's handlers answer when they are on.
using JobFlagFunc = bool (*)(const Building*);
const JobFlagFunc kHasBuildJob{&building_api::HasBuildJob};
const JobFlagFunc kHasDemolishJob{&building_api::HasDemolishJob};

// The text buffers the profession scripts' values point at (three static buffers). A script holds the pointer, so each
// buffer keeps its address; the longest text is "bin_emptyfullbin_anim" (21 characters).
constexpr size_t kTextBytes = 32;
char g_cleanerText[kTextBytes]{};
char g_mechanicText[kTextBytes]{};
char g_lifeguardText[kTextBytes]{};

class GameEnv {
public:
    object::Base* Object(int32_t id) const { return object_manager::ObjectById(GetObjectManager(), id); }
    bool IsOpen(object::Base* building) const { return kIsOpen(static_cast<const Building*>(building)); }
    float GameDayTime() const { return GetWorld()->m_gameDayTime; }
    float GameTime() const { return GetWorld()->m_gameTime; }
    void PushThought(Staff& member, uint32_t flags) const { StaffPushThought(member, flags); }
    char* CleanerText() const { return g_cleanerText; }
    char* MechanicText() const { return g_mechanicText; }
    char* LifeguardText() const { return g_lifeguardText; }
    // The builders' whistling counter is one global of the game, shared by all of them.
    int32_t& Whistling() const { return NumBuildersWhistling(); }
    construction::Manager* Construction() const { return GetWorld()->m_constructionManager; }
    int32_t CountJobs(int32_t mode) const { return construction::runtime::CountJobs(Construction(), mode); }
    int32_t AssignNearestJob(int32_t builder, const float pos[3], int32_t mode, uint32_t* type) const
    {
        const float copy[3] = {pos[0], pos[1], pos[2]}; // the manager gets a copy of the position
        return construction::runtime::AssignBuilderToNearestJob(Construction(), builder, copy, mode, type);
    }
    int32_t WorkDirection(int32_t building, int32_t token) const
    {
        return construction::runtime::GetWorkDirection(Construction(), building, token);
    }
    int32_t BuilderLocation(int32_t builder) const { return construction::runtime::GetBuilderLocation(Construction(), builder); }
    void SetBuilderLocation(int32_t builder, int32_t location) const
    {
        construction::runtime::SetBuilderLocation(Construction(), builder, location);
    }
    void BuildEntryPoint(int32_t building, int32_t token, const float pos[3], float out[3]) const
    {
        construction::runtime::BuildEntryPoint(building, token, pos, out);
    }
    bool HasBuildJob(object::Base* building) const { return kHasBuildJob(static_cast<const Building*>(building)); }
    bool HasDemolishJob(object::Base* building) const { return kHasDemolishJob(static_cast<const Building*>(building)); }
    // The base class: Staff's own methods.
    ScriptValue Base(Staff& member, int32_t key, intptr_t data) const
    {
        ScriptValue out{};
        member.Staff::GetValue(&out, key, data);
        return out;
    }
    void Base(Staff& member, int32_t key, intptr_t data, int32_t type, int32_t bits) const
    {
        member.Staff::SetValue(key, data, type, bits);
    }
    // A float converts to int by truncation, so the conversion is exact.
    int32_t FloatToIntWrap(int32_t floatBits) const { return script_value::ToInt(ex::kTypeFloat, static_cast<uint32_t>(floatBits)); }
    void* CreateSprite(const char* name) const { return kCreateSprite(ui::GetSpriteBank(), name); }
    void ReleaseSprite(void* sprite) const { kReleaseSprite(ui::GetSpriteBank(), sprite); }
    void SetAnim(void* sprite, const char* name, float startTime, float* position) const
    {
        (static_cast<SpriteInstance*>(sprite))->SetAnimByName(name, startTime, position);
    }
};

// ---- The six professions -----------------------------------------------------------------------------------------------------------

// What a profession does not say is the default: no attached sprite, nothing outside the member that its numbers write or draw from rand.
struct ProfessionDefaults {
    static constexpr bool kAttached = false;
    static bool GetsExternal(int32_t) { return false; }
    static bool GetMayDraw(int32_t) { return false; }
    static bool SetMayDraw(int32_t, int32_t, int32_t) { return false; }
    // The part of the game outside the member that a Set number writes (compared between the recorded run and the replay, then left as the recorded run made it).
    struct External {
        bool operator==(const External&) const = default;
    };
    template<class T> static External CaptureExternal(const T&) { return {}; }
    template<class T> static void RestoreExternal(const T&, const External&) {}
    static std::string DescribeExternal(const External&) { return {}; }
};

struct CleanerValues : ProfessionDefaults {
    using Object = object::Cleaner;
    static constexpr ObjectType kType = ObjectType::Cleaner;
    static bool HandlesGet(int32_t key) { return staff::CleanerHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::CleanerHandlesSet(key); }
    static bool SetsExternal(int32_t) { return false; }
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::CleanerGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::CleanerSetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.cleaner-get-value"};
    inline static test::CallCounter set{"objects.cleaner-set-value"};
};

struct MechanicValues : ProfessionDefaults {
    using Object = object::Mechanic;
    static constexpr ObjectType kType = ObjectType::Mechanic;
    static bool HandlesGet(int32_t key) { return staff::MechanicHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::MechanicHandlesSet(key); }
    static bool SetsExternal(int32_t) { return false; }
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::MechanicGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::MechanicSetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.mechanic-get-value"};
    inline static test::CallCounter set{"objects.mechanic-set-value"};
};

struct RepValues : ProfessionDefaults {
    static constexpr bool kAttached = true;
    using Object = object::Rep;
    static constexpr ObjectType kType = ObjectType::Rep;
    static bool HandlesGet(int32_t key) { return staff::RepHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::RepHandlesSet(key); }
    static bool SetsExternal(int32_t key) { return staff::RepSetsExternal(key); }
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::RepGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::RepSetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.rep-get-value"};
    inline static test::CallCounter set{"objects.rep-set-value"};
};

struct Security : ProfessionDefaults {
    static bool GetMayDraw(int32_t key)
    {
        return key == 0x85;
    } // FoundMisdemeanour is the one read that writes: a "yes" pushes a thought, which draws from rand
    using Object = SecurityGuard;
    static constexpr ObjectType kType = ObjectType::SecurityGuard;
    static bool HandlesGet(int32_t key) { return staff::SecurityHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::SecurityHandlesSet(key); }
    static bool SetsExternal(int32_t) { return false; }
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::SecurityGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::SecuritySetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.security-guard-get-value"};
    inline static test::CallCounter set{"objects.security-guard-set-value"};
};

struct LifeguardValues : ProfessionDefaults {
    static constexpr bool kAttached = true;
    using Object = object::Lifeguard;
    static constexpr ObjectType kType = ObjectType::Lifeguard;
    static bool HandlesGet(int32_t key) { return staff::LifeguardHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::LifeguardHandlesSet(key); }
    static bool SetsExternal(int32_t key) { return staff::LifeguardSetsExternal(key); }
    static bool SetMayDraw(int32_t key, int32_t, int32_t bits)
    {
        return staff::LifeguardSetMayDraw(key, bits);
    } // Saving = yes pushes a thought
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::LifeguardGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::LifeguardSetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.lifeguard-get-value"};
    inline static test::CallCounter set{"objects.lifeguard-set-value"};
};

struct BuilderValues : ProfessionDefaults {
    using Object = object::Builder;
    static constexpr ObjectType kType = ObjectType::Builder;
    static bool HandlesGet(int32_t key) { return staff::BuilderHandlesGet(key); }
    static bool HandlesSet(int32_t key) { return staff::BuilderHandlesSet(key); }
    static bool SetsExternal(int32_t) { return false; }
    static bool GetsExternal(int32_t key) { return staff::BuilderGetsExternal(key); }
    static bool SetMayDraw(int32_t key, int32_t type, int32_t bits)
    {
        GameEnv env;
        return staff::BuilderSetMayDraw(key, ex::ToInt(env, ScriptValue{type, bits})); // BuilderLocation = 4 pushes a thought
    }
    // The places outside the member that the builder's numbers write: its location in the construction manager and the whistlers' counter.
    struct External {
        int32_t location = -1; // -1: the manager has no entry for the builder
        int32_t whistling = 0;
        bool operator==(const External&) const = default;
    };
    static External CaptureExternal(const Object& o)
    {
        GameEnv env;
        return {env.BuilderLocation(static_cast<int32_t>(o.m_id)), env.Whistling()};
    }
    static void RestoreExternal(const Object& o, const External& e)
    {
        GameEnv env;
        if (e.location != -1) env.SetBuilderLocation(static_cast<int32_t>(o.m_id), e.location);
        env.Whistling() = e.whistling;
    }
    static std::string DescribeExternal(const External& e)
    {
        return "location " + std::to_string(e.location) + ", whistling " + std::to_string(e.whistling);
    }
    template<class Env> static ScriptValue Get(Object& o, int32_t key, intptr_t data, Env& env)
    {
        return staff::BuilderGetValue(o, key, data, env);
    }
    template<class Env> static void Set(Object& o, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
    {
        staff::BuilderSetValue(o, key, data, type, bits, env);
    }
    inline static test::CallCounter get{"objects.builder-get-value"};
    inline static test::CallCounter set{"objects.builder-set-value"};
};

template<class P> struct Slots {
    using T = typename P::Object;

    static ScriptValue* Get(Staff* member, ScriptValue* out, int32_t key, intptr_t data)
    {
        T* employee = static_cast<T*>(member);
        P::get.Count();
        GameEnv env;
        *out = P::Get(*employee, key, data, env);
        return out;
    }

    static void Set(Staff* member, int32_t key, intptr_t data, int32_t type, int32_t bits)
    {
        T* employee = static_cast<T*>(member);
        P::set.Count();
        GameEnv env;
        P::Set(*employee, key, data, type, bits, env);
    }
};

} // namespace

script::ScriptValue* Builder::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<BuilderValues>::Get(this, out, key, data);
}
void Builder::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<BuilderValues>::Set(this, key, data, type, bits);
}
script::ScriptValue* Cleaner::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<CleanerValues>::Get(this, out, key, data);
}
void Cleaner::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<CleanerValues>::Set(this, key, data, type, bits);
}
script::ScriptValue* Lifeguard::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<LifeguardValues>::Get(this, out, key, data);
}
void Lifeguard::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<LifeguardValues>::Set(this, key, data, type, bits);
}
script::ScriptValue* Mechanic::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<MechanicValues>::Get(this, out, key, data);
}
void Mechanic::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<MechanicValues>::Set(this, key, data, type, bits);
}
script::ScriptValue* Rep::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<RepValues>::Get(this, out, key, data);
}
void Rep::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<RepValues>::Set(this, key, data, type, bits);
}
script::ScriptValue* SecurityGuard::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return Slots<Security>::Get(this, out, key, data);
}
void SecurityGuard::SetValue(int key, intptr_t data, int type, int bits)
{
    Slots<Security>::Set(this, key, data, type, bits);
}
} // namespace obl::object
