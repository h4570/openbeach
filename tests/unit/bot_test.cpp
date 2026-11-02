// Scenario bots (src/game/bot/bot.h, docs/architecture/bots/index.md): the verdict of the script dialogs, the bot run file, and the scenario-1 bot's
// decisions on a hand-made view.

#include "unit_test.h"

#include "game/bot/bot.h"
#include "game/bot/bot_build_tree.h"

#include <set>
#include <string>
#include <utility>

using namespace obl::bot;
using obl::command::Command;
using obl::command::PlaceBuilding;
using obl::command::SetGameSpeed;

namespace {

// A world where every footprint fits on the free tiles of a set; the bot's searches are recorded.
class FakeQueries final : public Queries {
public:
    std::set<std::pair<int32_t, int32_t>> free;
    bool BuildingFits(std::string_view, int32_t x, int32_t y, int32_t direction) const override
    {
        return direction == 0 && free.count({x, y}) != 0;
    }
};

BuildingType Type(std::string key, int32_t cost, bool allowed, bool locked, int32_t total)
{
    BuildingType type;
    type.key = std::move(key);
    type.cost = cost;
    type.allowed = allowed;
    type.lockedByTree = locked;
    type.total = total;
    type.finished = total;
    return type;
}

WorldView View(uint32_t tick, float cash)
{
    WorldView view;
    view.tick = tick;
    view.cash = cash;
    view.mapWidth = 64;
    view.mapHeight = 64;
    view.speedLevel = 2;
    Building hotel;
    hotel.key = "hotel";
    hotel.tileX = 20;
    hotel.tileY = 30;
    view.buildings.push_back(hotel);
    return view;
}

const char* const kRunText = "# comment\n"
                             "OBL-bot 1\n"
                             "bot build-tree-s1\n"
                             "start scenario s1\n"
                             "seed 7\n"
                             "fixed-dt 0.02\n"
                             "max-ticks 30000\n"
                             "expect won\n";

} // namespace

OBL_TEST_CASE("bot.outcome_of_the_campaign_dialogs")
{
    OBL_CHECK(OutcomeOfDialog("ScenarioWon") == Outcome::Won);
    OBL_CHECK(OutcomeOfDialog("GameWon") == Outcome::Won); // scenario 12
    OBL_CHECK(OutcomeOfDialog("ScenarioLost") == Outcome::Lost);
    OBL_CHECK(OutcomeOfDialog("Objectives") == Outcome::Running);
    OBL_CHECK(OutcomeOfDialog("scenariowon") == Outcome::Running); // the script names dialogs case-sensitively
    OBL_CHECK(ParseOutcome("lost") == Outcome::Lost);
    OBL_CHECK(!ParseOutcome("draw").has_value());
}

OBL_TEST_CASE("bot.registry_names_every_bot_once")
{
    std::set<std::string_view> ids;
    for (const BotInfo& info : Bots()) {
        OBL_CHECK(ids.insert(info.id).second);
        OBL_CHECK(info.make(BotPlan{}) != nullptr);
    }
    OBL_CHECK(FindBot("build-tree-s1") != nullptr);
    OBL_CHECK(FindBot("wait") != nullptr);
    OBL_CHECK(FindBot("nobody") == nullptr);
}

OBL_TEST_CASE("bot.run_file_reads_every_key")
{
    std::string error;
    const auto run = ReadBotRun(kRunText, &error);
    OBL_REQUIRE(run.has_value());
    OBL_CHECK(error.empty());
    OBL_CHECK_EQ(run->bot, std::string("build-tree-s1"));
    OBL_CHECK_EQ(run->start, std::string("s1"));
    OBL_CHECK_EQ(run->seed, 7u);
    OBL_CHECK(run->fixedDt == 0.02f);
    OBL_CHECK_EQ(run->maxTicks, 30000u);
    OBL_CHECK(run->expect == Outcome::Won);
}

OBL_TEST_CASE("bot.run_file_defaults_and_errors")
{
    std::string error;
    const auto minimal = ReadBotRun("OBL-bot 1\nbot wait\nstart scenario sandbox2\nmax-ticks 5\n", &error);
    OBL_REQUIRE(minimal.has_value());
    OBL_CHECK_EQ(minimal->seed, 1u);
    OBL_CHECK(minimal->expect == Outcome::Won);

    OBL_CHECK(!ReadBotRun("OBL-bot 2\nbot wait\nstart scenario s1\nmax-ticks 5\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot nobody\nstart scenario s1\nmax-ticks 5\n", &error).has_value());
    OBL_CHECK_EQ(error, std::string("line 2: no bot named 'nobody'"));
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nstart save x.dat\nmax-ticks 5\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nstart scenario s1\nmax-ticks 0\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nstart scenario s1\nmax-ticks 5\nexpect running\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nbot wait\nstart scenario s1\nmax-ticks 5\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nstart scenario s1\nmax-ticks 5\nspeed 2\n", &error).has_value());
    OBL_CHECK(!ReadBotRun("OBL-bot 1\nbot wait\nstart scenario s1\n", &error).has_value());
    OBL_CHECK_EQ(error, std::string("bot, start and max-ticks are required"));
}

OBL_TEST_CASE("bot.build_tree_places_one_unlocked_affordable_type_per_think")
{
    FakeQueries queries;
    queries.free = {{23, 30}, {21, 31}};
    WorldView view = View(0, 3000.0f);
    view.speedLevel = 0;
    view.buildingTypes = {Type("hotel", 9000, false, false, 1), Type("pool", 500, true, true, 0), Type("bar", 5000, true, false, 0),
            Type("loo", 2500, true, false, 0), Type("hut", 100, true, false, 0)};
    const auto bot = MakeBuildTreeBot(BotPlan{});
    std::vector<Command> out;
    bot->Think(view, queries, out);
    // The speed first; HOTEL stands, POOL waits for its predecessor, BAR is too dear: LOO, at the free tile nearest to the hotel (ring 1).
    OBL_REQUIRE(out.size() == 2u);
    OBL_CHECK(out[0] == Command(SetGameSpeed{2}));
    OBL_CHECK(out[1] == Command(PlaceBuilding{"loo", 21, 31, 0}));

    // The LOO is pending, so the next think takes HUT; the cash must stay above the price.
    out.clear();
    view.tick = 25;
    bot->Think(view, queries, out);
    OBL_REQUIRE(out.size() == 1u);
    OBL_CHECK(out[0] == Command(PlaceBuilding{"hut", 21, 31, 0}));

    out.clear();
    view.tick = 50;
    view.cash = 100.0f;
    bot->Think(view, queries, out);
    OBL_CHECK(out.empty());
}

OBL_TEST_CASE("bot.build_tree_retries_a_placement_that_did_not_appear")
{
    FakeQueries queries;
    queries.free = {{22, 30}};
    WorldView view = View(0, 3000.0f);
    view.buildingTypes = {Type("loo", 2500, true, false, 0)};
    const auto bot = MakeBuildTreeBot(BotPlan{});
    std::vector<Command> out;
    bot->Think(view, queries, out);
    OBL_REQUIRE(out.size() == 1u);
    out.clear();
    view.tick = 99;
    bot->Think(view, queries, out);
    OBL_CHECK(out.empty());
    view.tick = 100;
    bot->Think(view, queries, out);
    OBL_CHECK_EQ(out.size(), 1u);
}
