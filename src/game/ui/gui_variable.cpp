#include "core/ascii_case.h"
#include "core/char16.h"
#include "game/ui/gui_variable.h"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace obl::gui_variable {
namespace {

constexpr uint32_t kNegativeColor = 0xff0000;

using Handler = void (*)(const Env&, Output&);

int Format(const Env& env, Output& out, const Char16* format, double value)
{
    return env.Format()(out.text, format, value);
}

void ColorIfNegative(float amount, Output& out)
{
    if (!(amount >= 0.0f)) out.color = kNegativeColor;
}

void PlayerMoney(const Env& env, Output& out)
{
    const float cash = env.Cash();
    Format(env, out, u"%.f", static_cast<double>(cash));
    ColorIfNegative(cash, out);
}

void PlayerMoneyDollar(const Env& env, Output& out)
{
    const float cash = env.Cash();
    Format(env, out, u"$%.f", static_cast<double>(cash));
    ColorIfNegative(cash, out);
}

void PlayerMoneyTitle(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("CASH_TITLE"), static_cast<double>(env.Cash()));
}

void MaxDistance(const Env& env, Output& out)
{
    Format(env, out, u"%.0f", static_cast<double>(env.MaxDistance()));
}

void MinDistance(const Env& env, Output& out)
{
    Format(env, out, u"%.0f", static_cast<double>(env.MinDistance()));
}

// `_itow(n, buffer, 10)`.
void NumGuests(const Env& env, Output& out)
{
    Char16 digits[12];
    int length = 0;
    const int n = env.NumGuests();
    unsigned magnitude = n < 0 ? 0u - static_cast<unsigned>(n) : static_cast<unsigned>(n);
    do {
        digits[length++] = static_cast<Char16>(u'0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    int at = 0;
    if (n < 0) out.text[at++] = u'-';
    while (length > 0) out.text[at++] = digits[--length];
    out.text[at] = u'\0';
}

void NumGuestsTitle(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("GUESTS_TITLE"), env.NumGuests());
}

void Clock(const Env& env, Char16* dest, float time)
{
    const int minutes = env.QuarterMinutes(time);
    const int hour = env.WholeHour(time);
    env.Format()(dest, u"%d:%02d", hour, minutes);
}

void Time(const Env& env, Output& out)
{
    Clock(env, out.text, env.GameTime());
}

void GameDay(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.GameDay() + 1);
}

void GameDayTitle(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("DAY_TITLE"), env.GameDay() + 1);
}

int Truncate(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

void PowerOutput(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", Truncate(env.PowerOutput()));
}

void PowerDemand(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", Truncate(env.PowerDemand()));
}

void PowerUse(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", Truncate(env.PowerUse()));
}

constexpr double kWeatherStep = 0.003921568859368563; // (float)(1/255)
constexpr double kWeatherScale = 255.0;

void WeatherChannel(const Env& env, Output& out, int shift)
{
    const int channel = (env.WeatherColor() >> shift) & 0xff;
    env.Format()(out.text, u"%d", Truncate(static_cast<double>(channel) * kWeatherStep * kWeatherScale));
}

void WeatherRed(const Env& env, Output& out)
{
    WeatherChannel(env, out, 16);
}
void WeatherGreen(const Env& env, Output& out)
{
    WeatherChannel(env, out, 8);
}
void WeatherBlue(const Env& env, Output& out)
{
    WeatherChannel(env, out, 0);
}

void LitterCount(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("IS_LITTER_ITEMS"), env.LitterCount());
}

void LitterCountNoFormat(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.LitterCount());
}

// Object classes the object manager counts by: 2 buildings, 7..0xc the six kinds of staff.
constexpr int kClassBuilding = 2, kFirstStaffClass = 7, kLastStaffClass = 0xc, kMechanicClass = 8;

void BuildingCount(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.ObjectCount(kClassBuilding));
}

