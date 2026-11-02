#pragma once

#include "core/text16.h"
#include "core/float_to_int.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

// (docs/architecture/objects.md): each class's `GetVariableText(key, out)` of the staff menu (
// `key` is the data keyword of the GUI script, `out` the text buffer of the widget). This file has the base class (Staff GetVariableText),
// the builder (), the representative (), the cleaner (), the lifeguard (), the mechanic () and the security guard
// (). Each profession answers its own keywords (and
// the state texts of its activity) and hands every other key to the base class. Pure logic over object::Staff; what reaches the game goes through
// an `Env`:
//   env.Lookup(key)                       text lookup of the game's text table (the answer is the wide text)
//   env.Print(out, format)                swprintf(out, format) of the game's CRT (the format is a table text, used as is)
//   env.PrintInt(out, format, value)      swprintf(out, format, int)
//   env.PrintReal(out, format, value)     swprintf(out, format, double)
//   env.BuildingKey(id, key)              the data key of the building with this object id (false when no such object)
//   env.BuildingName(id, out)             the building's own GetVariableText("BUILDING_NAME", out) through its vtable (false when no such object)
//   env.AskFeedback(flags)                the object's virtual GetFeedbackText(flags): the answer is thrown away, but the call draws
//   env.BeachOpen(id, open)               Building(0) of the building with this object id: is its beach area open (false when no such object)
//   env.Fatal(key)                        FatalError("A script read unrecognized staff variable \"%s\"", key)
// A staff member whose home or building is gone leaves such a text out.
namespace obl::staff {

namespace variable_text {

constexpr const char* kActivity = "STAFF_ACTIVITY";
constexpr const char* kWages = "STAFF_WAGES";
constexpr size_t kWagesPrefix = 11; // strncmp: any key that starts with STAFF_WAGES is the wage
constexpr const char* kDayWages = "STAFF_DAY_WAGES_$";
constexpr const char* kHappyPercent = "STAFF_HAPPY_PERCENT";
constexpr const char* kHome = "STAFF_HOME";
constexpr const char* kHiredOn = "STAFF_HIRED_ON";
constexpr const char* kStartHour = "STAFF_START_HOUR";
constexpr const char* kEndHour = "STAFF_END_HOUR";
constexpr const char* kHourSep = "STAFF_HOUR_SEP";
constexpr const char* kFeedbackMess[3] = {"STAFF_FEEDBACK_MESS0", "STAFF_FEEDBACK_MESS1", "STAFF_FEEDBACK_MESS2"};
constexpr size_t kFeedbackMessPrefix = 20; // strncmp of the whole name
constexpr const char* kDismissConfirm = "STAFF_DISMISS_CONFIRM";
constexpr const char* kName = "STAFF_NAME";
constexpr const char* kJobTitle = "STAFF_JOB_TITLE";

constexpr const char* kBuilderCurrentBuild = "BUILDER_CURRENT_BUILD";
constexpr const char* kRepGuestsToday = "GUESTS_TODAY";
constexpr const char* kRepHappinessBoostToday = "HAPPINESS_BOOST_TODAY";
constexpr const char* kRepGuestTime = "REP_GUEST_TIME";

constexpr const char* kCleanerToilets = "CLEANER_TOILETS";
constexpr const char* kCleanerBins = "CLEANER_BINS";
constexpr const char* kCleanerLitter = "CLEANER_LITTER";
constexpr const char* kToiletsCleaned = "TOILETS_CLEANED";
constexpr const char* kBinsEmptied = "BINS_EMPTIED";
constexpr const char* kLitterSwept = "LITTER_SWEPT";

constexpr const char* kRescueAttemptsToday = "RESCUE_ATTEMPTS_TODAY";
constexpr const char* kRescuesToday = "RESCUES_TODAY";
constexpr const char* kBeachOpen = "STAFF_BEACH_OPEN";
constexpr const char* kBeachOpenStatus = "STAFF_BEACH_OPEN_STATUS_";
constexpr size_t kBeachOpenStatusPrefix = 0x18; // strncmp

constexpr const char* kMechanicVandals = "MECHANIC_VANDALS";
constexpr const char* kMechanicRepair = "MECHANIC_REPAIR";
constexpr const char* kMechanicWater = "MECHANIC_WATER";
constexpr const char* kInspectToday = "INSPECT_TODAY";
constexpr const char* kRepairsToday = "REPAIRS_TODAY";
constexpr const char* kVandalRepairsToday = "VANDAL_REPAIRS_TODAY";
constexpr const char* kWateredToday = "WATERED_TODAY";

constexpr const char* kGuardCautionedToday = "GUARD_CAUTIONED_TODAY";
constexpr const char* kGuardDetainedToday = "GUARD_DETAINED_TODAY";
constexpr const char* kGuardFinesToday = "GUARD_FINES_TODAY_$";
constexpr const char* kSecurityHarshness = "SECURITY_HARSHNESS";
constexpr const char* kGuardFineMinor = "GUARD_FINE_MINOR_$";
constexpr size_t kGuardFineMinorPrefix = 0x10; // strncmp
constexpr const char* kGuardFineSerious = "GUARD_FINE_SERIOUS_$";
constexpr size_t kGuardFineSeriousPrefix = 0x12; // strncmp

// The activity states of the builder and the representative (the profession's own range of m_state; the base class's are the numbers of the switch).
constexpr uint32_t kBuilderFirstState = 0x5f, kBuilderStateCount = 9;
constexpr uint32_t kRepFirstState = 7, kRepStateCount = 5;
constexpr uint32_t kCleanerFirstState = 0x68, kCleanerStateCount = 14;
constexpr uint32_t kLifeguardFirstState = 0x4e, kLifeguardStateCount = 10;
constexpr uint32_t kMechanicFirstState = 0x58, kMechanicStateCount = 7;
constexpr uint32_t kGuardFirstState = 0x76, kGuardStateCount = 13;

inline bool Is(const char* key, const char* name)
{
    return std::strcmp(key, name) == 0;
}
inline bool Starts(const char* key, const char* prefix, size_t length)
{
    return std::strncmp(key, prefix, length) == 0;
}

// The script word of a staff member (Person::m_script for the guests) is the wide text of its name, which the profession hands to swprintf as the format.
inline const Char16* NameFormat(const object::Staff& member)
{
    return reinterpret_cast<const Char16*>(member.m_script);
}
// wcscpy of one of the stored thoughts (their character type is the layout's own).
template<class Char> void CopyText(Char16* out, const Char* text)
{
    while ((*out++ = static_cast<Char16>(*text++)) != 0) {}
}

} // namespace variable_text

// Staff GetVariableText
template<class Env> void BaseVariableText(const object::Staff& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    if (Is(key, kActivity)) {
        const char* text = nullptr;
        switch (static_cast<uint32_t>(member.m_state)) {
        case 0: out[0] = 0; return; // k_EmptyText (an empty string)
        case 1: text = "STARTING_WORK"; break;
        case 6:
        case 0x4c: text = "SACKED"; break;
        case 0x4d: text = "REQUIRES_ATTENTION"; break;
        case 0x83:
        case 0x84: text = "BEING_DIRECTED"; break;
        default: text16::Copy(out, u"Not done yet"); return;
        }
        text16::Copy(out, env.Lookup(text));
        return;
    }
    if (Starts(key, kWages, kWagesPrefix)) {
        env.PrintInt(out, u"%d", member.m_Wage);
        return;
    }
    if (Is(key, kDayWages)) {
        env.PrintInt(out, u"$%d", static_cast<int>(static_cast<uint32_t>(member.m_Wage) * 10u));
        return;
    }
    if (Is(key, kHappyPercent)) {
        env.PrintInt(out, u"%d%%", FloatToIntWrap(static_cast<double>(member.m_happiness) * 100.0));
        return;
    }
    if (Is(key, kHome)) {
        std::string building;
        if (!env.BuildingKey(member.m_HomeID, building)) return;
        text16::Copy(out, env.Lookup(("BUILDING_NAME_" + building).c_str()));
        return;
    }
    if (Is(key, kHiredOn)) {
        env.PrintInt(out, env.Lookup("STAFF_DAY_TEXT"), member.m_HiredOn);
        return;
    }
    if (Is(key, kStartHour)) {
        env.PrintInt(out, u"%d", member.m_StartTime);
        return;
    }
    if (Is(key, kEndHour)) {
        env.PrintInt(out, u"%d", member.m_EndTime);
        return;
    }
    if (Is(key, kHourSep)) {
        env.Print(out, env.Lookup("STAFFB_HOUR_SEP"));
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (Starts(key, kFeedbackMess[i], kFeedbackMessPrefix)) {
            CopyText(out, member.m_Thoughts[i]);
            return;
        }
    }
    if (Is(key, kDismissConfirm)) {
        env.PrintInt(out, env.Lookup("STAFFB_DISMISS_CONFIRM"),
                static_cast<int>(static_cast<uint32_t>(member.m_staffDefinition->m_RecommendedWage) * 10u));
        return;
    }
    env.Fatal(key);
}

// Builder GetVariableText
template<class Env> void BuilderVariableText(const object::Builder& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    if (Is(key, kActivity)) {
        static const char* const kTexts[kBuilderStateCount] = {
                "BUILDER_RESTING", // 0x5f
                "BUILDER_HEADING_HOME", // 0x60
                "BUILDER_GOING_TO_BUILD", // 0x61
                "BUILDER_GOING_TO_DEMOLISH", // 0x62
                "BUILDER_BUILDING", // 0x63
                "BUILDER_DEMOLISHING", // 0x64
                "BUILDER_RESTING", // 0x65
                "BUILDER_RESTING", // 0x66
                "BUILDER_IDLE", // 0x67
        };
        const uint32_t index = static_cast<uint32_t>(member.m_state) - kBuilderFirstState;
        if (index >= kBuilderStateCount) {
            BaseVariableText(member, key, out, env);
            return;
        }
        text16::Copy(out, env.Lookup(kTexts[index]));
        return;
    }
    if (Is(key, kBuilderCurrentBuild)) {
        env.BuildingName(member.m_DestObjectID, out);
        return;
    }
    if (Is(key, kName)) {
        env.Print(out, NameFormat(member));
        return;
    }
    if (!Is(key, kJobTitle)) {
        BaseVariableText(member, key, out, env);
        return;
    }
    env.Print(out, env.Lookup("STAFFB_TITLE_BUILDER"));
}

// Rep GetVariableText
template<class Env> void RepVariableText(const object::Rep& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    // Every call starts with a leftover: the object is asked for the text of its current feedback (a draw at most, the answer unused) and the buffer is
    // cleared with an empty format.
    env.AskFeedback(static_cast<uint32_t>(member.m_CurrentFeedback));
    env.Print(out, u"");
    if (Is(key, kActivity)) {
        const char* text = nullptr;
        switch (static_cast<uint32_t>(member.m_state) - kRepFirstState) {
        case 0: text = "REPRESENTATIVE_RESTING"; break;
        case 1: text = "REPRESENTATIVE_PATROLLING"; break;
        case 2: text = "REPRESENTATIVE_TALKING"; break;
        case 3: env.Print(out, u""); return;
        case 4: text = "REPRESENTATIVE_IDLE"; break;
        default: BaseVariableText(member, key, out, env); return;
        }
        env.Print(out, env.Lookup(text));
        return;
    }
    if (Is(key, kName)) {
        env.Print(out, NameFormat(member));
        return;
    }
    if (Is(key, kRepGuestsToday)) {
        env.PrintInt(out, u"%d", member.m_GuestsTalkedToToday);
        return;
    }
    if (Is(key, kRepHappinessBoostToday)) {
        float boost;
        std::memcpy(&boost, &member.m_happinessBoostToday, sizeof(boost));
        env.PrintInt(out, u"%d%%", FloatToIntWrap(static_cast<double>(boost)));
        return;
    }
    if (Is(key, kRepGuestTime)) {
        env.PrintReal(out, u"%f", static_cast<double>(member.m_timeWithGuests));
        return;
    }
    if (!Is(key, kJobTitle)) {
        BaseVariableText(member, key, out, env);
        return;
    }
    env.Print(out, env.Lookup("STAFFB_TITLE_REP"));
}

// Cleaner GetVariableText: the buffer is cleared first, then the object is asked for the text of its current feedback (it draws, the answer is unused).
template<class Env> void CleanerVariableText(const object::Cleaner& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    env.Print(out, u"");
    env.AskFeedback(static_cast<uint32_t>(member.m_CurrentFeedback));
    if (Is(key, kActivity)) {
        static const char* const kTexts[kCleanerStateCount] = {
                "CLEANER_CHOOSE_TASK", // 0x68
                "CLEANER_GOING_TO_REST", // 0x69
                "CLEANER_GOING_TO_REST", // 0x6a
                "CLEANER_RESTING", // 0x6b
                "CLEANER_RESTING", // 0x6c
                "CLEANER_GO_CLEAN_LITTER", // 0x6d
                "CLEANER_CLEANING_LITTER", // 0x6e
                "CLEANER_GO_CLEAN_TOILET", // 0x6f
                "CLEANER_GO_CLEAN_BIN", // 0x70
                "CLEANER_CLEANING_BIN", // 0x71
                "CLEANER_GO_CLEAN_TOILET", // 0x72
                "CLEANER_CLEANING_TOILET", // 0x73
                "CLEANER_CLEANING_TOILET", // 0x74
                "CLEANER_IDLE", // 0x75
        };
        const uint32_t index = static_cast<uint32_t>(member.m_state) - kCleanerFirstState;
        if (index >= kCleanerStateCount) {
            BaseVariableText(member, key, out, env);
            return;
        }
        text16::Copy(out, env.Lookup(kTexts[index]));
        return;
    }
    if (Is(key, kCleanerToilets))
        env.PrintInt(out, u"%d", member.m_cleanToilets);
    else if (Is(key, kCleanerBins))
        env.PrintInt(out, u"%d", member.m_cleanBins);
    else if (Is(key, kCleanerLitter))
        env.PrintInt(out, u"%d", member.m_cleanLitter);
    else if (Is(key, kName))
        env.Print(out, NameFormat(member));
    else if (Is(key, kToiletsCleaned))
        env.PrintInt(out, u"%d", member.m_ToiletsToday);
    else if (Is(key, kBinsEmptied))
        env.PrintInt(out, u"%d", member.m_BinsToday);
    else if (Is(key, kLitterSwept))
        env.PrintInt(out, u"%d", member.m_LitterToday);
    else if (Is(key, kJobTitle))
        env.Print(out, env.Lookup("STAFFB_TITLE_CLEANER"));
    else
        BaseVariableText(member, key, out, env);
}

// Lifeguard GetVariableText
template<class Env> void LifeguardVariableText(const object::Lifeguard& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    env.Print(out, u"");
    env.AskFeedback(static_cast<uint32_t>(member.m_CurrentFeedback));
    if (Is(key, kActivity)) {
        static const char* const kTexts[kLifeguardStateCount] = {
                "LIFEGUARD_PATROL", // 0x4e
                "LIFEGUARD_RESTING", // 0x4f
                "LIFEGUARD_RESCUE", // 0x50
                "LIFEGUARD_GOTO_TOWER", // 0x51
                "LIFEGUARD_LEAVING_TOWER", // 0x52
                "LIFEGUARD_ON_TOWER", // 0x53
                "LIFEGUARD_TALKING", // 0x54
                nullptr, // 0x55: empty text
                "LIFEGUARD_TELLOFF", // 0x56
                "LIFEGUARD_IDLE", // 0x57
        };
        const uint32_t index = static_cast<uint32_t>(member.m_state) - kLifeguardFirstState;
        if (index >= kLifeguardStateCount) {
            BaseVariableText(member, key, out, env);
            return;
        }
        env.Print(out, kTexts[index] ? env.Lookup(kTexts[index]) : u"");
        return;
    }
    if (Is(key, kName))
        env.Print(out, NameFormat(member));
    else if (Is(key, kRescueAttemptsToday))
        env.PrintInt(out, u"%d", member.m_NumDrowned + member.m_NumSaves);
    else if (Is(key, kRescuesToday))
        env.PrintInt(out, u"%d", member.m_NumSaves);
    else if (Is(key, kJobTitle))
        env.Print(out, env.Lookup("STAFFB_TITLE_LIFEGUARD"));
    else if (Is(key, kBeachOpen)) {
        bool open = false;
        if (env.BeachOpen(member.m_HomeID, open)) env.PrintInt(out, u"%d", open ? 1 : 0);
    } else if (Starts(key, kBeachOpenStatus, kBeachOpenStatusPrefix))
        env.PrintInt(out, u"%d", 1);
    else
        BaseVariableText(member, key, out, env);
}

// Mechanic GetVariableText (the feedback question comes before the clearing of the buffer)
template<class Env> void MechanicVariableText(const object::Mechanic& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    env.AskFeedback(static_cast<uint32_t>(member.m_CurrentFeedback));
    env.Print(out, u"");
    if (Is(key, kActivity)) {
        static const char* const kTexts[kMechanicStateCount] = {
                "MECHANIC_REPAIR", // 0x58
                "MECHANIC_RESTING", // 0x59
                "MECHANIC_HEAD_HOME", // 0x5a
                "MECHANIC_CHOOSE_JOB", // 0x5b
                "MECHANIC_GO_REST", // 0x5c
                "MECHANIC_GO_REPAIR", // 0x5d
                "MECHANIC_IDLE", // 0x5e
        };
        const uint32_t index = static_cast<uint32_t>(member.m_state) - kMechanicFirstState;
        if (index >= kMechanicStateCount) {
            BaseVariableText(member, key, out, env);
            return;
        }
        text16::Copy(out, env.Lookup(kTexts[index]));
        return;
    }
    // The three job switches are the bits of the job mask.
    if (Is(key, kMechanicVandals))
        env.PrintInt(out, u"%d", (member.m_JobMask >> 1) & 1);
    else if (Is(key, kMechanicRepair))
        env.PrintInt(out, u"%d", member.m_JobMask & 1);
    else if (Is(key, kMechanicWater))
        env.PrintInt(out, u"%d", (member.m_JobMask >> 2) & 1);
    else if (Is(key, kName))
        env.Print(out, NameFormat(member));
    else if (Is(key, kInspectToday))
        env.PrintInt(out, u"%d", member.m_inspectionsToday);
    else if (Is(key, kRepairsToday))
        env.PrintInt(out, u"%d", member.m_repairsToday);
    else if (Is(key, kVandalRepairsToday))
        env.PrintInt(out, u"%d", member.m_vandalsToday);
    else if (Is(key, kWateredToday))
        env.PrintInt(out, u"%d", member.m_wateredToday);
    else if (Is(key, kJobTitle))
        env.Print(out, env.Lookup("STAFFB_TITLE_MECHANIC"));
    else
        BaseVariableText(member, key, out, env);
}

// SecurityGuard GetVariableText
template<class Env> void SecurityGuardVariableText(const object::SecurityGuard& member, const char* key, Char16* out, Env& env)
{
    using namespace variable_text;
    env.Print(out, u"");
    env.AskFeedback(static_cast<uint32_t>(member.m_CurrentFeedback));
    if (Is(key, kActivity)) {
        static const char* const kTexts[kGuardStateCount] = {
                "SECURITYGUARD_WAVE_HELLO", // 0x76
                "SECURITYGUARD_CHOOSE_TASK", // 0x77
                "SECURITYGUARD_PATROL", // 0x78
                "SECURITYGUARD_GO_REST", // 0x79
                "SECURITYGUARD_CHOOSE_ACTION", // 0x7a
                "SECURITYGUARD_RESTING", // 0x7b
                "SECURITYGUARD_SUSPICIOUS", // 0x7c
                "SECURITYGUARD_REPRIMAND", // 0x7d
                "SECURITYGUARD_DETAIN", // 0x7e
                "SECURITYGUARD_ESCORT", // 0x7f
                "SECURITYGUARD_GO_REST", // 0x80
                "SECURITYGUARD_PATROL", // 0x81
                "SECURITYGUARD_IDLE", // 0x82
        };
        const uint32_t index = static_cast<uint32_t>(member.m_state) - kGuardFirstState;
        if (index >= kGuardStateCount) {
            BaseVariableText(member, key, out, env);
            return;
        }
        env.Print(out, env.Lookup(kTexts[index]));
        return;
    }
    if (Is(key, kName))
        env.Print(out, NameFormat(member));
    else if (Is(key, kGuardCautionedToday))
        env.PrintInt(out, u"%d", member.m_numCautioned);
    else if (Is(key, kGuardDetainedToday))
        env.PrintInt(out, u"%d", member.m_numJailed);
    else if (Is(key, kGuardFinesToday))
        env.PrintInt(out, u"$%d", FloatToIntWrap(static_cast<double>(member.m_fineTotalToday)));
    else if (Is(key, kSecurityHarshness))
        env.PrintReal(out, u"%f", static_cast<double>(member.m_tolerance));
    else if (Starts(key, kGuardFineMinor, kGuardFineMinorPrefix))
        env.PrintInt(out, u"$%d", FloatToIntWrap(static_cast<double>(member.m_minorFineAmount)));
    else if (Starts(key, kGuardFineSerious, kGuardFineSeriousPrefix))
        env.PrintInt(out, u"$%d", FloatToIntWrap(static_cast<double>(member.m_seriousFineAmount)));
    else if (Is(key, kJobTitle))
        env.Print(out, env.Lookup("STAFFB_TITLE_SECURITY"));
    else
        BaseVariableText(member, key, out, env);
}

} // namespace obl::staff
