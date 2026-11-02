#include "bot.h"

#include "bot_build_tree.h"
#include "game/game/command_recorder.h"

#include <array>
#include <charconv>
#include <system_error>

namespace obl::bot {
namespace {

constexpr std::string_view kMagic = "OBL-bot";
constexpr int32_t kBotFormatVersion = 1;

// Only sets the fastest game speed, then waits: the baseline that shows a scenario is not won by waiting (and the run that checks the loss
// detection).
class WaitBot final : public Bot {
public:
    void Think(const WorldView& view, const Queries&, std::vector<command::Command>& out) override
    {
        constexpr int32_t kFastestSpeed = 2;
        if (view.speedLevel != kFastestSpeed && view.tick == 0) out.push_back(command::SetGameSpeed{kFastestSpeed});
    }
};

std::unique_ptr<Bot> MakeWaitBot(const BotPlan&)
{
    return std::make_unique<WaitBot>();
}

constexpr std::array kBots = {
        BotInfo{"wait", "", "sets the fastest speed and waits (any start)", &MakeWaitBot},
        BotInfo{"build-tree-s1", "s1", "scenario 1: one finished building of every type before day 2, 17:00", &MakeBuildTreeBot},
};

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::pair<std::string_view, std::string_view> SplitWord(std::string_view line)
{
    const size_t space = line.find_first_of(" \t");
    if (space == std::string_view::npos) return {line, {}};
    return {line.substr(0, space), Trim(line.substr(space + 1))};
}

template<class T> bool ParseNumber(std::string_view text, T& out)
{
    if (text.empty()) return false;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

} // namespace

std::string_view OutcomeName(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Won: return "won";
    case Outcome::Lost: return "lost";
    case Outcome::Running: break;
    }
    return "running";
}

std::optional<Outcome> ParseOutcome(std::string_view name)
{
    for (const Outcome outcome : {Outcome::Running, Outcome::Won, Outcome::Lost})
        if (name == OutcomeName(outcome)) return outcome;
    return std::nullopt;
}

Outcome OutcomeOfDialog(std::string_view dialog)
{
    if (dialog == "ScenarioWon" || dialog == "GameWon") return Outcome::Won;
    if (dialog == "ScenarioLost") return Outcome::Lost;
    return Outcome::Running;
}

const BuildingType* WorldView::FindType(std::string_view key) const
{
    for (const BuildingType& type : buildingTypes)
        if (type.key == key) return &type;
    return nullptr;
}

std::optional<BotPlan> BotPlan::Parse(std::string_view text, std::string* error)
{
    BotPlan plan;
    int lineNumber = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find('\n', pos);
        const size_t stop = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(pos, stop - pos);
        pos = stop + 1;
        ++lineNumber;
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;
        const size_t colon = line.find(':');
        const std::string_view name = colon == std::string_view::npos ? std::string_view() : Trim(line.substr(0, colon));
        if (name.empty() || name.find_first_of(" \t") != std::string_view::npos) {
            if (error) *error = "line " + std::to_string(lineNumber) + ": expected '<name>: <words>'";
            return std::nullopt;
        }
        std::vector<std::string> words;
        std::string_view rest = Trim(line.substr(colon + 1));
        while (!rest.empty()) {
            const auto [word, tail] = SplitWord(rest);
            words.emplace_back(word);
            rest = tail;
        }
        plan.m_lists.emplace_back(std::string(name), std::move(words));
    }
    return plan;
}

const std::vector<std::string>& BotPlan::Words(std::string_view name) const
{
    static const std::vector<std::string> kNone;
    for (const auto& [key, words] : m_lists)
        if (key == name) return words;
    return kNone;
}

std::vector<int32_t> BotPlan::Ints(std::string_view name) const
{
    std::vector<int32_t> values;
    for (const std::string& word : Words(name)) {
        int32_t value = 0;
        ParseNumber(word, value);
        values.push_back(value);
    }
    return values;
}