void StaffCount(const Env& env, Output& out)
{
    int total = 0;
    for (int objectClass = kFirstStaffClass; objectClass <= kLastStaffClass; ++objectClass) total += env.ObjectCount(objectClass);
    env.Format()(out.text, u"%d", total);
}

void GuestBreakdown(const Env& env, Output& out)
{
    const int zero = env.GuestGenderCount(false);
    const int nonZero = env.GuestGenderCount(true);
    env.Format()(out.text, env.Text("IS_GUEST_COUNT"), env.NumGuests(), zero, nonZero);
}

void SceneryCount(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("IS_SCENERY_OBJECTS"), env.SceneryNonLitterCount());
}

// "<text with three %d>" of (both kinds, kind 0, kind 1).
void GuestFlowText(const Env& env, Output& out, GuestFlowField field)
{
    const int first = env.GuestFlow(field, 0), second = env.GuestFlow(field, 1);
    env.Format()(out.text, env.Text("FP_GUEST_FORMAT"), first + second, first, second);
}

void ArrivalsToday(const Env& env, Output& out)
{
    GuestFlowText(env, out, GuestFlowField::TodayArrive);
}
void ArrivalsTotal(const Env& env, Output& out)
{
    GuestFlowText(env, out, GuestFlowField::TotalArrive);
}
void DepartToday(const Env& env, Output& out)
{
    GuestFlowText(env, out, GuestFlowField::TodayDepart);
}
void DepartTotal(const Env& env, Output& out)
{
    GuestFlowText(env, out, GuestFlowField::TotalDepart);
}

// Unhappy leavers as a percentage of all leavers; the sums are read as unsigned and a zero total gives 0.
void UnhappyDepart(const Env& env, Output& out, GuestFlowField departed, GuestFlowField unhappy)
{
    const int first = env.GuestFlow(unhappy, 0), second = env.GuestFlow(unhappy, 1);
    const uint32_t leavers = static_cast<uint32_t>(env.GuestFlow(departed, 0) + env.GuestFlow(departed, 1));
    int percent = 0;
    if (leavers != 0) {
        constexpr double kHundredth = 0.009999999776482582; // (float)0.01
        const double unhappyTotal = static_cast<double>(static_cast<uint32_t>(first + second));
        percent = Truncate(unhappyTotal / (static_cast<double>(leavers) * kHundredth));
    }
    env.Format()(out.text, env.Text("FP_UNHAPPY_FORMAT"), first + second, first, second, percent);
}

void UnDepartTotal(const Env& env, Output& out)
{
    UnhappyDepart(env, out, GuestFlowField::TotalDepart, GuestFlowField::TotalUnhappyDepart);
}

void UnDepartToday(const Env& env, Output& out)
{
    UnhappyDepart(env, out, GuestFlowField::TodayDepart, GuestFlowField::TodayUnhappyDepart);
}

// "$%.f" of a World money field; the income-like keys turn red below zero like the cash does, the cost keys never do.
template<MoneyField field, bool kColored> void MoneyText(const Env& env, Output& out)
{
    const float amount = env.Money(field);
    env.Format()(out.text, u"$%.f", static_cast<double>(amount));
    if (kColored) ColorIfNegative(amount, out);
}

// The profit is income minus costs: the text takes the difference at full precision, the colour test the float one.
void ProfitToday(const Env& env, Output& out)
{
    const float income = env.Money(MoneyField::Income), costs = env.Money(MoneyField::Costs);
    env.Format()(out.text, u"$%.f", static_cast<double>(income) - static_cast<double>(costs));
    ColorIfNegative(income - costs, out);
}

constexpr int kKeyCapacity = 128;

// The text-table entry `<prefix><key>`, formatted with `swprintf("%s")`.
void LookedUp(const Env& env, Output& out, const char* format, const char* key)
{
    char textKey[kKeyCapacity];
    std::snprintf(textKey, sizeof(textKey), format, key);
    env.Format()(out.text, u"%s", env.Text(textKey));
}

