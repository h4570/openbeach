#include "game/objects/staff_feedback.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/world/world_state.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/feedback/feedback_manager.h"

#include "game/game/game_accessors.h"
#include "game/objects/staff_actions.h"
#include "game/objects/staff_call.h"
#include "game/ui/rep_security.h"
#include "game/objects/entity_accessors.h"
#include "game/objects/staff_roster.h"
#include "game/objects/staff_text.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/ui/sprite.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/staff_profession_receive.h"
#include "game/objects/staff_receive.h"
#include "game/objects/staff_state.h"
#include "game/objects/staff_update.h"
#include "game/objects/staff_update_feedback.h"
#include "core/call_counter.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/construction/construction_manager.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/objects/script_program_run.h"
#include "game/game/game_api.h"
#include "game/game/diagnostics.h"
#include "game/objects/object_static.h"

#include <cstdint>
#include <type_traits>

namespace obl::object {
using feedback::Feedback;
using game::FatalError;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetWorld;
using ui::RepMoveToNextBuilding;
using ui::SecurityLookForOffender;

namespace {

// The macros paste the class names.
using object::Builder;
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::Staff;

constexpr size_t kFeedbackTextSlot = 27; // GetFeedbackText supplies the profession feedback key.
constexpr size_t kOnEnterStateSlot = 2; // OnEnterState runs the profession entry commands.

using FeedbackKeyFunc = const char* (*)(Staff*, uint32_t);
using EnterStateFunc = char (*)(Staff*, uint32_t);

const auto kRepairJobs = &repair::repair_facade::CountUrgentJobs;

constexpr char kGuestClass = 3;

// Every call goes to the managers.
struct GameFeedbackEnv {
    Staff& object;
    const char* Key(uint32_t flags) { return object.GetFeedbackText(flags); }
    uint32_t StaffMessages(int type) { return feedback::Feedback::OfWorld().StaffMessages(type); }
    void Mark(uint32_t bits, int type) { feedback::Feedback::OfWorld().MarkStaffMessage(bits, type); }
    void Add(const char* key, int32_t id, uint32_t type) { feedback::Feedback::OfWorld().AddKey(key, id, type); }
    void Push(uint32_t flags) { StaffPushThought(object, flags); }
    void Blink(const char* icon) { reinterpret_cast<ui::GuiManager*>(GetGuiManager())->BlinkActionButtons(icon, false); }
    uint32_t RepairJobs() { return kRepairJobs(GetWorld()->m_repairManager); }
    int32_t CountByName(const char* name) { return object_manager::CountByName(GetObjectManager(), name); }
    int32_t CountBuildings(BuildingBehavior behavior)
    {
        return static_cast<int32_t>(obl::object_manager::CountBuildingsByBehavior(*GetObjectManager(), behavior));
    }
    uint32_t CountGuests() { return static_cast<uint32_t>(object_manager::CountByClass(GetObjectManager(), kGuestClass)); }
};

// ---- the staff Update ---------------------------------------------------------------------------------------------------------------
// The staff Update = the human Update (the base class: movement and animation) + the logic of staff_update.h.

struct UpdateEnv : GameFeedbackEnv {
    bool HumanUpdate() { return object.Person::Update(); }
    void Fatal(const char* name) { FatalError("Update of guest \"%s\" failed", name); }
    int32_t DayLengthSeconds()
    {
        int32_t seconds = 0;
        game::game_parameters::GetInt(game::game_parameters::Current(), &seconds, "iDAY_LENGTH_IN_SECONDS");
        return seconds;
    }
    float GameDeltaTime() { return GetWorld()->m_gameDeltaTime; }
};

test::CallCounter g_update("objects.staff-update");

char RunStaffUpdate(Staff* employee)
{
    g_update.Count();
    UpdateEnv env{{*employee}};
    return staff::UpdateStaff(*employee, env) ? 1 : 0;
}

// ---- the six UpdateFeedback methods --------------------------------------------------------------------------------------------------

template<class T, class P> struct Slots {
    static void Run(T* employee)
    {
        P::counter.Count();
        GameFeedbackEnv env{*employee};
        P::Update(*employee, env);
    }
};

#define OBL_STAFF_FEEDBACK_PROFESSION(Name, ObjectName, Subject, UpdateFunction)                                                           \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        inline static test::CallCounter counter{"objects." Subject "-update-feedback"};                                                    \
        static void Update(Object& object, GameFeedbackEnv& env) { staff::UpdateFunction(object, env); }                                   \
    }

OBL_STAFF_FEEDBACK_PROFESSION(BuilderFeedback, Builder, "builder", UpdateBuilderFeedback);
OBL_STAFF_FEEDBACK_PROFESSION(CleanerFeedback, Cleaner, "cleaner", UpdateCleanerFeedback);
OBL_STAFF_FEEDBACK_PROFESSION(LifeguardFeedback, Lifeguard, "lifeguard", UpdateLifeguardFeedback);
OBL_STAFF_FEEDBACK_PROFESSION(MechanicFeedback, Mechanic, "mechanic", UpdateMechanicFeedback);
OBL_STAFF_FEEDBACK_PROFESSION(RepFeedback, Rep, "rep", UpdateRepFeedback);
OBL_STAFF_FEEDBACK_PROFESSION(Security, SecurityGuard, "security-guard", UpdateSecurityFeedback);
#undef OBL_STAFF_FEEDBACK_PROFESSION

// ---- Profession updates -----------------------------------------------------------------------------------------------
// The dispatcher calls the handler functions of the helpers directly (not through their addresses); the script interpreter and the construction manager
// are the game's calls.

template<class T, class D> struct DispatchEnv {
    T& object;

    bool StaffUpdate() { return RunStaffUpdate(&object) != 0; }
    void FatalUpdate() { FatalError(D::kFatalFormat, object.m_Name); }
    void UpdateFeedback() { Slots<T, typename D::Feedback>::Run(&object); }
    void RunOnUpdate() { ScriptProgramRunOnUpdate(*object::StaffScript(object), &object, object.m_state); }
    void ReallocateBuilders() { construction::runtime::ReallocateBuilders(GetWorld()->m_constructionManager); }
};

template<class T, class D> struct DispatchSlots {
    static char Run(T* employee)
    {
        D::counter.Count();
        DispatchEnv<T, D> env{*employee};
        D::Run(*employee, env);
        return 1;
    }
};

#define OBL_STAFF_UPDATE_PROFESSION(Name, ObjectName, Subject, FeedbackProfession, FatalText, RunBody)                                     \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        using Feedback = FeedbackProfession;                                                                                               \
        static constexpr const char* kFatalFormat = FatalText;                                                                             \
        inline static test::CallCounter counter{"objects." Subject "-update"};                                                             \
        static void Run([[maybe_unused]] Object& object, DispatchEnv<Object, Name>& env) { RunBody; }                                      \
    }

