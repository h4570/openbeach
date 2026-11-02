// The chaos bot (src/game/bot/chaos.h, docs/testing/chaos-bot.md): its generator, its command stream on a hand-made view, the chaos run file
// and the rand() jump-ahead of the save round trip.

#include "unit_test.h"

#include "game/bot/chaos.h"
#include "game/bot/chaos_findings.h"

#include <memory>
#include <limits>

#include <set>
#include <string>
#include <tuple>
#include <variant>

using namespace obl::chaos;
using obl::bot::Building;
using obl::bot::BuildingType;
using obl::bot::Queries;
using obl::bot::WorldView;
using obl::command::Command;

OBL_TEST_CASE("chaos.cash_ledger_separates_refunds_and_costs_and_rebases")
{
    obl::world_data::World world{};
    obl::world_data::Player player{};
    world.m_player.reset(&player);
    player.m_cash = 10000;
    CashLedger ledger;
    OBL_CHECK(ledger.Observe(world).empty());
    world.m_incomeToday = 100;
    world.m_demolishIncomeToday = 75;
    world.m_costsToday = 200;
    world.m_constructCostToday = 150;
    world.m_sceneryCostToday = 50;
    player.m_cash -= 25;
    OBL_CHECK(ledger.Observe(world).empty());
    // Scenery's 100-cost sale books 75 but pays 20. Keep the discrepancy visible.
    world.m_demolishIncomeToday += 75;
    player.m_cash += 20;
    const auto findings = ledger.Observe(world);
    OBL_REQUIRE(findings.size() == 1u);
    OBL_CHECK(findings[0].find("cash=20.000000 income=0.000000 refunds=75.000000 costs=0.000000") != std::string::npos);
    OBL_CHECK(findings[0].find("residual=-55.000000") != std::string::npos);
    OBL_CHECK(ledger.Observe(world).empty());
    OBL_CHECK_EQ(player.m_cash, 9995.0f);
    // Day resets cannot be reconstructed from the lossy history (refunds are not saved there).
    ++world.m_gameDay;
    world.m_incomeToday = world.m_demolishIncomeToday = world.m_costsToday = 0;
    player.m_cash += 100;
    const auto reset = ledger.Observe(world);
    OBL_REQUIRE(reset.size() == 1u);
    OBL_CHECK_EQ(reset[0], "cash-ledger-reset day=1");
    OBL_CHECK(ledger.Observe(world).empty());
    player.m_cash += 0.25f;
    OBL_CHECK(ledger.Observe(world).empty());
    player.m_cash += 10;
    OBL_REQUIRE(ledger.Observe(world).size() == 1u);
    world.m_player = nullptr;
    OBL_CHECK(ledger.Observe(world).empty());
    world.m_player.reset(&player);
    OBL_CHECK(ledger.Observe(world).empty());
}

OBL_TEST_CASE("chaos.cash_ledger_reports_nonfinite_and_accepts_float_rounding")
{
    obl::world_data::World world{};
    obl::world_data::Player player{};
    world.m_player.reset(&player);
    player.m_cash = 10000000;
    CashLedger ledger;
    OBL_CHECK(ledger.Observe(world).empty());
    player.m_cash += 8;
    OBL_CHECK(ledger.Observe(world).empty());
    player.m_cash += 100;
    OBL_REQUIRE(ledger.Observe(world).size() == 1u);
    player.m_cash = std::numeric_limits<float>::infinity();
    OBL_REQUIRE(ledger.Observe(world).size() == 1u);
}