// `wcscpy(out, text)`.
void CopyText(Output& out, const Char16* text)
{
    int i = 0;
    for (; i < kTextCapacity - 1 && text[i] != u'\0'; ++i) out.text[i] = text[i];
    out.text[i] = u'\0';
}

void ColorIfUnaffordable(float cash, int cost, Output& out)
{
    if (!(static_cast<double>(cash) - static_cast<double>(cost) >= 0.0)) out.color = kNegativeColor;
}

void BuildDescription(const Env& env, Output& out)
{
    LookedUp(env, out, "BUILD_DESC_%s", env.BuildingKey(env.SelectedBuilding()));
}

// Name of the building to build first, red when this one is not buildable yet (not allowed, or no finished building of the
// preceding type: one still under construction or demolition does not count).
void BuildTree(const Env& env, Output& out)
{
    const int selected = env.SelectedBuilding();
    const char* parent = env.BuildingKey(env.BuildingIdByKey(env.BuildingTreeKey(selected)));
    if (!env.BuildingAllowed(selected) || !env.HasIdleBuilding(parent)) out.color = kNegativeColor;
    LookedUp(env, out, "BUILD_SNAME_%s", parent);
}

void BuildHours(const Env& env, Output& out)
{
    struct Hours {
        const char* key;
        const char* text;
    };
    static constexpr Hours kHours[] = {{"ALLHOURS", "BI_HOURS_ALLDAY"}, {"MORNING", "BI_HOURS_MORNING"}, {"DAY", "BI_HOURS_DAY"},
            {"EVENING", "BI_HOURS_EVENING"}, {"NIGHT", "BI_HOURS_NIGHT"}};
    const char* hours = env.BuildingHoursKey(env.SelectedBuilding());
    for (const Hours& entry : kHours) {
        if (ascii::EqualsNoCase(hours, entry.key)) {
            CopyText(out, env.Text(entry.text));
            return;
        }
    }
    out.undefined = true;
}

void BuildCost(const Env& env, Output& out)
{
    const int cost = env.BuildingCost(env.SelectedBuilding());
    env.Format()(out.text, u"%d", cost);
    ColorIfUnaffordable(env.Cash(), cost, out);
}

// |power| of the selected building as `(x ^ (x >> 31)) - (x >> 31)`, so INT_MIN stays negative.
void BuildPower(const Env& env, Output& out)
{
    const int power = env.BuildingPower(env.SelectedBuilding());
    const uint32_t sign = static_cast<uint32_t>(power >> 31);
    env.Format()(out.text, u"%d", static_cast<int>((static_cast<uint32_t>(power) ^ sign) - sign));
}

void BuildName(const Env& env, Output& out)
{
    char textKey[kKeyCapacity];
    std::snprintf(textKey, sizeof(textKey), "BUILDING_NAME_%s", env.BuildingKey(env.SelectedBuilding()));
    CopyText(out, env.Text(textKey));
}

// The scenery description key is upper-cased (`_strupr`, C locale: ASCII only).
void SceneryDescription(const Env& env, Output& out)
{
    char textKey[kKeyCapacity];
    std::snprintf(textKey, sizeof(textKey), "SP_DESC_%s", env.SceneryKey(env.SelectedScenery()));
    for (char* c = textKey; *c != '\0'; ++c)
        if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 'a' + 'A');
    env.Format()(out.text, u"%s", env.Text(textKey));
}

void SceneryCost(const Env& env, Output& out)
{
    const int cost = env.SceneryCost(env.SelectedScenery());
    env.Format()(out.text, u"%d", cost);
    ColorIfUnaffordable(env.Cash(), cost, out);
}

double AverageAppeal(const Env& env, int building, bool female)
{
    const int count = env.BuildingCommodityCount(building);
    if (count == 0) return -1.0;
    double sum = 0.0;
    for (int slot = 0; slot < count; ++slot) sum += static_cast<double>(env.BuildingAppeal(building, slot, female));
    return sum / count;
}