// The Lifeguard and the Rep report with the guest's error text (one shared string).
constexpr const char* kPooledFatal = "Update of guest \"%s\" failed";

OBL_STAFF_UPDATE_PROFESSION(
        BuilderUpdate, Builder, "builder", BuilderFeedback, "Update of builder \"%s\" failed", staff::UpdateBuilder(object, env));
OBL_STAFF_UPDATE_PROFESSION(
        CleanerUpdate, Cleaner, "cleaner", CleanerFeedback, "Update of cleaner \"%s\" failed", staff::UpdateProfession(env));
OBL_STAFF_UPDATE_PROFESSION(LifeguardUpdate, Lifeguard, "lifeguard", LifeguardFeedback, kPooledFatal, staff::UpdateProfession(env));
OBL_STAFF_UPDATE_PROFESSION(
        MechanicUpdate, Mechanic, "mechanic", MechanicFeedback, "Update of mechanic \"%s\" failed", staff::UpdateProfession(env));
OBL_STAFF_UPDATE_PROFESSION(RepUpdate, Rep, "rep", RepFeedback, kPooledFatal, staff::UpdateProfession(env));
OBL_STAFF_UPDATE_PROFESSION(
        SecurityUpdate, SecurityGuard, "security-guard", Security, "Update of security guard \"%s\" failed", staff::UpdateProfession(env));
#undef OBL_STAFF_UPDATE_PROFESSION

// ---- the staff ReceiveMessage --------------------------------------------------------------------------------------------------------
// The common half of every profession's ReceiveMessage (each ends by calling it): the member's own part of the two clock messages and a
// bracket around two calls into the game, the script interpreter and (when the script has no procedure for the type) the movement code of the base class.

const auto kIsOpen = &building_api::IsOpen;

