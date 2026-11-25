#pragma once

// The generic functions of the object scripts of mods (`Function <name>;` in a mod's scripts/object/*.obs, docs/modding/reference/object-scripts.md).
// A function takes its arguments from the registers Arg1..Arg4 and leaves its answer in Result. The registers belong to one run of a handler
// (OnEntry, OnUpdate, a message handler): every run starts with the registers zeroed, so nothing is saved and a reload goes on the same.
// The functions use the simulation's state and its random numbers only. A call outside a limit or with a wrong argument stops the game with a
// message that names the function and the script line (Call returns the text; the caller reports it).
#include "game/objects/script_types.h"

#include <cstdint>
#include <string>

namespace obl::object {
struct Base;
}

namespace obl::mod_script {

// Registers, as script values. The numbers follow the engine's value table (script_program_api.cpp) and skip 0x110, the interpreter's "no value".
constexpr int32_t kArg1 = 280, kArg2 = 281, kArg3 = 282, kArg4 = 283, kResult = 284;
// Functions: the numbers continue far after the original ones so a pack script never meets them.
constexpr int32_t kFirstFunction = 1000;
constexpr int32_t kFindNearestBuilding = 1000, kFindNearestCommodity = 1001, kWalkToObject = 1002, kChangeGuestsHappiness = 1003,
                  kChangeGuestsDesire = 1004, kPayMoney = 1005, kChargeMoney = 1006, kCreateObject = 1007, kRemoveObject = 1008,
                  kSendToNearby = 1009, kLastFunction = 1009;

// The limits a script cannot pass.
constexpr int kMaxRadius = 16; // tiles
constexpr double kMaxMoney = 10000.0; // one PayMoney or ChargeMoney call
constexpr int kMaxCreatedPerDay = 20; // objects of mod types that arrived today and are still in the resort
constexpr int kMaxObjectsPerType = 40; // objects of one mod type in the resort

inline bool IsRegister(int32_t key)
{
    return key >= kArg1 && key <= kResult;
}
inline bool IsFunction(int32_t id)
{
    return id >= kFirstFunction && id <= kLastFunction;
}

struct Registers {
    script::ScriptValue arg[4] = {{2, 0}, {2, 0}, {2, 0}, {2, 0}};
    script::ScriptValue result = {2, 0};

    script::ScriptValue Get(int32_t key) const { return key == kResult ? result : arg[key - kArg1]; }
    void Set(int32_t key, int32_t type, int32_t bits) { (key == kResult ? result : arg[key - kArg1]) = {type, bits}; }
};

// Pure checks shared by the functions (unit-tested). Each returns an empty string when the value is acceptable, else what is wrong.
std::string CheckRadius(double radius);
std::string CheckMoney(double amount);
std::string CheckUnitDelta(double delta);
// A happiness or desire after a change, kept in 0..1.
float AddClamped(float value, float delta);

// Runs function `id` for `self`; the answer goes to registers.result. Returns "" or the error text.
std::string Call(object::Base& self, int32_t id, Registers& registers);

} // namespace obl::mod_script
