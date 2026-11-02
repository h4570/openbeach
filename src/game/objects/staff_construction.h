#pragma once

#include "core/ascii_case.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/mover_lifecycle.h"

#include <cstddef>
#include <bit>
#include <cstdint>
#include <cstring>

// The constructor and destructor bodies of Staff and of the professions with plain members (cleaner, mechanic,
// lifeguard, representative, security guard) and F2b: `Builder` (unregistering from the construction manager).
// and G1: `ServiceStaff` (no counter, no name buffer). `SnootyStaff` (G2) is constructed elsewhere.
// Pure logic: whatever leaves the object goes through an `Env` that the game side maps onto the game modules
// (src/game/objects/staff_construct.cpp) and the unit test onto a fake.
//
// Env:
// Staff's part runs in Staff's constructor (after Person's), a profession's in its own, after Staff's; teardown runs the other way.
//   Construct:  void RecalculateHappiness(Staff&)                     recalculates the staff happiness target
//               const StaffDef* StaffDefinition(const char* name)           staff key lookup + staff definition lookup
//               uint32_t GameDay()  uint32_t DayTimeBits()                  zero-based day and bit representation of the fractional hour
//               int Rand()   void* ScriptLookup(const char* name)           the game RNG; the object script of that name
//               void* CreateSprite(const char* name)  void CopyKeyline(void* sprite)   creates a bank sprite and copies ObjectManager::m_globalKeyline into its keyline flag
//               int32_t& Counter(Kind)  void UpdateStaffButtons(uint8_t type, bool present)  void RemoveFeedbackByText(const char* key)
//               uint32_t FineBits(const char* key)                          the float cost of FINE_<key> (fine key lookup, fine definition lookup)
//               uint32_t NameCount(bool female)  const char* NameKey(bool female, uint32_t index)   WorldParams::m_nameCounts / m_names
//               void* New(size_t)  void Delete(void*)  void Widen(const char*, Char16*, size_t)  const Char16* Lookup(const Char16* key)
//                                                                           the game heap, MultiByteToWideChar(CP_ACP), wide text lookup on g_TextTable
//   Destruct:   void RemoveFeedbackByObject(uint32_t id)  void ReleaseSprite(void*)   feedback::Feedback::RemoveByObject, returning the sprite to its bank
// The order of the draws and of the effects is fixed: the RNG tape and the GUI depend on the sequence, and tests pin it.
namespace obl::staff::construct {

using object::Staff;
using obl::Char16;

enum class Kind { Staff, Cleaner, Mechanic, Lifeguard, Rep, Security, Builder, Service };

// Staff type numbers used when updating the staff buttons.
constexpr uint8_t kButtonLifeguard = 7, kButtonMechanic = 8, kButtonRep = 9, kButtonBuilder = 10, kButtonCleaner = 11, kButtonSecurity = 12;

inline void SetScript(Staff& employee, void* script)
{
    employee.m_definition = reinterpret_cast<const tables::GuestDef*>(script);
}
inline void SetNameBuffer(Staff& employee, Char16* name)
{
    employee.m_script = reinterpret_cast<script::ScriptProgram*>(name);
}
inline Char16* NameBuffer(const Staff& employee)
{
    return reinterpret_cast<Char16*>(employee.m_script);
}

inline size_t Length16(const Char16* text)
{
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

// swprintf(buffer, L"ID_<PROFESSION>_%d", number) for the text keys of the names (the prefix without the number).
inline void FormatKey(Char16* buffer, const char* prefix, int number)
{
    size_t at = 0;
    for (; *prefix; ++prefix) buffer[at++] = static_cast<Char16>(*prefix);
    char digits[12];
    size_t count = 0;
    uint32_t magnitude = number < 0 ? 0u - static_cast<uint32_t>(number) : static_cast<uint32_t>(number);
    do {
        digits[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (number < 0) buffer[at++] = u'-';
    while (count) buffer[at++] = static_cast<Char16>(digits[--count]);
    buffer[at] = 0;
}

// The shared tail of every profession's constructor: a name key drawn from the male or female list (unsigned remainder), a second draw (signed
// remainder) for the family-name text, and the display name "<first> <second>" in a buffer that the destructor gives back. The temporary wide copy of
// the key is allocated and freed around the two lookups (the heap sees a fixed sequence).
template<class Env> void BuildName(Staff& employee, bool female, const char* prefix, int modulus, Env& env)
{
    const uint32_t count = env.NameCount(female);
    const uint32_t pick = static_cast<uint32_t>(env.Rand()) % count;
    const char* key = env.NameKey(female, pick);
    const size_t keyLength = std::strlen(key) + 1;
    Char16* wide = static_cast<Char16*>(env.New(keyLength * 2));
    env.Widen(key, wide, keyLength);
    const int number = env.Rand() % modulus + 1;
    Char16 family[64];
    FormatKey(family, prefix, number);
    const Char16* first = env.Lookup(wide);
    const Char16* second = env.Lookup(family);
    env.Delete(wide);
    const size_t secondLength = Length16(second), firstLength = Length16(first);
    Char16* name = static_cast<Char16*>(env.New((firstLength + secondLength) * 2 + 4));
    SetNameBuffer(employee, name);
    std::memcpy(name, first, firstLength * sizeof(Char16));
    name[firstLength] = u' ';
    std::memcpy(name + firstLength + 1, second, (secondLength + 1) * sizeof(Char16));
}

// Construct (with the object name). The waiter has no definition: its wage and expectations are zero, and the
// fields only a definition fills (`m_staffDefinition`, the hours counters, `m_happinessChanging`) keep the zero of their member
// initializers.
template<class Env> Staff* Construct(Staff& employee, const char* name, Env& env)
{
    employee.m_currentMessage = nullptr;
    if (ascii::EqualsNoCase(name, "Waiter")) {
        employee.m_Wage = 0;
        employee.m_walkpoint = 0;
        employee.m_happiness = 0;
        employee.m_StartTime = 0;
        employee.m_EndTime = 0;
        employee.m_foundDirecting = 0;
        employee.m_IdleTimePerHour = 0;
        employee.m_SalaryExpectation = 0.0f;
        employee.m_salaryExpectationGrowth = 0.0f;
        employee.m_scale = 0;
    } else {
        const tables::StaffDef* def = env.StaffDefinition(name);
        employee.m_staffDefinition = def;
        employee.m_Wage = def->m_Wage;
        employee.m_walkpoint = 0;
        std::memcpy(&employee.m_happiness, &def->m_initialHappiness, sizeof(float));
        employee.m_StartTime = def->m_StartHour;
        employee.m_EndTime = def->m_EndHour;
        employee.m_hoursWorkedInRow = 0;
        employee.m_OverWorkReduct = 0;
        employee.m_happinessChanging = 0;
        employee.m_foundDirecting = 0;
        employee.m_IdleTimePerHour = 0;
        // The recommended wage is read unsigned and converted as a 64-bit integer.
        employee.m_SalaryExpectation = static_cast<float>(static_cast<uint32_t>(def->m_RecommendedWage));
        std::memcpy(&employee.m_salaryExpectationGrowth, &def->m_expectationGrowth, sizeof(float));
        std::memcpy(&employee.m_scale, &def->m_scale, sizeof(float));
    }
    employee.m_DirectStaff = 0;
    employee.m_GotoObject = -1;
    employee.m_LastAction = 0;
    employee.m_gotTheSack = 0;
    employee.m_endedShift = 0;
    employee.m_gonePastShift = 0;
    employee.m_HiredOn = static_cast<int32_t>(env.GameDay()) + 1;
    employee.m_NumFeedBackMesslog = 0;
    for (auto& thought : employee.m_Thoughts) thought[0] = 0;
    employee.m_directedToObject = 0;
    employee.m_cantDoJob = 0;
    employee.m_DebugInfo = 0;
    env.RecalculateHappiness(employee);
    return &employee;
}

// The sprite of every profession is the one named by the definition; mechanics, cleaners, lifeguards and guards also copy the global keyline flag.
template<class Env> void AttachSprite(Staff& employee, Env& env, bool keyline)
{
    void* sprite = env.CreateSprite(employee.m_staffDefinition->m_Sprite.c_str());
    employee.m_sprite = sprite;
    if (keyline) env.CopyKeyline(sprite);
}

// The first of a profession gives its toolbar button and clears the "no staff" complaint; the counters are the statics of the classes.
template<class Env> void CountIn(Kind kind, uint8_t button, const char* complaint, Env& env)
{
    if (env.Counter(kind) == 0) {
        env.UpdateStaffButtons(button, true);
        env.RemoveFeedbackByText(complaint);
    }
}

template<class Env> void CountOut(Kind kind, uint8_t button, Env& env)
{
    int32_t& counter = env.Counter(kind);
    --counter;
    if (counter == 0) env.UpdateStaffButtons(button, false);
}

// ---- Cleaner ( /, CleanerLogic, female names) ---------------------------------------------------------------------------

template<class Env> object::Cleaner* ConstructCleaner(object::Cleaner& employee, const char* name, Env& env)
{
    employee.m_litterFound = 0;
    employee.m_toiletFound = 0;
    employee.m_binFound = 0;
    employee.m_cleanToilets = 1;
    employee.m_cleanLitter = 1;
    employee.m_cleanBins = 1;
    employee.m_ToiletsToday = 0;
    employee.m_BinsToday = 0;
    employee.m_LitterToday = 0;
    employee.m_BusyToilet = 0;
    employee.m_FeedbackStatus = 0x2000007e;
    employee.m_RequiredFeedback = 1;
    employee.m_CurrentFeedback = 0;
    employee.m_ActionState = 0;
    employee.m_UrgentType = 0;
    employee.m_urgentClean = 0;
    SetScript(employee, env.ScriptLookup("CleanerLogic"));
    employee.m_lastTaskTime = std::bit_cast<float>(moving::lifecycle::QuietNaN(env.DayTimeBits()));
    CountIn(Kind::Cleaner, kButtonCleaner, "FEEDBACK_NO_CLEANERS", env);
    ++env.Counter(Kind::Cleaner);
    employee.m_maxVel = 50.0f;
    employee.m_maxForce = 30.0f;
    AttachSprite(employee, env, true);
    BuildName(employee, true, "ID_CLEANER_", 20, env);
    return &employee;
}

// The count goes down before the "last one gone" button update; the name buffer is the one the constructor allocated.
template<class Env> void DestructCleaner(object::Cleaner& employee, Env& env)
{
    CountOut(Kind::Cleaner, kButtonCleaner, env);
    env.Delete(NameBuffer(employee));
}

// ---- Mechanic ( /, MechanicLogic, male names) -----------------------------------------------------------------------------

template<class Env> object::Mechanic* ConstructMechanic(object::Mechanic& employee, const char* name, Env& env)
{
    employee.m_FeedbackStatus = 0x3e;
    employee.m_RequiredFeedback = 1;
    employee.m_CurrentFeedback = 0;
    employee.m_inspectionsToday = 0;
    employee.m_repairsToday = 0;
    employee.m_vandalsToday = 0;
    employee.m_wateredToday = 0;
    SetScript(employee, env.ScriptLookup("MechanicLogic"));
    employee.m_lastTaskTime = std::bit_cast<float>(moving::lifecycle::QuietNaN(env.DayTimeBits()));
    CountIn(Kind::Mechanic, kButtonMechanic, "FEEDBACK_NO_MECHANICS", env);
    ++env.Counter(Kind::Mechanic);
    employee.m_maxVel = 50.0f;
    employee.m_maxForce = 30.0f;
    AttachSprite(employee, env, true);
    // The three jobs are on. Only bits 0-2 are read or saved; the whole mask is written so that reports show no other bits.
    employee.m_JobMask = 7;
    BuildName(employee, false, "ID_MECHANIC_", 16, env);
    return &employee;
}

template<class Env> void DestructMechanic(object::Mechanic& employee, Env& env)
{
    CountOut(Kind::Mechanic, kButtonMechanic, env);
    env.RemoveFeedbackByObject(employee.m_id);
    env.Delete(NameBuffer(employee));
}

// ---- Lifeguard ( /, LifeGuardLogic, names by the definition's gender) -----------------------------------------------------

template<class Env> object::Lifeguard* ConstructLifeguard(object::Lifeguard& employee, const char* name, Env& env)
{
    employee.m_saving = 0;
    employee.m_RescueThenDeath = 0;
    employee.m_LeaveState = 0;
    employee.m_StopAction = 0;
    employee.m_ActionState = 0;
    employee.m_attachedSprite = nullptr;
    employee.m_AttachedOffsetX = 0;
    employee.m_AttachedOffsetY = 0;
    employee.m_ReturnAnimation[0] = 0;
    employee.m_CurrAnimation[0] = 0;
    SetScript(employee, env.ScriptLookup("LifeGuardLogic"));
    AttachSprite(employee, env, true);
    employee.m_maxVel = 50.0f;
    employee.m_maxForce = 30.0f;
    CountIn(Kind::Lifeguard, kButtonLifeguard, "FEEDBACK_NO_LIFEGUARDS", env);
    ++env.Counter(Kind::Lifeguard);
    employee.m_FeedbackStatus = 0xfe;
    employee.m_RequiredFeedback = 1;
    employee.m_CurrentFeedback = 0;
    employee.m_NumSaves = 0;
    employee.m_NumDrowned = 0;
    employee.m_timesCalledOut = 0;
    employee.m_timesPathBlocked = 0;
    employee.m_GuestDrowned = 0;
    const tables::StaffDef* def = env.StaffDefinition(name);
    BuildName(employee, def->m_female != 0, "ID_LIFE_GUARD_", 24, env);
    employee.m_inMyArea = 0;
    employee.m_inSea = 0;
    employee.m_enteringSea = 0;
    employee.m_AtHotel = 0;
    employee.m_DrawGuest = 0;
    return &employee;
}

template<class Env> void DestructLifeguard(object::Lifeguard& employee, Env& env)
{
    CountOut(Kind::Lifeguard, kButtonLifeguard, env);
    if (employee.m_attachedSprite) env.ReleaseSprite(employee.m_attachedSprite);
    env.Delete(NameBuffer(employee));
}

// ---- Rep ( /, RepresentiveLogic, names by gender; its sprite gets no keyline flag) ------------------------------------------

template<class Env> object::Rep* ConstructRep(object::Rep& employee, const char* name, Env& env)
{
    employee.m_talking_id = 0;
    employee.m_LeaveState = 0;
    employee.m_StopAction = 0;
    employee.m_ActionState = 0;
    employee.m_attachedSprite = nullptr;
    employee.m_AttachedOffsetX = 0;
    employee.m_AttachedOffsetY = 0;
    employee.m_ReturnAnimation[0] = 0;
    employee.m_CurrAnimation[0] = 0;
    SetScript(employee, env.ScriptLookup("RepresentiveLogic"));
    std::memset(employee.m_counters, 0, sizeof(employee.m_counters));
    CountIn(Kind::Rep, kButtonRep, "FEEDBACK_NO_REPS", env);
    AttachSprite(employee, env, false);
    employee.m_maxVel = 50.0f;
    employee.m_maxForce = 30.0f;
    ++env.Counter(Kind::Rep);
    employee.m_FeedbackStatus = 0;
    employee.m_RequiredFeedback = 1;
    employee.m_CurrentFeedback = 0;
    employee.m_GuestsTalkedToToday = 0;
    employee.m_MaxTalkTime = 10;
    employee.m_MinTalkTime = 3;
    employee.m_happinessBoostToday = 0;
    employee.m_timeWithGuests = 0.5f;
    employee.m_sentToGuest = 0;
    employee.m_GuestOffSetPosX = 0.0f;
    employee.m_GuestOffSetPosY = 0.0f;
    employee.m_GuestOffSetPosZ = 0.0f;
    employee.m_GuestOpinionValue = 0;
    const tables::StaffDef* def = env.StaffDefinition(name);
    BuildName(employee, def->m_female != 0, "ID_REP_", 21, env);
    return &employee;
}

template<class Env> void DestructRep(object::Rep& employee, Env& env)
{
    CountOut(Kind::Rep, kButtonRep, env);
    env.Delete(NameBuffer(employee));
}

// ---- SecurityGuard ( /, SecurityLogic, male names) ------------------------------------------------------------------------

template<class Env> object::SecurityGuard* ConstructSecurity(object::SecurityGuard& employee, const char* name, Env& env)
{
    employee.m_misdemeanourFound = 0;
    employee.m_shouldReprimand = 0;
    employee.m_shouldDetain = 0;
    employee.m_numMinorFinesToday = 0;
    employee.m_numSeriousFinesToday = 0;
    employee.m_numVandals = 0;
    employee.m_numFights = 0;
    employee.m_numJailed = 0;
    employee.m_FeedbackStatus = 0x1e;
    employee.m_RequiredFeedback = 1;
    employee.m_CurrentFeedback = 0;
    employee.m_ActionState = 0;
    employee.m_fineTotalToday = 0.0f;
    SetScript(employee, env.ScriptLookup("SecurityLogic"));
    employee.m_offenderId = -1;
    employee.m_fightWithId = -1;
    employee.m_toleranceClass = 1;
    employee.m_tolerance = 0.5f;
    CountIn(Kind::Security, kButtonSecurity, "FEEDBACK_NO_SECURITY", env);
    employee.m_maxVel = 50.0f;
    AttachSprite(employee, env, true);
    employee.m_minorFineAmount = std::bit_cast<float>(env.FineBits("MINOR"));
    employee.m_seriousFineAmount = std::bit_cast<float>(env.FineBits("SERIOUS"));
    employee.m_FineLevel = 0;
    employee.m_numCautioned = 0;
    ++env.Counter(Kind::Security);
    BuildName(employee, false, "ID_SECURITY_GUARD_", 31, env);
    return &employee;
}

template<class Env> void DestructSecurity(object::SecurityGuard& employee, Env& env)
{
    CountOut(Kind::Security, kButtonSecurity, env);
    env.Delete(NameBuffer(employee));
}

// ---- Builder ( /, BuilderLogic, male names; F2b) -----------------------------------------------------------------------------
// The builder also needs
//   void RemoveBuilder(int32_t id)                removes the builder from World::m_constructionManager

// Unlike the other professions there is no "no builders" complaint to clear, and the work-day fields are set.
template<class Env> object::Builder* ConstructBuilder(object::Builder& employee, const char* name, Env& env)
{
    SetScript(employee, env.ScriptLookup("BuilderLogic"));
    employee.m_maxForce = 65.0f;
    employee.m_maxVel = 65.0f;
    AttachSprite(employee, env, true);
    if (env.Counter(Kind::Builder) == 0) env.UpdateStaffButtons(kButtonBuilder, true);
    ++env.Counter(Kind::Builder);
    employee.m_BuilderNumber = 0;
    employee.m_ActionState = 0;
    employee.m_Night = 0;
    employee.m_InHut = 0;
    employee.m_CurrentFeedback = 0;
    employee.m_FeedbackStatus = 6;
    employee.m_RequiredFeedback = 1;
    employee.m_lastTaskTime = std::bit_cast<float>(moving::lifecycle::QuietNaN(env.DayTimeBits()));
    employee.m_WaitTillMorning = 0;
    employee.m_NumTimesPlayedAnimInRow = 0;
    employee.m_JobType = 0;
    employee.m_LastStartTime = employee.m_StartTime;
    employee.m_checkForJobs = 0;
    BuildName(employee, false, "ID_BUILDER_", 25, env);
    employee.m_FeedbackStatus |= 6;
    return &employee;
}

// The builder leaves the construction manager's table first (the table may send messages and give its job back), then the count goes down.
template<class Env> void DestructBuilder(object::Builder& employee, Env& env)
{
    env.RemoveBuilder(static_cast<int32_t>(employee.m_id));
    CountOut(Kind::Builder, kButtonBuilder, env);
    env.Delete(NameBuffer(employee));
}

// ---- ServiceStaff ( /, GuestLogic; barman, barwoman, chef and chef-ette stand still) ----------------------------------------
// Env::PlayStand starts the stand animation at the staff position and current game time.

inline bool StandsStill(const char* sprite)
{
    return ascii::EqualsNoCase(sprite, "BARMAN") || ascii::EqualsNoCase(sprite, "BARWOMAN") || ascii::EqualsNoCase(sprite, "CHEF") ||
            ascii::EqualsNoCase(sprite, "CHEFFETTE");
}

template<class Env> object::ServiceStaff* ConstructService(object::ServiceStaff& employee, const char* name, Env& env)
{
    employee.m_logic = env.ScriptLookup("GuestLogic");
    AttachSprite(employee, env, false);
    if (StandsStill(employee.m_staffDefinition->m_Sprite.c_str())) env.PlayStand(employee);
    employee.m_actionState = 0;
    return &employee;
}

} // namespace obl::staff::construct