OBL_TEST_CASE("chaos.soft_findings_observe_counters_and_grid_without_mutation")
{
    auto objects = std::make_unique<obl::object::Manager>();
    obl::world_data::World world{};
    obl::map::Grid grid;
    grid.m_width = grid.m_height = 8;
    grid.m_tileSize = 64;
    grid.m_cells.resize(64);
    world.m_objectManager.reset(objects.get());
    world.m_grid.reset(&grid);
    OBL_CHECK(SoftFindings(world).empty());
    world.m_numGuests = -1;
    world.m_numBuildings = -2;
    obl::object::Building building{};
    building.m_type = obl::object::ObjectType::Building;
    building.m_id = 42;
    building.m_numMalestaff = -3;
    building.m_NumBoards = -4;
    objects->AddBuilding(&building, building.m_id);
    grid.m_cells[7].m_flags = 0x10;
    const auto findings = SoftFindings(world);
    OBL_CHECK_EQ(findings.size(), 6u);
    OBL_CHECK(std::find(findings.begin(), findings.end(), "negative-counter building[42].boards=-4") != findings.end());
    OBL_CHECK_EQ(building.m_NumBoards, -4);
    OBL_CHECK_EQ(grid.m_cells[7].m_flags, 0x10u);
    objects->m_buildings.resize(obl::object::Manager::kMaxBuildings + 1, &building);
    const auto overfull = SoftFindings(world);
    OBL_CHECK(std::find(overfull.begin(), overfull.end(), "invalid-count objects.buildings=501") != overfull.end());
}

OBL_TEST_CASE("chaos.soft_findings_report_grid_ids_the_manager_does_not_list")
{
    auto objects = std::make_unique<obl::object::Manager>();
    obl::world_data::World world{};
    obl::map::Grid grid;
    grid.m_width = grid.m_height = 1;
    grid.m_cells.resize(1);
    world.m_objectManager.reset(objects.get());
    world.m_grid.reset(&grid);
    obl::object::Base object{};
    object.m_id = 12;
    object.m_type = obl::object::ObjectType::Scenery;
    objects->AddOther(&object, 12);
    obl::grid_cell::AddObject(grid.m_cells[0], obl::cell_objects::IdOf(object));
    OBL_CHECK(SoftFindings(world).empty());
    // Scenery is no exception any more: every object on the grid is listed.
    obl::grid_cell::AddObject(grid.m_cells[0], static_cast<obl::map::ObjectId>(13));
    const auto findings = SoftFindings(world);
    OBL_REQUIRE(findings.size() == 1u);
    OBL_CHECK_EQ(findings[0], "unlisted-grid-object references=1");
}

OBL_TEST_CASE("chaos.soft_findings_detect_partial_footprints_and_rectangular_buildings")
{
    auto objects = std::make_unique<obl::object::Manager>();
    obl::world_data::World world{};
    obl::map::Grid grid;
    grid.m_width = grid.m_height = 8;
    grid.m_tileSize = 64;
    grid.m_cells.resize(64);
    world.m_objectManager.reset(objects.get());
    world.m_grid.reset(&grid);
    obl::object::Building building{};
    obl::tables::BuildingDef def{};
    obl::ui::SpriteInstance sprite{};
    auto shared = std::make_shared<obl::tables::BuildingFootprint>();
    auto& mask = *shared;
    building.m_type = obl::object::ObjectType::Building;
    building.m_definition = &def;
    building.m_sprite = &sprite;
    objects->AddBuilding(&building, building.m_id);
    obl::grid_cell::AddObject(grid.m_cells[0], obl::cell_objects::IdOf(building));
    // A 2x1 building with only its origin tile present.
    def.m_GridWidth = 2;
    def.m_GridHeight = 1;
    grid.m_cells[0].m_flags = 0x10;
    auto findings = SoftFindings(world);
    OBL_REQUIRE(findings.size() == 1u);
    OBL_CHECK_EQ(findings[0], "missing-footprint building[0].tiles=1");
    grid.m_cells[0].m_flags = 0;
    def.m_footprint = shared;
    mask.m_layers[0][0][0] = 1;
    mask.m_layers[3][0][1] = 1;
    // All eight directions, including the construction adapter's zero-step fallback for 4-7.
    for (int rotation = 0; rotation < 8; ++rotation) {
        sprite.m_direction = rotation;
        findings = SoftFindings(world);
        OBL_REQUIRE(findings.size() == 1u);
        OBL_CHECK_EQ(findings[0], "missing-footprint building[0].tiles=2");
    }
}

