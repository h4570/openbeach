#include "core/char16.h"
#include "game/objects/staff_text.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/staff_roster.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/staff_thoughts.h"
#include "core/call_counter.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"

#include <cstdint>

namespace obl::object {
using game::GetWorldParams;
using game::Rand;

namespace {

using object::Staff;
using staff::TextKind;
using tables::StaffDef;

using TextFunc = const char* (*)(Staff*, uint32_t);

constexpr size_t kFeedbackTextSlot = 27; // GetFeedbackText supplies the profession feedback key.

// What staff::FeedbackText needs of the game: the draws, and the gender of the staff definition found by the object's name (the world
// parameters give the id of the name and then the record of that id, so a name no record has stops with the game's FatalError).
struct GameTextEnv {
    const Staff& employee;
    int Rand() { return game::Rand(); }
    bool Female()
    {
        tables::WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetStaffId(params, employee.m_Name);
        const auto* definition = static_cast<const StaffDef*>(world::world_params::GetStaffDefinition(params, id));
        return definition->m_female != 0;
    }
};

// PushThought also asks the object's own GetFeedbackText through the vtable (so the profession's override answers) and looks texts up in the game's table.
struct GameThoughtEnv : GameTextEnv {
    Staff& object;
    explicit GameThoughtEnv(Staff& staff) : GameTextEnv{staff}, object(staff) {}
    const char* FeedbackKey(uint32_t flags) { return object.GetFeedbackText(flags); }
    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
};

// ---- GetFeedbackText ---------------------------------------------------------------------------------------------------------

struct TextSlot {
    TextKind kind;
    test::CallCounter counter;
    TextSlot(TextKind kindIn, const char* name) : kind(kindIn), counter(name) {}
};

TextSlot g_text[] = {
        {TextKind::Base, "objects.staff-get-feedback-text"},
        {TextKind::Builder, "objects.builder-get-feedback-text"},
        {TextKind::Cleaner, "objects.cleaner-get-feedback-text"},
        {TextKind::Lifeguard, "objects.lifeguard-get-feedback-text"},
        {TextKind::Mechanic, "objects.mechanic-get-feedback-text"},
        {TextKind::Rep, "objects.rep-get-feedback-text"},
        {TextKind::Security, "objects.security-guard-get-feedback-text"},
};

const char* Text(const TextSlot& slot, Staff* employee, uint32_t flags)
{
    GameTextEnv env{*employee};
    return staff::FeedbackText(slot.kind, flags, env);
}

template<int I> const char* RunFeedbackText(Staff* employee, uint32_t flags)
{
    TextSlot& slot = g_text[I];
    slot.counter.Count();
    return Text(slot, employee, flags);
}

// ---- Staff PushThought --------------------------------------------------------------------------------------------------

test::CallCounter g_push("objects.staff-push-thought");

} // namespace

void StaffPushThought(Staff& member, uint32_t flags)
{
    g_push.Count();
    RunPushThought(member, flags);
}

void RunPushThought(Staff& member, uint32_t flags)
{
    GameThoughtEnv env(member);
    staff::PushThought(member, flags, env);
}

} // namespace obl::object

namespace obl::object {
const char* Staff::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<0>(this, action);
}
const char* Builder::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<1>(this, action);
}
const char* Cleaner::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<2>(this, action);
}
const char* Lifeguard::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<3>(this, action);
}
const char* Mechanic::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<4>(this, action);
}
const char* Rep::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<5>(this, action);
}
const char* SecurityGuard::GetFeedbackText(uint32_t action)
{
    return object::RunFeedbackText<6>(this, action);
}
} // namespace obl::object