std::vector<std::vector<std::string>> BotPlan::Rows(std::string_view name) const
{
    std::vector<std::vector<std::string>> rows;
    for (const std::string& word : Words(name)) {
        std::vector<std::string>& row = rows.emplace_back();
        size_t from = 0;
        for (;;) {
            const size_t colon = word.find(':', from);
            row.push_back(word.substr(from, colon == std::string::npos ? colon : colon - from));
            if (colon == std::string::npos) break;
            from = colon + 1;
        }
    }
    return rows;
}

std::vector<BotPlan::Want> BotPlan::Wants(std::string_view name) const
{
    std::vector<Want> wants;
    for (const auto& row : Rows(name)) {
        Want want;
        want.key = row[0] == "-" ? std::string() : row[0];
        if (row.size() > 1) ParseNumber(row[1], want.count);
        want.save = row.size() > 2 && row[2] == "save";
        wants.push_back(std::move(want));
    }
    return wants;
}

std::span<const BotInfo> Bots()
{
    return kBots;
}

const BotInfo* FindBot(std::string_view id)
{
    for (const BotInfo& info : kBots)
        if (info.id == id) return &info;
    return nullptr;
}

std::optional<BotRun> ReadBotRun(std::string_view text, std::string* error)
{
    int lineNumber = 0;
    auto fail = [&](const std::string& why) -> std::optional<BotRun> {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return std::nullopt;
    };
    BotRun run;
    bool magic = false, haveBot = false, haveStart = false, haveSeed = false, haveDt = false, haveTicks = false, haveExpect = false;
    // Each key once, in any order after the magic line.
    auto once = [&](bool& seen, std::string_view key) {
        if (seen) return std::optional<std::string>("duplicate '" + std::string(key) + "'");
        seen = true;
        return std::optional<std::string>();
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find('\n', pos);
        const size_t stop = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = Trim(text.substr(pos, stop - pos));
        pos = stop + 1;
        ++lineNumber;
        if (line.empty() || line.front() == '#') continue;
        const auto [word, rest] = SplitWord(line);
        if (!magic) {
            int32_t version = 0;
            if (word != kMagic || !ParseNumber(rest, version)) return fail("expected '" + std::string(kMagic) + " <version>'");
            if (version != kBotFormatVersion) return fail("unsupported bot format version " + std::string(rest));
            magic = true;
            continue;
        }
        std::optional<std::string> duplicate;
        if (word == "bot") {
            duplicate = once(haveBot, word);
            if (!duplicate && !FindBot(rest)) return fail("no bot named '" + std::string(rest) + "'");
            run.bot = rest;
        } else if (word == "start") {
            duplicate = once(haveStart, word);
            const auto [kind, name] = SplitWord(rest);
            if (!duplicate && (kind != "scenario" || !command::ParseStartScenario(name)))
                return fail("expected 'start scenario s<n>|sandbox<n>'");
            run.start = name;
        } else if (word == "seed") {
            duplicate = once(haveSeed, word);
            if (!duplicate && !ParseNumber(rest, run.seed)) return fail("bad seed '" + std::string(rest) + "'");
        } else if (word == "fixed-dt") {
            duplicate = once(haveDt, word);
            const auto dt = command::ParseFloat(rest);
            if (!duplicate && (!dt || !(*dt > 0.0f))) return fail("fixed-dt needs a positive number");
            if (dt) run.fixedDt = *dt;
        } else if (word == "max-ticks") {
            duplicate = once(haveTicks, word);
            if (!duplicate && (!ParseNumber(rest, run.maxTicks) || run.maxTicks == 0)) return fail("max-ticks needs a positive count");
        } else if (word == "expect") {
            duplicate = once(haveExpect, word);
            const auto outcome = ParseOutcome(rest);
            if (!duplicate && (!outcome || *outcome == Outcome::Running)) return fail("expect needs won or lost");
            if (outcome) run.expect = *outcome;
        } else {
            return fail("unrecognized key '" + std::string(word) + "'");
        }
        if (duplicate) return fail(*duplicate);
    }
    const char* missing = !magic                   ? "empty bot run file"
            : !haveBot || !haveStart || !haveTicks ? "bot, start and max-ticks are required"
                                                   : nullptr;
    if (missing) {
        if (error) *error = missing;
        return std::nullopt;
    }
    return run;
}

} // namespace obl::bot