namespace {

class FitsNowhere final : public Queries {
public:
    bool BuildingFits(std::string_view, int32_t, int32_t, int32_t) const override { return false; }
};

// Fits at even columns, direction 1: one in eight of the bot's supported placements.
class FitsAt final : public Queries {
public:
    static bool Fits(int32_t x, int32_t direction) { return x % 2 == 0 && direction == 1; }
    bool BuildingFits(std::string_view, int32_t x, int32_t, int32_t direction) const override { return Fits(x, direction); }
};

WorldView View()
{
    WorldView view;
    view.mapWidth = 64;
    view.mapHeight = 48;
    BuildingType bar;
    bar.key = "bar";
    view.buildingTypes = {bar};
    Building building;
    building.id = 7;
    building.key = "bar";
    building.tileX = 30;
    building.tileY = 30;
    view.buildings = {building};
    view.staffIds[0] = {11, 12};
    view.sceneryKeys = {"bench", "palm"};
    view.surfaceKeys = {"path", "grass"};
    view.beerKeys = {"capehorn"};
    view.showKeys = {"band"};
    return view;
}

std::vector<std::string> Stream(uint32_t seed, int thinks)
{
    ChaosBot bot(seed);
    const WorldView view = View();
    std::vector<std::string> lines;
    for (int i = 0; i < thinks; ++i) {
        std::vector<Command> out;
        bot.Think(view, FitsNowhere(), out);
        for (const Command& command : out) lines.push_back(obl::command::Format(command));
    }
    return lines;
}

} // namespace

OBL_TEST_CASE("chaos.rng_is_splitmix64")
{
    // Reference outputs of splitmix64 from state 0: the stream must not depend on the compiler or the standard library.
    Rng rng(0);
    OBL_CHECK(rng.Next() == 0xe220a8397b1dcdafull);
    OBL_CHECK(rng.Next() == 0x6e789e6aa1b965f4ull);
    Rng bounded(5);
    for (int i = 0; i < 1000; ++i) {
        const int32_t value = bounded.Between(-3, 3);
        OBL_CHECK(value >= -3 && value <= 3);
        OBL_CHECK(bounded.Below(7) < 7u);
    }
    OBL_CHECK_EQ(bounded.Below(0), 0u);
}

OBL_TEST_CASE("chaos.same_seed_same_stream_other_seed_other_stream")
{
    const auto first = Stream(3, 200);
    OBL_CHECK(first.size() >= 200u);
    OBL_CHECK(first == Stream(3, 200));
    OBL_CHECK(first != Stream(4, 200));
}

OBL_TEST_CASE("chaos.every_kind_draws_a_command_that_round_trips_as_text")
{
    ChaosBot bot(9);
    const WorldView view = View();
    for (int round = 0; round < 50; ++round) {
        for (int kind = 0; kind < static_cast<int>(Kind::Count); ++kind) {
            const Command command = bot.Draw(static_cast<Kind>(kind), view, FitsNowhere());
            const std::string text = obl::command::Format(command);
            std::string error;
            const auto parsed = obl::command::Parse(text, &error);
            OBL_REQUIRE(parsed.has_value());
            OBL_CHECK(*parsed == command);
        }
    }
}