// The "appeal" caption appears only when the building attracts that sex at all, otherwise the cell stays empty.
template<bool kFemale> void BuildMenuAppeal(const Env& env, Output& out)
{
    if (AverageAppeal(env, env.SelectedBuilding(), kFemale) > 0.0)
        env.Format()(out.text, u"%s", env.Text("BUILDMENU_APPEAL"));
    else
        out.text[0] = u'\0';
}

int WrapAdd(int a, int b)
{
    return static_cast<int>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
int WrapSub(int a, int b)
{
    return static_cast<int>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

// A heading in the second text renderer's font (`OBJ_SCENARIO_*`) takes the left part of the rectangle; the text of
// `textKey` is the whole format string of the swprintf.
void HeadedText(const Env& env, Output& out, const char* headingKey, const char* textKey, int extraOffset)
{
    out.xOffset = env.MeasureWidth(env.Text(headingKey)) + extraOffset;
    env.Format()(out.text, env.Text(textKey));
}

void ScenarioDescription(const Env& env, Output& out)
{
    HeadedText(env, out, "OBJ_SCENARIO_DESCRIPTION", env.ScenarioDescriptionKey(), 0);
}

void ScenarioObjectives(const Env& env, Output& out)
{
    HeadedText(env, out, "OBJ_SCENARIO_OBJECTIVES", env.ScenarioObjectivesKey(), 0);
}

// msvcrt `atoi`: leading white space, one sign, digits; the accumulator wraps instead of saturating.
uint32_t ParseInt(const char* text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = false;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + static_cast<uint32_t>(*text++ - '0');
    return negative ? 0u - value : value;
}

// `SCENARIO_OBJ_<n>`: objective n of the scenario, shifted right by the progress heading; beyond the count the cell is empty.
void ScenarioObjective(const Env& env, Output& out, const char* tail)
{
    const uint32_t index = ParseInt(tail);
    if (index < env.ObjectiveCount())
        HeadedText(env, out, "OBJ_SCENARIO_PROGRESS", env.ObjectiveKey(index), 0x23);
    else
        env.Format()(out.text, u"");
}

// `MAINSS_SANDBOX%d_<what>` / `MAINSS_SCENARIO%d_<what>` of the scenario (1-based) the main menu has picked.
void MenuScenarioText(const Env& env, Output& out, const char* scenarioFormat, const char* sandboxFormat)
{
    char textKey[kKeyCapacity];
    std::snprintf(textKey, sizeof(textKey), env.SandboxMode() ? sandboxFormat : scenarioFormat, env.CurrentScenario() + 1);
    env.Format()(out.text, env.Text(textKey));
}

// The title of the running scenario: the PIS key in a scenario, the sandbox menu caption in a sandbox.
void ScenarioTitle(const Env& env, Output& out)
{
    if (env.SandboxMode())
        MenuScenarioText(env, out, "MAINSS_SCENARIO%d_TITLE", "MAINSS_SANDBOX%d_TITLE");
    else
        env.Format()(out.text, env.Text(env.ScenarioTitleKey()));
}

void MenuScenarioDescription(const Env& env, Output& out)
{
    MenuScenarioText(env, out, "MAINSS_SCENARIO%d_DESC", "MAINSS_SANDBOX%d_DESC");
}

void MenuScenarioTitle(const Env& env, Output& out)
{
    MenuScenarioText(env, out, "MAINSS_SCENARIO%d_TITLE", "MAINSS_SANDBOX%d_TITLE");
}

void MenuScenarioStatus(const Env& env, Output& out)
{
    if (env.SandboxMode()) {
        env.Format()(out.text, u"");
        return;
    }
    const int scenario = env.CurrentScenario();
    const char* key = "MAINSS_SCENARIO_NOTCOMPLETED";
    if (env.ScenarioCompleted(scenario))
        key = "MAINSS_SCENARIO_COMPLETE";
    else if (env.ScenarioAlwaysAvailable(scenario))
        key = "MAINSS_SCENARIO_ALWAYS";
    env.Format()(out.text, env.Text(key));
}

void ObjectivesDone(const Env& env, Output& out)
{
    int done = 0;
    for (uint32_t i = 0, count = env.ObjectiveCount(); i < count; ++i)
        if (env.ObjectiveStatus(i) == 1) ++done;
    env.Format()(out.text, u"%d", done);
}

template<WorldCounter counter> void CounterText(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.WorldCounterValue(counter));
}

void UnhappyLeavers(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d",
            WrapAdd(env.GuestFlow(GuestFlowField::TotalUnhappyDepart, 1), env.GuestFlow(GuestFlowField::TotalUnhappyDepart, 0)));
}

