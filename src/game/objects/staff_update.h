#pragma once

#include "core/float_to_int.h"
#include "game/objects/object_staff.h"

#include <cstdint>

// Staff Update runs the common half of every profession's update, and its
// helper (the two announcements below). The base class update (the human update, movement and animation) stays a call into the
// game; what is spelled out here is what the staff member adds: the thoughts of the one-off flags, and the walk of the happiness to its target.
// The profession update dispatchers themselves are at the end of the file.
//
// The logic is a template over an environment, so it runs on the game and in the unit tests:
//   env.HumanUpdate()           - the human update of the member; false makes the game end with a FatalError
//   env.Fatal(name)             - FatalError("Update of guest \"%s\" failed", name)
//   env.Key(flags)              - the virtual GetFeedbackText; may draw
//   env.StaffMessages(type)     - feedback::Feedback::StaffMessages
//   env.Mark(bits, type)        - feedback::Feedback::MarkStaffMessage
//   env.Add(key, id, type)      - feedback::Feedback::AddKey
//   env.Push(flags)             - PushThought
//   env.DayLengthSeconds()      - reads iDAY_LENGTH_IN_SECONDS
//   env.GameDeltaTime()         - World::m_gameDeltaTime
// The arithmetic runs at 53-bit precision: each operation is one double operation (the products of floats and small
// constants are exact, the quotient, the products of them and the sum round once to 53 bits) and the result is stored to a float once.
namespace obl::staff {

namespace update_detail {

constexpr uint32_t kFlagThought = 0x200000; // announced once when `m_directedToObject` is set
constexpr uint32_t kSackedThought = 0x10000000; // announced once per game for the whole staff (staff message type 6) after a sacking
constexpr int kStaffMessageType = 6; // the shared mask of the announcements of this function
constexpr uint32_t kMessageType = 7; // feedback::Feedback::AddKey type (the colour/category of the message)
constexpr float kGranularity = 1000.0f; // the happiness and its target are compared in thousandths
constexpr float kRateScale = 100.0f;
constexpr float kRateFactor = 0.01f; // the float nearest to 0.01 (0.00999999977...)

template<class Env> void Announce(object::Staff& staff, Env& env, uint32_t flags)
{
    // No null check on the text: whatever the profession answers goes to `Add` (the same as the unhappy thought).
    env.Add(env.Key(flags), static_cast<int32_t>(staff.m_id), kMessageType);
    env.Mark(flags, kStaffMessageType);
    env.Push(flags);
}

} // namespace update_detail

// the member's directed-to-object flag (cleared by the announcement) and a sacked member (told to the player once for the whole staff).
template<class Env> void AnnounceStaffEvents(object::Staff& staff, Env& env)
{
    using namespace update_detail;
    if (staff.m_directedToObject != 0) {
        Announce(staff, env, kFlagThought);
        staff.m_directedToObject = 0;
    }
    if (staff.m_gotTheSack != 0) {
        if ((env.StaffMessages(kStaffMessageType) & kSackedThought) == 0) Announce(staff, env, kSackedThought);
    }
}

// UpdateStaff. The happiness moves by `rate × 100 × (dt / (dayLength / 24)) × 0.01` per frame (the rate is per game hour); when the happiness and
// its target agree to the thousandth the walk is over and the member stops changing. The result is clamped to 0..1 (NaN becomes 0).
template<class Env> bool UpdateStaff(object::Staff& staff, Env& env)
{
    using namespace update_detail;
    if (!env.HumanUpdate()) env.Fatal(staff.m_Name);
    AnnounceStaffEvents(staff, env);

    const int target = FloatToIntWrap(static_cast<double>(staff.m_targetHappiness) * static_cast<double>(kGranularity));
    const int current = FloatToIntWrap(static_cast<double>(staff.m_happiness) * static_cast<double>(kGranularity));
    if (current == target && staff.m_happinessChanging != 0) {
        staff.m_happinessRate = 0.0f;
        staff.m_happinessChanging = 0;
    }

    // The game hour in seconds is an integer division (truncating), the quotient is in double: zero (a day shorter than 24 s) gives an infinity, or NaN
    // at a zero frame, which the clamps below turn into 1 and 0.
    const int32_t hourSeconds = env.DayLengthSeconds() / 24;
    const double perHour = static_cast<double>(env.GameDeltaTime()) / static_cast<double>(hourSeconds);
    const double scaledRate = static_cast<double>(staff.m_happinessRate) * static_cast<double>(kRateScale);
    const double step = perHour * scaledRate;
    const double scaledStep = step * static_cast<double>(kRateFactor);
    const double next = scaledStep + static_cast<double>(staff.m_happiness);
    staff.m_happiness = static_cast<float>(next);
    // The compare with 1.0 looks at the double, the compare with 0.0 at the stored float; a NaN passes the first and fails the second.
    if (next > 1.0) staff.m_happiness = 1.0f;
    if (!(staff.m_happiness >= 0.0f)) staff.m_happiness = 0.0f;
    return true;
}

// Each profession Update runs once per frame from the object loop. It updates shared staff state,
// refreshes profession feedback and runs the current script state's update commands. Builders also reallocate jobs.
// The environment supplies StaffUpdate, FatalUpdate, UpdateFeedback, RunOnUpdate and ReallocateBuilders.
// A failed StaffUpdate reports a fatal error; script updates skip paused worlds. Successful dispatch always returns true.
template<class Env> bool UpdateProfession(Env& env)
{
    if (!env.StaffUpdate()) env.FatalUpdate();
    env.UpdateFeedback();
    env.RunOnUpdate();
    return true;
}

// UpdateBuilder: after the script the builder tells the construction manager that its shift moved (the player changed the hours of a builder), so the
// jobs are shared out again. The start time is read again after the call (the call may change it).
template<class Env> bool UpdateBuilder(object::Builder& builder, Env& env)
{
    UpdateProfession(env);
    if (builder.m_StartTime != builder.m_LastStartTime) env.ReallocateBuilders();
    builder.m_LastStartTime = builder.m_StartTime;
    return true;
}

} // namespace obl::staff