OBL_TEST_CASE("chaos.stream_mixes_valid_and_bad_targets")
{
    ChaosBot bot(1);
    const WorldView view = View();
    std::set<std::string> names;
    std::set<uint32_t> demolished;
    std::set<int32_t> directions;
    int placed = 0, fitted = 0;
    for (int i = 0; i < 3000; ++i) {
        std::vector<Command> out;
        bot.Think(view, FitsAt(), out);
        for (const Command& command : out) {
            names.insert(std::string(obl::command::Name(command)));
            if (const auto* demolish = std::get_if<obl::command::Demolish>(&command)) demolished.insert(demolish->object);
            if (const auto* place = std::get_if<obl::command::PlaceBuilding>(&command)) {
                directions.insert(place->direction);
                ++placed;
                fitted += FitsAt::Fits(place->x, place->direction);
            }
        }
    }
    // Every command of the set except `resume-game` (only when paused).
    OBL_CHECK_EQ(names.size(), 35u);
    OBL_CHECK(demolished.count(7) == 1); // the live building
    OBL_CHECK(demolished.count(0xffffffffu) == 1); // the game's "no object"
    OBL_CHECK(directions.count(2) == 1);
    OBL_CHECK(directions.count(5) == 1); // the refused rotations
    // About a tenth fit by chance; the placements that search with the fit query (40 %) nearly always find one.
    OBL_CHECK(fitted * 10 > placed * 3);
}

OBL_TEST_CASE("chaos.paused_world_is_resumed_soon")
{
    ChaosBot bot(2);
    WorldView view = View();
    view.paused = true;
    int resumed = 0;
    for (int i = 0; i < 100; ++i) {
        std::vector<Command> out;
        bot.Think(view, FitsNowhere(), out);
        OBL_REQUIRE(!out.empty());
        resumed += std::holds_alternative<obl::command::ResumeGame>(out.front()) || out.front() == Command(obl::command::SetPaused{false});
    }
    OBL_CHECK(resumed > 40);
}

OBL_TEST_CASE("chaos.run_file")
{
    std::string error;
    const auto run = ReadChaosRun("# a run\nOBL-chaos 1\nstart scenario sandbox2\nseed 7\nmax-ticks 3000\nsave-at 1500\n"
                                  "checkpoint-every 250\ncheck-rejected 0\ncommands shrunk.replay\n",
            &error);
    OBL_REQUIRE(run.has_value());
    OBL_CHECK_EQ(run->start, std::string("sandbox2"));
    OBL_CHECK_EQ(run->seed, 7u);
    OBL_CHECK(run->fixedDt == 0.02f);
    OBL_CHECK_EQ(run->maxTicks, 3000u);
    OBL_CHECK(run->saveAt == std::optional<uint32_t>(1500));
    OBL_CHECK_EQ(run->checkpointEvery, 250u);
    OBL_CHECK_EQ(run->checkRejected, 0u);
    OBL_CHECK_EQ(run->commands, std::string("shrunk.replay"));

    const auto minimal = ReadChaosRun("OBL-chaos 1\nstart scenario s1\nmax-ticks 10\n", &error);
    OBL_REQUIRE(minimal.has_value());
    OBL_CHECK(!minimal->saveAt.has_value());
    OBL_CHECK(minimal->commands.empty());
    OBL_CHECK_EQ(minimal->checkRejected, 2u);

    OBL_CHECK(!ReadChaosRun("OBL-chaos 2\nstart scenario s1\nmax-ticks 10\n", &error).has_value());
    OBL_CHECK(!ReadChaosRun("OBL-chaos 1\nstart scenario s1\nmax-ticks 10\nsave-at 10\n", &error).has_value());
    OBL_CHECK_EQ(error, std::string("save-at must be below max-ticks"));
    OBL_CHECK(!ReadChaosRun("OBL-chaos 1\nstart scenario s1\nseed 1\nseed 2\nmax-ticks 10\n", &error).has_value());
    OBL_CHECK_EQ(error, std::string("line 4: duplicate 'seed'"));
    OBL_CHECK(!ReadChaosRun("OBL-chaos 1\nstart save x.dat\nmax-ticks 10\n", &error).has_value());
    OBL_CHECK(!ReadChaosRun("OBL-chaos 1\nstart scenario s1\nmax-ticks 10\nbot wait\n", &error).has_value());
    OBL_CHECK(!ReadChaosRun("OBL-chaos 1\nstart scenario s1\n", &error).has_value());
    OBL_CHECK_EQ(error, std::string("start and max-ticks are required"));
}

