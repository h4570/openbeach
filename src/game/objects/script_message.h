#pragma once

#include "script_types.h"
#include "script_arith.h"

#include <cstdint>
#include <cstring>
#include <string>

// The pure rules of the scenario interpreter's two message commands (scenario command execution opcodes 1 `Message` and 2 `ChangeState`,
// Scenario message construction): how the three operand values become message words, when a send is immediate or delayed, how the
// delay is drawn and which state index a `ChangeState` posts. Covered by unit tests.
namespace obl::script {

// The float-to-int truncation: a 64-bit truncation of which only the low 32 bits are kept, so a value beyond int wraps, and NaN
// or anything beyond 64 bits gives the "integer indefinite" 0x8000000000000000, i.e. 0.
inline int32_t FloatToIntWrap(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

constexpr int32_t kTypeByte = 0;

// One operand of `Message MT_x <receiver> <data> <flags>` as a message word: a byte keeps its low byte, an int stays, any other kind (a
// float, but also a string pointer) is read as float bits and truncated.
inline int32_t MessageWord(ScriptValue value)
{
    if (value.type == kTypeByte) return value.bits & 0xff;
    if (value.type == kTypeInt) return value.bits;
    return FloatToIntWrap(BitsToFloat(value.bits));
}

// The words of a scenario message that scenario message construction defines (the sender is always -1, scripts address a receiver by the first
// operand, `data` is the second and `flags` the third). `extra`/`floatData` stay zero.
struct BuiltMessage {
    int32_t sender, receiver, type, flags, data;
};

inline BuiltMessage BuildMessage(int32_t type, ScriptValue first, ScriptValue second, ScriptValue third)
{
    return {-1, MessageWord(first), type, MessageWord(third), MessageWord(second)};
}

// A compare against 0.0 that is true for "equal" and for "unordered", so NaN counts as zero.
inline bool IsZeroOrNaN(float value)
{
    return !(value < 0.0f || value > 0.0f);
}

// the float 3.0518509e-05 (= 1/32767, not 1/32768) the interpreter scales `rand` by.
constexpr float kRandUnit = 3.0518509447574615e-05f;

struct SendPlan {
    bool delayed;
    float delay;
};

// `Time`/`MaxTime` of a `Message` (both 0 when absent). A MaxTime (not zero, not NaN) gives a random delay in [Time, MaxTime]:
// Time + (MaxTime - Time) * rand * 3.0518509e-05, one draw, this order (the product of rand and the constant first). Otherwise a
// Time of zero (or NaN) posts at once, any other Time posts with that delay. `draw` is the game's rand().
template<class Draw> SendPlan PlanMessageSend(float time, float maxTime, Draw draw)
{
    if (!IsZeroOrNaN(maxTime)) {
        const double scale = static_cast<double>(draw()) * static_cast<double>(kRandUnit);
        const double span = static_cast<double>(maxTime) - static_cast<double>(time);
        return {true, static_cast<float>(span * scale + static_cast<double>(time))};
    }
    if (IsZeroOrNaN(time)) return {false, 0.0f};
    return {true, time};
}

// `ChangeState <name> [Time t]` posts MT_STATECHANGE (3) from -2 to -2 with the state's index; the same immediate/delayed rule as above
// without MaxTime. A name the script has no state of gives index -1 (the search never raises a fatal error).
constexpr int32_t kMessageTypeStateChange = 3;
constexpr int32_t kStateNotFound = -1;

inline bool SameStateName(const char* a, const char* b)
{
    return std::strcmp(a, b) == 0;
}
inline bool SameStateName(const std::string& a, const char* b)
{
    return a == b;
}

template<class Names> int32_t FindStateIndex(const Names& names, uint32_t count, const char* name)
{
    for (uint32_t i = 0; i < count; ++i)
        if (SameStateName(names[i], name)) return static_cast<int32_t>(i);
    return kStateNotFound;
}

inline SendPlan PlanChangeStateSend(float time)
{
    return {!IsZeroOrNaN(time), time};
}

} // namespace obl::script
