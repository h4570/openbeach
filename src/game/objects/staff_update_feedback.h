#pragma once

#include "game/tables/building_behavior.h"
#include "game/objects/object_staff.h"

#include <cstdint>

// (docs/architecture/objects.md): the six profession `UpdateFeedback` methods that the staff update runs every frame
// before the script. Each turns a condition of the member (or of the world) into a thought: the feedback bit is remembered in the status word
// (`m_FeedbackStatus`, a bit that was tested is never tested again until a script clears it), the text goes to the player's message list
// (`feedback::Feedback::AddKey`, only when the profession has a key for the bit), to the member's own list of thoughts (`PushThought`) and the
// action button of the profession stops blinking. `m_RequiredFeedback` is a gate: the whole method does nothing while it is not zero (the unhappy
// thought leaves it set, the others reset it), so one frame announces at most what the gate lets through.
//
// The logic is a template over an environment, so it runs on the game and in the unit tests:
//   env.Key(flags)            - the virtual GetFeedbackText of the member: the text key of the bits, or null; may draw
//   env.StaffMessages(type)   - feedback::Feedback::StaffMessages: the bits already announced to the player for a staff type
//   env.Mark(bits, type)      - feedback::Feedback::MarkStaffMessage
//   env.Add(key, id, type)    - feedback::Feedback::AddKey (the type is the colour/category of the message, not a staff type)
//   env.Push(flags)           - PushThought
//   env.Blink(icon)           - clears the blink flag for the action button icon
//   env.RepairJobs()          - repair::repair_facade::CountUrgentJobs: breakdowns waiting + the scenery jobs
//   env.CountByName(name)     - the object manager's count by name: the objects with that name ("bin")
//   env.CountBuildings(b)     - object_manager::CountBuildingsByBehavior: the buildings with that behavior (toilet)
//   env.CountGuests()         - the object manager's count of class 3 (guests)
namespace obl::staff {

namespace feedback_detail {

constexpr int32_t kUnhappyBit = 0x800000; // `m_happiness` below 0.6; its text is asked with 0x100000, the thought is pushed with this bit
constexpr uint32_t kUnhappyTextFlags = 0x100000;
constexpr int32_t kCantDoJobBit = 1u << 22; // `m_cantDoJob`
constexpr uint32_t kMessageTypeUnhappy =
        7; // feedback::Feedback::AddKey type of the unhappy message, whatever the profession; the rep's category messages use it too

// Happiness against the float 0.6: below the limit, and also unordered (NaN).
inline bool Unhappy(float happiness)
{
    return !(happiness >= 0.6f);
}

// The announcement every block ends with once its condition holds: the bit joins what is required (normally nothing), becomes the current
// feedback and the requirement is cleared; the text of the bits goes to the player (when there is one), the thought to the member's list.
template<class Object, class Env> void Announce(Object& object, Env& env, int32_t bits, uint32_t addType)
{
    const int32_t current = object.m_RequiredFeedback | bits;
    object.m_CurrentFeedback = current;
    object.m_RequiredFeedback = 0;
    const char* key = env.Key(static_cast<uint32_t>(current));
    if (key) env.Add(key, static_cast<int32_t>(object.m_id), addType);
    env.Push(static_cast<uint32_t>(object.m_CurrentFeedback));
}

// The unhappy block shared by all six: no null check on the text (whatever the profession answers goes to Add).
template<class Object, class Env> void AnnounceUnhappy(Object& object, Env& env)
{
    if ((object.m_FeedbackStatus & kUnhappyBit) == 0 && Unhappy(object.m_happiness)) {
        object.m_FeedbackStatus |= kUnhappyBit;
        object.m_RequiredFeedback |= kUnhappyBit;
        env.Add(env.Key(kUnhappyTextFlags), static_cast<int32_t>(object.m_id), kMessageTypeUnhappy);
        env.Push(static_cast<uint32_t>(kUnhappyBit));
    }
}

// "I can't do the job": the flag is tested only the first time the bit is looked at and cleared by the announcement (the lifeguard keeps it).
template<class Object, class Env> bool CantDoJobBlock(Object& object, Env& env, uint32_t addType, const char* icon, bool clearFlag)
{
    if ((object.m_FeedbackStatus & kCantDoJobBit) != 0) return false;
    object.m_FeedbackStatus |= kCantDoJobBit;
    if (object.m_cantDoJob == 0) return false;
    Announce(object, env, kCantDoJobBit, addType);
    env.Blink(icon);
    if (clearFlag) object.m_cantDoJob = 0;
    return true;
}

// The block that tells the player once per staff type (the shared mask of FeedbackManager) when its caller found that `bit` was not announced yet and the
// condition holds: the bit is required, marked as told, then announced like any other. The requirement is still zero when a block starts, so setting it
// before or after the mark gives the same state.
template<class Object, class Env>
void AnnounceOnce(Object& object, Env& env, int32_t bit, int staffType, uint32_t addType, const char* icon)
{
    object.m_RequiredFeedback |= bit;
    env.Mark(static_cast<uint32_t>(bit), staffType);
    Announce(object, env, bit, addType);
    env.Blink(icon);
}

} // namespace feedback_detail

// The builder's UpdateFeedback. The quota and woken-up thoughts need no condition: the status bits start clear and a script clears them
// again to ask for the thought. After the unhappy block (which leaves the requirement set) both carry its bit into the text flags.
template<class Env> void UpdateBuilderFeedback(object::Builder& builder, Env& env)
{
    using namespace feedback_detail;
    if (builder.m_RequiredFeedback != 0) return;
    AnnounceUnhappy(builder, env);
    constexpr uint32_t kAddType = 0xb;
    constexpr const char* kIcon = "BuilderIcon";
    for (int32_t bit : {4, 2}) {
        if ((builder.m_FeedbackStatus & bit) != 0) continue;
        builder.m_FeedbackStatus |= bit;
        Announce(builder, env, bit, kAddType);
        env.Blink(kIcon);
    }
}

// The cleaner's UpdateFeedback: no toilets at all (announced once to the player), a toilet that was busy, no way to do the job, unhappy.
template<class Env> void UpdateCleanerFeedback(object::Cleaner& cleaner, Env& env)
{
    using namespace feedback_detail;
    if (cleaner.m_RequiredFeedback != 0) return;
    constexpr uint32_t kAddType = 0xc;
    constexpr const char* kIcon = "CleanerIcon";
    constexpr int kStaffType = 0xb;
    if ((cleaner.m_FeedbackStatus & 4) == 0) {
        cleaner.m_FeedbackStatus |= 4;
        if ((env.StaffMessages(kStaffType) & 4) == 0 && env.CountBuildings(BuildingBehavior::Toilet) == 0)
            AnnounceOnce(cleaner, env, 4, kStaffType, kAddType, kIcon);
    }
    if ((cleaner.m_FeedbackStatus & 2) == 0) {
        cleaner.m_FeedbackStatus |= 2;
        if (cleaner.m_BusyToilet != 0) {
            Announce(cleaner, env, 2, kAddType);
            env.Blink(kIcon);
            cleaner.m_BusyToilet = 0;
        }
    }
    CantDoJobBlock(cleaner, env, kAddType, kIcon, true);
    AnnounceUnhappy(cleaner, env);
}

// The lifeguard's UpdateFeedback: a guest drowned, too many calls out of the area (both once to the player), three thoughts a script
// asks for with `m_cantDoJob` (they never clear it), unhappy.
template<class Env> void UpdateLifeguardFeedback(object::Lifeguard& lifeguard, Env& env)
{
    using namespace feedback_detail;
    if (lifeguard.m_RequiredFeedback != 0) return;
    constexpr uint32_t kAddType = 8;
    constexpr const char* kIcon = "LifeguardIcon";
    constexpr int kStaffType = 7;
    if (lifeguard.m_GuestDrowned != 0 && (lifeguard.m_FeedbackStatus & 2) == 0) {
        lifeguard.m_FeedbackStatus |= 2;
        AnnounceOnce(lifeguard, env, 2, kStaffType, kAddType, kIcon);
        lifeguard.m_GuestDrowned = 0;
    }
    if ((lifeguard.m_FeedbackStatus & 4) == 0) {
        lifeguard.m_FeedbackStatus |= 4;
        if ((env.StaffMessages(kStaffType) & 4) == 0 && static_cast<uint32_t>(lifeguard.m_timesCalledOut) > 8)
            AnnounceOnce(lifeguard, env, 4, kStaffType, kAddType, kIcon);
    }
    for (int32_t bit : {8, 0x10, 0x20}) {
        if ((lifeguard.m_FeedbackStatus & bit) != 0) continue;
        lifeguard.m_FeedbackStatus |= bit;
        if (lifeguard.m_cantDoJob == 0) continue;
        Announce(lifeguard, env, bit, kAddType);
        env.Blink(kIcon);
    }
    AnnounceUnhappy(lifeguard, env);
}

// The mechanic's UpdateFeedback: repairs waiting (once to the player; when it was told already the member only thinks it, and the requirement
// stays set), no way to do the job, unhappy.
template<class Env> void UpdateMechanicFeedback(object::Mechanic& mechanic, Env& env)
{
    using namespace feedback_detail;
    if (mechanic.m_RequiredFeedback != 0) return;
    constexpr uint32_t kAddType = 9;
    constexpr const char* kIcon = "MechanicIcon";
    constexpr int kStaffType = 8;
    if ((mechanic.m_FeedbackStatus & 2) == 0) {
        mechanic.m_FeedbackStatus |= 2;
        if (env.RepairJobs() != 0) {
            if ((env.StaffMessages(kStaffType) & 2) == 0) {
                AnnounceOnce(mechanic, env, 2, kStaffType, kAddType, kIcon);
            } else {
                mechanic.m_RequiredFeedback |= 2;
                env.Push(2);
            }
        }
    }
    CantDoJobBlock(mechanic, env, kAddType, kIcon, true);
    AnnounceUnhappy(mechanic, env);
}

// The rep's UpdateFeedback. The status word doubles as the index of the category the script is scanning (0..6), so the first block
// reads the counters of that category: with the guest opinion below 2 the pair (+0, +1) is summed and its bit is 2 << index, otherwise the pair
// (+3, +4) with 0x100 << index; more than five reports are announced once to the player. An index beyond the table announces nothing. The reset after the announcement clears the
// first pair's two counters, but of the second pair only +2 and +3, so +4 survives (tests pin this). The block always ends by
// setting the requirement and clearing the status, so a scan step is taken once per request.
template<class Env> void UpdateRepFeedback(object::Rep& rep, Env& env)
{
    using namespace feedback_detail;
    if (rep.m_RequiredFeedback != 0) return;
    constexpr int kStaffType = 9;
    const uint32_t messages = env.StaffMessages(kStaffType);
    if ((static_cast<uint32_t>(rep.m_FeedbackStatus) & messages) == 0) {
        const uint32_t index = static_cast<uint32_t>(rep.m_FeedbackStatus);
        const uint32_t row = index <= 6 ? index : 0u; // past the table nothing is announced, so row 0 is only read
        auto& counters = rep.m_counters[row];
        const bool lowOpinion = static_cast<uint32_t>(rep.m_GuestOpinionValue) < 2;
        const uint32_t total = lowOpinion ? static_cast<uint32_t>(counters[1]) + static_cast<uint32_t>(counters[0])
                                          : static_cast<uint32_t>(counters[4]) + static_cast<uint32_t>(counters[3]);
        const uint32_t mask = (lowOpinion ? 2u : 0x100u) << index;
        if (index <= 6 && total > 5) {
            env.Mark(mask, kStaffType);
            env.Add(env.Key(mask), static_cast<int32_t>(rep.m_id), kMessageTypeUnhappy);
            if (lowOpinion) {
                counters[0] = 0;
                counters[1] = 0;
            } else {
                counters[2] = 0;
                counters[3] = 0;
            }
        }
        rep.m_RequiredFeedback = 1;
        rep.m_FeedbackStatus = 0;
    }
    AnnounceUnhappy(rep, env);
}

// The security guard's UpdateFeedback: more guests than fifteen per litter bin (once to the player), more than five vandals, more than five
// fights (both once to the player), no way to do the job, unhappy.
template<class Env> void UpdateSecurityFeedback(object::SecurityGuard& guard, Env& env)
{
    using namespace feedback_detail;
    if (guard.m_RequiredFeedback != 0) return;
    constexpr uint32_t kAddType = 0xd;
    constexpr const char* kIcon = "SecurityGuardIcon";
    constexpr int kStaffType = 0xc;
    if ((guard.m_FeedbackStatus & 2) == 0) {
        guard.m_FeedbackStatus |= 2;
        if ((env.StaffMessages(kStaffType) & 2) == 0) {
            const uint32_t guests = env.CountGuests();
            const int32_t bins = env.CountByName("bin");
            if (static_cast<uint32_t>(bins) * 15u < guests) AnnounceOnce(guard, env, 2, kStaffType, kAddType, kIcon);
        }
    }
    if (guard.m_numVandals > 5 && (guard.m_FeedbackStatus & 4) == 0) {
        guard.m_FeedbackStatus |= 4;
        if ((env.StaffMessages(kStaffType) & 4) == 0) AnnounceOnce(guard, env, 4, kStaffType, kAddType, kIcon);
    }
    if (guard.m_numFights > 5 && (guard.m_FeedbackStatus & 8) == 0) {
        guard.m_FeedbackStatus |= 8;
        if ((env.StaffMessages(kStaffType) & 8) == 0) AnnounceOnce(guard, env, 8, kStaffType, kAddType, kIcon);
    }
    CantDoJobBlock(guard, env, kAddType, kIcon, true);
    AnnounceUnhappy(guard, env);
}

} // namespace obl::staff