// Buildings finished (and the total the scenario started without) in the game's own format text.
void BuildingTreeCompleted(const Env& env, Output& out)
{
    const int unbuilt = env.WorldCounterValue(WorldCounter::UnbuiltAfterStartup);
    const int left = env.WorldCounterValue(WorldCounter::BuildingsLeftToBuild);
    env.Format()(out.text, env.Text("PM_BUILD_TREE_FORMAT"), WrapSub(unbuilt, left), unbuilt);
}

void UniqueBuildings(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("PM_5_UNIQUE_FORMAT"), env.WorldCounterValue(WorldCounter::DiffBuildingsConstructed));
}

void MechanicCount(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.ObjectCount(kMechanicClass));
}

void TotalFines(const Env& env, Output& out)
{
    env.Format()(out.text, u"$%d", Truncate(env.Money(MoneyField::TotalFines)));
}

void NumApartments(const Env& env, Output& out)
{
    env.Format()(out.text, u"%d", env.GuestCapacityOfLodgings());
}

// The six staff categories of the browser: object class, the building that houses them and the
// texts naming one worker / several.
struct StaffKind {
    int objectClass;
    const char* home;
    const char* oneText;
    const char* manyText;
};
constexpr StaffKind kStaffKinds[] = {
        {0xb, "CLEANERSHACK", "STAFFB_TITLE_CLEANER", "STAFFB_FILTER_CLEANERS"},
        {0xa, "BUILDERYARD", "STAFFB_TITLE_BUILDER", "STAFFB_FILTER_BUILDERS"},
        {8, "HANDYMAN", "STAFFB_TITLE_MECHANIC", "STAFFB_FILTER_MECHANICS"},
        {9, "REP", "STAFFB_TITLE_REP", "STAFFB_FILTER_REP"},
        {7, "BAYWATCH", "STAFFB_TITLE_LIFEGUARD", "STAFFB_FILTER_LIFEGUARD"},
        {0xc, "SECURITY_POST", "STAFFB_TITLE_SECURITY", "STAFFB_FILTER_SECURITY"},
};
constexpr int kAllStaffCategory = 6;

void StaffAmounts(const Env& env, Output& out)
{
    const int category = env.StaffCategory();
    if (category == kAllStaffCategory) {
        env.Format()(out.text, env.Text("STAFFB_STAFF_ALL"), env.StaffCountAll());
        return;
    }
    if (static_cast<uint32_t>(category) >= std::size(kStaffKinds)) {
        out.undefined = true;
        return;
    }
    const StaffKind& kind = kStaffKinds[category];
    const int hired = env.StaffOnPayroll(kind.objectClass);
    const int homes = env.IdleBuildingCount(kind.home);
    const int perHome = env.BuildingMaxStaff(env.BuildingIdByKey(kind.home));
    const int maximum = static_cast<int>(static_cast<uint32_t>(perHome) * static_cast<uint32_t>(homes)); // wraps in 32 bits
    const Char16* role = env.Text(hired == 1 ? kind.oneText : kind.manyText);
    env.Format()(out.text, env.Text("STAFFB_HIRED_MAXIMUM"), hired, role, maximum);
}

void NextFerryTool(const Env& env, Output& out)
{
    uint32_t day = 0;
    const double next = env.NextBoatTime(1, &day);
    if (next > 0.0) {
        Clock(env, out.text, static_cast<float>(next));
    } else {
        out.text[0] = u'-';
        out.text[1] = u'\0';
    }
}