OBL_TEST_CASE("chaos.rand_state_jumps_like_the_lcg")
{
    for (const uint32_t seed : {0u, 1u, 12345u}) {
        uint32_t state = seed;
        for (uint64_t calls = 0; calls < 300; ++calls) {
            OBL_CHECK_EQ(RandStateAfter(seed, calls), state);
            state = state * 214013u + 2531011u;
        }
    }
    // A long jump agrees with stepping.
    uint32_t state = 1;
    for (int i = 0; i < 1000000; ++i) state = state * 214013u + 2531011u;
    OBL_CHECK_EQ(RandStateAfter(1, 1000000), state);
}

OBL_TEST_CASE("chaos.tricky_sale_sequence_and_report_only_observations")
{
    class FountainFits final : public Queries {
    public:
        bool BuildingFits(std::string_view, int32_t, int32_t, int32_t) const override { return false; }
        bool SceneryFits(std::string_view key, int32_t x, int32_t y, int32_t dir) const override
        {
            return key == "fountain04" && x == 2 && y == 1 && dir == 0;
        }
    } queries;
    WorldView view;
    view.mapWidth = 4;
    view.mapHeight = 3;
    view.cash = 1000;
    view.sceneryKeys = {"fountain04"};
    TrickyBot bot;
    std::vector<Command> commands;
    bot.Think(view, queries, commands);
    OBL_REQUIRE(commands.size() == 1u);
    OBL_CHECK_EQ(obl::command::Format(commands[0]), "place-scenery key=fountain04 x=2 y=1 dir=0");
    commands.clear();
    bot.Think(view, queries, commands);
    OBL_REQUIRE(commands.size() == 3u);
    for (const auto& c : commands) OBL_CHECK_EQ(obl::command::Format(c), "dozer x=2 y=1");
    view.cash = 880;
    view.incomeToday = 75;
    for (int phase = 0; phase < 3; ++phase) {
        commands.clear();
        const WorldView before = view;
        bot.Think(view, queries, commands);
        OBL_CHECK_EQ(view.cash, before.cash);
        OBL_CHECK_EQ(view.incomeToday, before.incomeToday);
    }
    OBL_REQUIRE(bot.observations.size() == 3u);
    OBL_CHECK(bot.observations[0].find("income-delta=75") != std::string::npos);
    commands.clear();
    bot.Think(view, queries, commands);
    OBL_CHECK(commands.empty());
    TrickyBot skipped;
    skipped.Think(view, FitsNowhere(), commands);
    OBL_CHECK(commands.empty());
    OBL_REQUIRE(skipped.observations.size() == 1u);
    OBL_CHECK(skipped.observations[0].find("skipped=") != std::string::npos);
}

OBL_TEST_CASE("chaos.tricky_run_file")
{
    const std::string nl(1, static_cast<char>(10));
    const std::string base = "OBL-chaos 1\nstart scenario s8\nmax-ticks 100\n";
    const auto run = ReadChaosRun(base + "mode tricky\n");
    OBL_REQUIRE(run.has_value());
    OBL_CHECK_EQ(run->tricky, "scenery-sale");
    const auto accommodation = ReadChaosRun(base + "mode tricky accommodation-away" + nl);
    const auto construction = ReadChaosRun(base + "mode tricky construction" + nl);
    OBL_REQUIRE(accommodation.has_value());
    OBL_REQUIRE(construction.has_value());
    OBL_CHECK_EQ(accommodation->tricky, "accommodation-away");
    OBL_CHECK_EQ(construction->tricky, "construction");
    OBL_CHECK(!ReadChaosRun(base + "mode tricky unrecognized" + nl).has_value());
    OBL_CHECK(!ReadChaosRun(base + "mode unrecognized\n").has_value());
    OBL_CHECK(!ReadChaosRun(base + "mode tricky\ncommands fixed.replay\n").has_value());
    OBL_CHECK(ReadChaosRun(base)->tricky.empty());
}