struct ReceiveEnv {
    Staff& object;
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    bool IsOpen(object::Base* building) { return kIsOpen(static_cast<const object::Building*>(building)); }
    float GameDayTime() { return GetWorld()->m_gameDayTime; }
    layout::World& World() { return *GetWorld(); }
    void Push(uint32_t flags) { StaffPushThought(object, flags); }
    void Recalculate() { StaffRecalculateHappinessTarget(object); }
    bool RunOnReceipt(const messages::Message& message)
    {
        return ScriptProgramRunOnReceipt(*object::StaffScript(object), &object, message) != 0;
    }
    bool Movement(const messages::Message& message) { return object.Person::ReceiveMessage(const_cast<messages::Message*>(&message)); }
};

test::CallCounter g_receive("objects.staff-receive-message");

char RunStaffReceive(Staff* employee, const messages::Message* message)
{
    g_receive.Count();
    ReceiveEnv env{*employee};
    return staff::ReceiveMessage(*employee, *message, env) ? 1 : 0;
}

// ---- the six professions' ReceiveMessage, Sack, OnEnterState and the staff OnExitState -----------------------------------------------
// Short runs of calls into the game (src/game/objects/staff_profession_receive.h, staff_state.h); they always end in the staff ReceiveMessage or the
// base class' sack.

const auto kDidNotClean = &cleaning::cleaning_facade::DidNotClean;
const auto kCancelCleaning = &cleaning::cleaning_facade::CancelJob;
const auto kRepairType = &repair::repair_facade::GetJobType;
const auto kReleaseRepair = &repair::repair_facade::ReleaseJob;
const auto kReturnRepair = &repair::repair_facade::ReturnJob;

uint8_t AnimationDirections(const Staff& member)
{
    const auto* instance = static_cast<const ui::SpriteInstance*>(member.m_sprite);
    return instance->m_sprite->m_anims[instance->m_animIndex]->m_directions;
}

template<class T> struct ProfessionEnv {
    T& object;

    float GameDayTime() { return GetWorld()->m_gameDayTime; }
    void Post(const messages::Message& message)
    {
        messages::Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    bool Base(const messages::Message& message) { return RunStaffReceive(&object, &message) != 0; }
    void ClearStaffMessages(int32_t type) { feedback::Feedback::OfWorld().ClearStaffMessages(type); }
    void LeaveTower()
    {
        if constexpr (std::is_same_v<T, object::Lifeguard>) LifeguardLeave(object);
    }
    void ReleaseGuest(uint8_t sentToGuest)
    {
        if constexpr (std::is_same_v<T, object::Rep>)
            RepReleaseGuestAndLeave(object, reinterpret_cast<uint32_t*>(&object.m_talking_id), sentToGuest != 0);
    }
    void MoveToNextBuilding()
    {
        if constexpr (std::is_same_v<T, object::Rep>) RepMoveToNextBuilding(object);
    }
    void FreeBuilder(int32_t builderId) { construction::runtime::FreeBuilderOfJob(GetWorld()->m_constructionManager, builderId); }
    void SetState(int32_t state) { entity::accessors::SetState(object, static_cast<uint32_t>(state)); }
    void EnterState(int32_t state) { object.OnEnterState(static_cast<uint32_t>(state)); }
    void LookForOffender()
    {
        if constexpr (std::is_same_v<T, object::SecurityGuard>) SecurityLookForOffender(object);
    }
    void DidNotClean(int32_t id) { kDidNotClean(GetWorld()->m_cleaningManager, id); }
    void CancelCleaning(int32_t id) { kCancelCleaning(GetWorld()->m_cleaningManager, id); }
    int32_t RepairType(int32_t id) { return kRepairType(GetWorld()->m_repairManager, id); }
    void ReleaseRepair(int32_t id) { kReleaseRepair(GetWorld()->m_repairManager, id); }
    void ReturnRepair(int32_t id, uint32_t type) { kReturnRepair(GetWorld()->m_repairManager, id, type); }
    void ReturnJobAndLeave(bool found)
    {
        if constexpr (std::is_same_v<T, object::Mechanic>) MechanicReturnJobAndLeave(object, found);
    }
    bool MechanicClaimed(int32_t id)
    {
        object::Base* target = object_manager::ObjectById(GetObjectManager(), id);
        return target && static_cast<object::Prop*>(target)->IsMechanicClaimed();
    }
    void BaseSack() { object.Staff::Sack(); }
    void AddBuilder(int32_t id) { construction::runtime::AddBuilder(GetWorld()->m_constructionManager, id); }
    bool HomeInfluenceArea(int32_t& area)
    {
        object::Base* home = object_manager::ObjectById(GetObjectManager(), object.m_HomeID);
        if (!home) return false;
        area = static_cast<object::Building*>(home)->m_InfluenceArea;
        return true;
    }
    void RunEntryScript(uint32_t state) { ScriptProgramRunOnEntry(*object::StaffScript(object), &object, static_cast<int32_t>(state)); }
    void RunExitScript(int32_t state) { ScriptProgramRunOnExit(*object::StaffScript(object), &object, state); }
    void StopMechanicAnimation()
    {
        auto* instance = static_cast<ui::SpriteInstance*>(object.m_sprite);
        instance->m_direction = staff::MechanicStoppedDirection(AnimationDirections(object));
    }
};

template<class T, class D> struct ReceiveSlots {
    static char Run(T* employee, const messages::Message* message)
    {
        D::counter.Count();
        ProfessionEnv<T> env{*employee};
        return D::Run(*employee, *message, env) ? 1 : 0;
    }
};

#define OBL_STAFF_RECEIVE_PROFESSION(Name, ObjectName, Subject, Function)                                                                  \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        inline static test::CallCounter counter{"objects." Subject "-receive-message"};                                                    \
        static bool Run(Object& object, const messages::Message& message, ProfessionEnv<Object>& env)                                      \
        {                                                                                                                                  \
            return staff::Function(object, message, env);                                                                                  \
        }                                                                                                                                  \
    }