// Today's next boat as a clock time, else the next one on a later day with its day number, else nothing.
void NextFerry(const Env& env, Output& out)
{
    uint32_t day = 0;
    double next = env.NextBoatTime(1, &day);
    if (next > 0.0) {
        Clock(env, out.text, static_cast<float>(next));
        return;
    }
    next = env.NextBoatTime(0, &day);
    if (next > 0.0) {
        const float time = static_cast<float>(next);
        const int minutes = env.QuarterMinutes(time);
        const int hour = env.WholeHour(time);
        env.Format()(out.text, env.Text("FP_ARRIVE_FORMAT"), hour, minutes, static_cast<int>(day + 1));
        return;
    }
    out.text[0] = u'\0';
}

void FerrySchedule(const Env& env, Output& out)
{
    out.text[0] = u'\0';
    size_t length = 0;
    const auto append = [&](const Char16* part) {
        for (; *part != u'\0' && length + 1 < static_cast<size_t>(kTextCapacity); ++part) out.text[length++] = *part;
        out.text[length] = u'\0';
    };
    for (int index = 0;; ++index) {
        const double boat = env.BoatTimeToday(index);
        if (!(boat > -1.0)) break;
        if (index > 0) append(u", ");
        Char16 clock[48];
        Clock(env, clock, static_cast<float>(boat));
        append(clock);
    }
}

// Captions of the ferry panel: the text is itself the format string, ferry wording when a WHARF exists, bus wording otherwise.
template<const char* const* Texts> void FerryCaption(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text(env.ObjectExists("WHARF") ? Texts[0] : Texts[1]));
}
constexpr const char* kSummaryTexts[] = {"FP_SUMMARY_FERRY", "FP_SUMMARY_BUS"};
constexpr const char* kNextTexts[] = {"FP_NEXT_FERRY", "FP_NEXT_BUS"};
constexpr const char* kScheduleTexts[] = {"FP_SCHEDULE_FERRY", "FP_SCHEDULE_BUS"};

template<IslandText Which> void IslandCaption(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text(env.IslandKey(Which)));
}

void ScriptText(const Env& env, Output& out)
{
    env.Format()(out.text, u"%s", env.Text(env.ScriptTextKey()));
}

void MessageText(const Env& env, Output& out)
{
    env.Format()(out.text, u"%s", env.Text(env.MessageTextKey()));
}

// `swprintf(MAINMENU_VERSION, version)`: the game text is the format, the version a double (1.1f widened).
void MainVersion(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("MAINMENU_VERSION"), env.GameVersion());
}

// Object browser captions: the heading of the active column filter (empty without one) and the two row counts.
void FilterHeading(const Env& env, Output& out)
{
    const char* key = env.FilterHeadingKey();
    if (key)
        env.Format()(out.text, u"%s", env.Text(key));
    else
        out.text[0] = u'\0';
}

void BrowserGuestCount(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("OB_NUM_GUESTS"), env.BrowserCount());
}

void BrowserBuildingCount(const Env& env, Output& out)
{
    env.Format()(out.text, env.Text("OB_NUM_BUILDINGS"), env.BrowserCount());
}

struct Entry {
    const char* key;
    Handler handler;
};