OBL_TEST_CASE("chaos.tricky_construction_tries_rotations_4_to_7_then_0_to_3")
{
    class FitsAt final : public Queries {
    public:
        bool BuildingFits(std::string_view, int32_t x, int32_t y, int32_t dir) const override { return x == 1 && y == 2 && dir <= 3; }
    } queries;
    WorldView view;
    view.mapWidth = 3;
    view.mapHeight = 3;
    view.cash = 10000;
    for (const char* key : {"A", "B"}) {
        BuildingType type;
        type.key = key;
        type.cost = 100;
        type.maxInstances = 5;
        type.allowed = true;
        view.buildingTypes.push_back(type);
    }
    BuildingType locked;
    locked.key = "C";
    locked.tree = "A";
    locked.lockedByTree = true;
    locked.maxInstances = 5;
    view.buildingTypes.push_back(locked);
    TrickyConstruction bot;
    std::vector<Command> commands;
    bot.Think(view, queries, commands);
    std::vector<std::string> text;
    for (const auto& c : commands) text.push_back(obl::command::Format(c));
    OBL_REQUIRE(text.size() == 6u);
    OBL_CHECK_EQ(text[0], "place-building key=A x=1 y=2 dir=4");
    OBL_CHECK_EQ(text[1], "place-building key=A x=1 y=2 dir=0");
    OBL_CHECK_EQ(text[2], "place-building key=B x=1 y=2 dir=5");
    OBL_CHECK_EQ(text[3], "place-building key=B x=1 y=2 dir=1");
    OBL_CHECK_EQ(text[4], "place-building key=A x=1 y=2 dir=0"); // the same footprint twice in one tick
    OBL_CHECK_EQ(text[5], "set-game-speed level=2");
    TrickyConstruction skipped;
    commands.clear();
    view.buildingTypes.clear();
    skipped.Think(view, queries, commands);
    OBL_CHECK(commands.empty());
    OBL_REQUIRE(skipped.observations.size() == 1u);
}

OBL_TEST_CASE("chaos.tricky_accommodation_demolishes_the_fullest_block_by_day_or_at_night")
{
    WorldView view;
    view.hour = 9.0f;
    for (auto [id, key, guests] : {std::tuple{0u, "hotel", 100}, {41u, "accomblk", 6}, {40u, "accomblk", 0}}) {
        Building building;
        building.id = id;
        building.key = key;
        building.guests = guests;
        view.buildings.push_back(building);
    }
    for (const bool away : {true, false}) {
        TrickyAccommodation bot(away);
        std::vector<Command> commands;
        view.day = 0;
        view.hour = 9.0f;
        bot.Think(view, FitsNowhere(), commands);
        OBL_REQUIRE(commands.size() == 1u); // the speed
        commands.clear();
        view.hour = 10.0f;
        bot.Think(view, FitsNowhere(), commands);
        OBL_CHECK_EQ(commands.size(), away ? 2u : 0u);
        view.day = 1;
        view.hour = 1.0f;
        commands.clear();
        bot.Think(view, FitsNowhere(), commands);
        if (!away) {
            OBL_REQUIRE(commands.size() == 2u);
            OBL_CHECK_EQ(obl::command::Format(commands[0]), "demolish object=41");
        }
    }
    TrickyAccommodation skipped(false);
    std::vector<Command> commands;
    view.buildings.resize(1);
    skipped.Think(view, FitsNowhere(), commands);
    OBL_CHECK(commands.empty());
    OBL_REQUIRE(skipped.observations.size() == 1u);
}