OBL_STAFF_RECEIVE_PROFESSION(LifeguardReceive, Lifeguard, "lifeguard", ReceiveLifeguard);
OBL_STAFF_RECEIVE_PROFESSION(RepReceive, Rep, "rep", ReceiveRep);
OBL_STAFF_RECEIVE_PROFESSION(SecurityReceive, SecurityGuard, "security-guard", ReceiveSecurity);
OBL_STAFF_RECEIVE_PROFESSION(BuilderReceive, Builder, "builder", ReceiveBuilder);
OBL_STAFF_RECEIVE_PROFESSION(CleanerReceive, Cleaner, "cleaner", ReceiveCleaner);
OBL_STAFF_RECEIVE_PROFESSION(MechanicReceive, Mechanic, "mechanic", ReceiveMechanic);
#undef OBL_STAFF_RECEIVE_PROFESSION

template<class D> struct SackSlots {
    using T = typename D::Object;
    static void Run(T* employee)
    {
        D::counter.Count();
        ProfessionEnv<T> env{*employee};
        D::Run(*employee, env);
    }
};

#define OBL_STAFF_SACK(Name, ObjectName, Subject, Function)                                                                                \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        inline static test::CallCounter counter{"objects." Subject "-sack"};                                                               \
        static void Run(Object& object, ProfessionEnv<Object>& env) { staff::Function(object, env); }                                      \
    }

OBL_STAFF_SACK(BuilderSack, Builder, "builder", SackBuilder);
OBL_STAFF_SACK(CleanerSack, Cleaner, "cleaner", SackCleaner);
OBL_STAFF_SACK(LifeguardSack, Lifeguard, "lifeguard", SackLifeguard);
OBL_STAFF_SACK(MechanicSack, Mechanic, "mechanic", SackMechanic);
OBL_STAFF_SACK(RepSack, Rep, "rep", SackRep);
OBL_STAFF_SACK(SecuritySack, SecurityGuard, "security-guard", SackSecurity);
#undef OBL_STAFF_SACK

#define OBL_STAFF_ENTER(Name, ObjectName, Subject, RunBody)                                                                                \
    struct Name {                                                                                                                          \
        using Object = ObjectName;                                                                                                         \
        inline static test::CallCounter counter{"objects." Subject "-on-enter-state"};                                                     \
        static bool Run(Object& object, uint32_t state, ProfessionEnv<Object>& env) { return RunBody; }                                    \
    }

OBL_STAFF_ENTER(BuilderEnter, Builder, "builder", staff::EnterBuilder(object, state, env));
OBL_STAFF_ENTER(CleanerEnter, Cleaner, "cleaner", staff::EnterCleaner(object, state, env));
OBL_STAFF_ENTER(LifeguardEnter, Lifeguard, "lifeguard", staff::EnterLifeguard(object, state, env));
OBL_STAFF_ENTER(MechanicEnter, Mechanic, "mechanic", staff::EnterMechanic(object, state, env));
OBL_STAFF_ENTER(RepEnter, Rep, "rep", staff::EnterRepOrSecurity(object, state, env));
OBL_STAFF_ENTER(SecurityEnter, SecurityGuard, "security-guard", staff::EnterRepOrSecurity(object, state, env));
#undef OBL_STAFF_ENTER