constexpr Entry kEntries[] = {
        {"PlayerMoney", &PlayerMoney},
        {"PlayerMoney$", &PlayerMoneyDollar},
        {"PlayerMoneyTitle", &PlayerMoneyTitle},
        {"MaxDistance", &MaxDistance},
        {"MinDistance", &MinDistance},
        {"NumGuests", &NumGuests},
        {"NumGuestsTitle", &NumGuestsTitle},
        {"Time", &Time},
        {"GameDay", &GameDay},
        {"GameDayTitle", &GameDayTitle},
        {"NextFerryTool", &NextFerryTool},
        {"NextFerry", &NextFerry},
        {"FerrySchedule", &FerrySchedule},
        {"FP_TITLE", &FerryCaption<kSummaryTexts>},
        {"FP_NEXT", &FerryCaption<kNextTexts>},
        {"FP_SCHEDULE", &FerryCaption<kScheduleTexts>},
        {"ISLAND_TITLE", &IslandCaption<IslandText::Title>},
        {"ISLAND_AREA", &IslandCaption<IslandText::Area>},
        {"ISLAND_WEATHER", &IslandCaption<IslandText::Weather>},
        {"ISLAND_TEMP", &IslandCaption<IslandText::Temp>},
        {"ISLAND_HAZARDS", &IslandCaption<IslandText::Hazards>},
        {"ScriptText", &ScriptText},
        {"MessageText", &MessageText},
        {"MAIN_VERSION", &MainVersion},
        {"OB_FILTER_HEADING", &FilterHeading},
        {"OB_GUEST_NUM", &BrowserGuestCount},
        {"OB_BUILDING_NUM", &BrowserBuildingCount},
        {"WORLD_POWER_OUTPUT", &PowerOutput},
        {"WORLD_POWER_DEMAND", &PowerDemand},
        {"WORLD_POWER_USE", &PowerUse},
        {"WEATHER_RED", &WeatherRed},
        {"WEATHER_GREEN", &WeatherGreen},
        {"WEATHER_BLUE", &WeatherBlue},
        {"LITTER_COUNT", &LitterCount},
        {"LITTER_COUNT_NOFORMAT", &LitterCountNoFormat},
        {"BUILDING_COUNT", &BuildingCount},
        {"STAFF_COUNT", &StaffCount},
        {"GUEST_BREAKDOWN", &GuestBreakdown},
        {"SCENERY_COUNT", &SceneryCount},
        {"ARRIVALS_TODAY", &ArrivalsToday},
        {"ARRIVALS_TOTAL", &ArrivalsTotal},
        {"DEPART_TODAY", &DepartToday},
        {"DEPART_TOTAL", &DepartTotal},
        {"UN_DEPART_TODAY", &UnDepartToday},
        {"UN_DEPART_TOTAL", &UnDepartTotal},
        {"WORLD_INCOME_$", &MoneyText<MoneyField::Income, true>},
        {"WORLD_INCOME_DEMOLISH_$", &MoneyText<MoneyField::DemolishIncome, false>},
        {"WORLD_INCOME_FINE_$", &MoneyText<MoneyField::FineIncome, false>},
        {"WORLD_INCOME_ACC_$", &MoneyText<MoneyField::AccomIncome, true>},
        {"WORLD_INCOME_ATTRACT_$", &MoneyText<MoneyField::AttractIncome, true>},
        {"WORLD_INCOME_RETAIL_$", &MoneyText<MoneyField::RetailIncome, true>},
        {"WORLD_COST_$", &MoneyText<MoneyField::Costs, false>},
        {"WORLD_COST_CON_$", &MoneyText<MoneyField::ConstructCost, false>},
        {"WORLD_COST_WAGES_$", &MoneyText<MoneyField::WagesCost, false>},
        {"WORLD_COST_STOCK_$", &MoneyText<MoneyField::StockCost, false>},
        {"WORLD_COST_SCEN_$", &MoneyText<MoneyField::SceneryCost, false>},
        {"WORLD_PROFIT_$", &ProfitToday},
        {"WORLD_PROFIT_AVG_$", &MoneyText<MoneyField::AverageProfit, true>},
        {"WORLD_COST_AVG_$", &MoneyText<MoneyField::AverageCost, false>},
        {"WORLD_INCOME_AVG_$", &MoneyText<MoneyField::AverageIncome, true>},
        {"BUILD_BUILDING_DESC", &BuildDescription},
        {"BUILD_BUILDING_TREE", &BuildTree},
        {"BUILD_BUILDING_HOURS", &BuildHours},
        {"BUILD_COST_AMOUNT", &BuildCost},
        {"BUILD_POWER_AMOUNT", &BuildPower},
        {"BUILD_NAME_TEXT", &BuildName},
        {"SP_SCENERY_DESC", &SceneryDescription},
        {"SP_SCENERY_COST", &SceneryCost},
        {"BUILDMENU_MALE", &BuildMenuAppeal<false>},
        {"BUILDMENU_FEMALE", &BuildMenuAppeal<true>},
        {"SCENARIO_DESCRIPTION", &ScenarioDescription},
        {"SCENARIO_OBJECTIVES", &ScenarioObjectives},
        {"SCENARIO_TITLE", &ScenarioTitle},
        {"SS_CUR_SCENARIO_DESC", &MenuScenarioDescription},
        {"SS_CUR_SCENARIO_TITLE", &MenuScenarioTitle},
        {"SS_CUR_SCENARIO_STATUS", &MenuScenarioStatus},
        {"OBJECTIVES_DONE", &ObjectivesDone},
        {"BUILDING_CONSTRUCTED", &CounterText<WorldCounter::BuildingsConstructed>},
        {"BUILDING_DIFF_CONSTRUCTED", &CounterText<WorldCounter::DiffBuildingsConstructed>},
        {"UNHAPPY_LEAVERS", &UnhappyLeavers},
        {"GUEST_PARTYCOUNT", &CounterText<WorldCounter::PartyCount>},
        {"BUILDING_TREE_COMPELETED", &BuildingTreeCompleted},
        {"GUESTS_DROWNED", &CounterText<WorldCounter::GuestsDrowned>},
        {"MECHANIC_COUNT", &MechanicCount},
        {"FINE_INCOME_$", &TotalFines},
        {"GUESTS_EATEN", &CounterText<WorldCounter::GuestsEaten>},
        {"PM_5_UNIQUE", &UniqueBuildings},
        {"SHAG_COUNT", &CounterText<WorldCounter::ShagCount>},
        {"PISSED_COUNT", &CounterText<WorldCounter::PissedCount>},
        {"BAR_COUNT", &CounterText<WorldCounter::BarsConstructed>},
        {"STAFF_AMOUNTS", &StaffAmounts},
        {"NUM_APARTMENTS", &NumApartments},
};
constexpr int kExactCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

using PrefixHandler = void (*)(const Env&, Output&, const char* tail);
struct PrefixEntry {
    const char* prefix;
    PrefixHandler handler;
};
constexpr PrefixEntry kPrefixEntries[] = {
        {"SCENARIO_OBJ_", &ScenarioObjective},
};

// Concrete keys of those families for tests that walk the handled keys: in range, past the count, negative, no digits.
constexpr const char* kPrefixExamples[] = {"SCENARIO_OBJ_0", "SCENARIO_OBJ_1", "SCENARIO_OBJ_2", "SCENARIO_OBJ_3", "SCENARIO_OBJ_4",
        "SCENARIO_OBJ_99", "SCENARIO_OBJ_-1", "SCENARIO_OBJ_"};

} // namespace

bool Evaluate(const char* key, const Env& env, uint32_t widgetColor, Output& out)
{
    out.color = widgetColor;
    out.xOffset = 0;
    out.undefined = false;
    out.fromProvider = false;
    for (const Entry& entry : kEntries) {
        if (std::strcmp(key, entry.key) == 0) {
            entry.handler(env, out);
            if (out.undefined) out.text[0] = u'\0';
            return true;
        }
    }
    for (const PrefixEntry& entry : kPrefixEntries) {
        const size_t length = std::strlen(entry.prefix);
        if (std::strncmp(key, entry.prefix, length) == 0) {
            entry.handler(env, out, key + length);
            return true;
        }
    }

    out.fromProvider = true;
    return env.ProviderText(key, out.text);
}

int HandledKeyCount()
{
    return kExactCount + static_cast<int>(sizeof(kPrefixExamples) / sizeof(kPrefixExamples[0]));
}

const char* HandledKey(int index)
{
    return index < kExactCount ? kEntries[index].key : kPrefixExamples[index - kExactCount];
}

} // namespace obl::gui_variable