template<class D> char RunEnter(Staff* employee, uint32_t state)
{
    using T = typename D::Object;
    D::counter.Count();
    ProfessionEnv<T> env{*static_cast<T*>(employee)};
    return D::Run(*static_cast<T*>(employee), state, env) ? 1 : 0;
}

// Rep and Security share OnEnterState: the member's type picks the counter.
char RunEnterRepOrSecurity(Staff* employee, uint32_t state)
{
    if (employee->m_type == object::ObjectType::SecurityGuard) return RunEnter<SecurityEnter>(employee, state);
    return RunEnter<RepEnter>(employee, state);
}

// RunOnExitState (OnExitState of every profession): the script of the member's state, answer true.
test::CallCounter g_exit("objects.staff-on-exit-state");

char RunOnExitState(Staff* employee)
{
    g_exit.Count();
    ProfessionEnv<Staff> env{*employee};
    return staff::ExitStaff(*employee, env) ? 1 : 0;
}

} // namespace

void RepUpdateFeedback(object::Rep& rep)
{
    Slots<Rep, RepFeedback>::Run(&rep);
}

} // namespace obl::object

namespace obl::object {
bool Staff::Update()
{
    return object::RunStaffUpdate(this);
}
bool Staff::ReceiveMessage(messages::Message* message)
{
    return object::RunStaffReceive(this, message);
}
bool Builder::OnEnterState(uint32_t state)
{
    return object::RunEnter<BuilderEnter>(this, state);
}
bool Builder::Update()
{
    return object::DispatchSlots<object::Builder, BuilderUpdate>::Run(this);
}
bool Builder::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Builder::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::Builder, BuilderReceive>::Run(this, message);
}
void Builder::Sack()
{
    object::SackSlots<BuilderSack>::Run(this);
}
bool Cleaner::OnEnterState(uint32_t state)
{
    return object::RunEnter<CleanerEnter>(this, state);
}
bool Cleaner::Update()
{
    return object::DispatchSlots<object::Cleaner, CleanerUpdate>::Run(this);
}
bool Cleaner::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Cleaner::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::Cleaner, CleanerReceive>::Run(this, message);
}
void Cleaner::Sack()
{
    object::SackSlots<CleanerSack>::Run(this);
}
bool Lifeguard::OnEnterState(uint32_t state)
{
    return object::RunEnter<LifeguardEnter>(this, state);
}
bool Lifeguard::Update()
{
    return object::DispatchSlots<object::Lifeguard, LifeguardUpdate>::Run(this);
}
bool Lifeguard::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Lifeguard::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::Lifeguard, LifeguardReceive>::Run(this, message);
}
void Lifeguard::Sack()
{
    object::SackSlots<LifeguardSack>::Run(this);
}
bool Mechanic::OnEnterState(uint32_t state)
{
    return object::RunEnter<MechanicEnter>(this, state);
}
bool Mechanic::Update()
{
    return object::DispatchSlots<object::Mechanic, MechanicUpdate>::Run(this);
}
bool Mechanic::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Mechanic::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::Mechanic, MechanicReceive>::Run(this, message);
}
void Mechanic::Sack()
{
    object::SackSlots<MechanicSack>::Run(this);
}
bool Rep::OnEnterState(uint32_t state)
{
    return object::RunEnterRepOrSecurity(this, state);
}
bool Rep::Update()
{
    return object::DispatchSlots<object::Rep, RepUpdate>::Run(this);
}
bool Rep::OnExitState()
{
    return object::RunOnExitState(this);
}
bool Rep::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::Rep, RepReceive>::Run(this, message);
}
void Rep::Sack()
{
    object::SackSlots<RepSack>::Run(this);
}
bool SecurityGuard::OnEnterState(uint32_t state)
{
    return object::RunEnterRepOrSecurity(this, state);
}
bool SecurityGuard::Update()
{
    return object::DispatchSlots<object::SecurityGuard, SecurityUpdate>::Run(this);
}
bool SecurityGuard::OnExitState()
{
    return object::RunOnExitState(this);
}
bool SecurityGuard::ReceiveMessage(messages::Message* message)
{
    return object::ReceiveSlots<object::SecurityGuard, SecurityReceive>::Run(this, message);
}
void SecurityGuard::Sack()
{
    object::SackSlots<SecuritySack>::Run(this);
}
} // namespace obl::object
