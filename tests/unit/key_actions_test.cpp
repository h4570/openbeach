// Named actions and their keys (src/formats/key_actions.h): the default table is the game's own keys, a mod declares and moves
// actions, the player's openbeach.ini beats the mods, and a key belongs to one action only.

#include "unit_test.h"

#include "formats/key_actions.h"
#include "core/key_code.h"

#include "core/key_names.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <map>
#include <string>
#include <vector>

using namespace obl;
namespace ka = obl::key_actions;
using obl::input::KeyCode;

namespace {

ka::KeyList K(std::initializer_list<uint8_t> keys)
{
    return ka::KeyList(keys);
}

const ka::Entry* Find(const ka::Table& table, const std::string& name)
{
    for (const ka::Entry& e : table.entries)
        if (e.name == name) return &e;
    return nullptr;
}

ka::PlayerLookup Ini(std::map<std::string, std::string> values)
{
    return [values](std::string_view name) -> std::optional<std::string> {
        const auto it = values.find(std::string(name));
        if (it == values.end()) return std::nullopt;
        return it->second;
    };
}

ka::Table Res(const std::vector<ka::ModKeys>& mods, const ka::PlayerLookup& player = nullptr)
{
    return ka::Resolve(mods, player);
}

} // namespace

OBL_TEST_CASE("key_actions.the_default_table_is_the_keys_the_game_always_had")
{
    const ka::Table table = Res({});
    OBL_CHECK(table.conflicts.empty());
    OBL_CHECK(table.problems.empty());
    struct Row {
        const char* name;
        ka::KeyList keys;
        bool held;
    };
    const std::vector<Row> expected = {
            {"camera.left", K({KeyCode::Left}), true},
            {"camera.right", K({KeyCode::Right}), true},
            {"camera.up", K({KeyCode::Up}), true},
            {"camera.down", K({KeyCode::Down}), true},
            {"game.pause", K({KeyCode::P}), false},
            {"panel.load_game", K({KeyCode::F5}), false},
            {"panel.save_game", K({KeyCode::F4}), false},
            {"panel.options", K({KeyCode::F2}), false},
            {"panel.music", K({KeyCode::M}), false},
            {"panel.build", K({KeyCode::B}), false},
            {"panel.scenery", K({KeyCode::S}), false},
            {"camera.zoom_in", K({KeyCode::PageUp}), false},
            {"camera.zoom_out", K({KeyCode::PageDown}), false},
            {"game.speed_up", K({KeyCode::Equals}), false},
            {"game.speed_down", K({KeyCode::Minus}), false},
            {"tool.cancel", K({KeyCode::Escape}), false},
            {"place.rotate", K({KeyCode::Space}), false},
            {"place.keep", K({KeyCode::LeftShift, KeyCode::RightShift}), true},
    };
    OBL_CHECK_EQ(table.entries.size(), expected.size());
    OBL_CHECK_EQ(table.entries.size(), ka::kEngineCount);
    for (size_t i = 0; i < expected.size(); ++i) {
        OBL_CHECK(table.entries[i].name == expected[i].name);
        OBL_CHECK(table.entries[i].keys == expected[i].keys);
        OBL_CHECK_EQ(table.entries[i].held, expected[i].held);
        OBL_CHECK(ka::FindEngine(expected[i].name) == static_cast<ka::Engine>(i));
    }
}

OBL_TEST_CASE("key_actions.the_player_binds_a_key_in_the_ini")
{
    const ka::Table table = Res({}, Ini({{"game.pause", "F10"}, {"place.keep", "none"}, {"panel.music", "Q, W"}}));
    OBL_CHECK(Find(table, "game.pause")->keys == K({KeyCode::F10}));
    OBL_CHECK(Find(table, "place.keep")->keys.empty());
    OBL_CHECK(Find(table, "panel.music")->keys == K({KeyCode::Q, KeyCode::W}));
    OBL_CHECK(Find(table, "panel.build")->keys == K({KeyCode::B}));
    // A key no key has is a problem and the default stays.
    const ka::Table bad = Res({}, Ini({{"game.pause", "Banana"}}));
    OBL_CHECK_EQ(bad.problems.size(), 1u);
    OBL_CHECK(Find(bad, "game.pause")->keys == K({KeyCode::P}));
}

OBL_TEST_CASE("key_actions.a_mod_declares_an_action_and_moves_an_engine_one")
{
    const std::vector<ka::ModKeys> mods = {{"fit", {{"zoom_fit", "Fit", K({KeyCode::F9})}}, {{"game.pause", K({KeyCode::F10})}}}};
    const ka::Table table = Res(mods);
    OBL_CHECK(table.conflicts.empty());
    const ka::Entry* mine = Find(table, "fit.zoom_fit");
    OBL_CHECK(mine != nullptr);
    OBL_CHECK(mine->mod == "fit" && mine->id == "zoom_fit");
    OBL_CHECK(mine->keys == K({KeyCode::F9}));
    OBL_CHECK(Find(table, "game.pause")->keys == K({KeyCode::F10}));
    // A binding to an action nobody has is skipped and reported.
    const ka::Table odd = Res({{"fit", {}, {{"game.nothing", K({KeyCode::F10})}}}});
    OBL_CHECK_EQ(odd.problems.size(), 1u);
}

OBL_TEST_CASE("key_actions.a_key_taken_by_two_actions_goes_to_the_later_mod_and_the_player_beats_the_mods")
{
    const std::vector<ka::ModKeys> mods = {
            {"a", {{"x", "", K({KeyCode::F9})}}, {}},
            {"b", {{"y", "", K({KeyCode::F9})}}, {{"game.pause", K({KeyCode::F9})}}},
    };
    const ka::Table table = Res(mods);
    // "b.y" and "game.pause" are both b's: the later action wins inside a layer; "a.x" loses to both.
    OBL_CHECK_EQ(table.conflicts.size(), 1u);
    OBL_CHECK(Find(table, "a.x")->keys.empty());
    OBL_CHECK(Find(table, "b.y")->keys == K({KeyCode::F9}));
    OBL_CHECK(Find(table, "game.pause")->keys.empty());
    OBL_CHECK(table.conflicts[0].find("\"b.y\" wins") != std::string::npos);

    // The player's binding beats every mod.
    const ka::Table player = Res(mods, Ini({{"a.x", "F9"}}));
    OBL_CHECK(Find(player, "a.x")->keys == K({KeyCode::F9}));
    OBL_CHECK(Find(player, "b.y")->keys.empty());
    OBL_CHECK(player.conflicts[0].find("\"a.x\" wins (the player's binding)") != std::string::npos);

    // A mod moving an engine action onto a key the engine used elsewhere takes it from that action.
    const ka::Table steal = Res({{"m", {}, {{"game.pause", K({KeyCode::B})}}}});
    OBL_CHECK(Find(steal, "game.pause")->keys == K({KeyCode::B}));
    OBL_CHECK(Find(steal, "panel.build")->keys.empty());
}

OBL_TEST_CASE("key_actions.the_installed_table_feeds_the_game_and_a_takeover_gives_the_action_back")
{
    const auto pauseKeys = [] { return ka::Keys(ka::Engine::GamePause); };
    OBL_CHECK(pauseKeys().size() == 1 && pauseKeys()[0] == KeyCode::P);
    ka::Install(Res({{"m", {{"go", "", K({KeyCode::F9})}}, {{"game.pause", K({KeyCode::F10})}}}}));
    OBL_CHECK(pauseKeys().size() == 1 && pauseKeys()[0] == KeyCode::F10);
    OBL_CHECK_EQ(ka::ModActionsFor(KeyCode::F9).size(), 1u);
    OBL_CHECK(ka::ModActionsFor(KeyCode::P).empty());

    ka::SetTakenOver(ka::Engine::GamePause, true);
    OBL_CHECK(pauseKeys().empty());
    ka::SetTakenOver(ka::Engine::GamePause, false);
    OBL_CHECK(pauseKeys().size() == 1);

    ka::Install(Res({}));
    OBL_CHECK(pauseKeys().size() == 1 && pauseKeys()[0] == KeyCode::P);
}

OBL_TEST_CASE("key_actions.key_lists_parse_names_ignoring_case_and_refuse_unknown_ones")
{
    ka::KeyList keys;
    std::string error;
    OBL_CHECK(ka::ParseKeys(" f5 , Escape,f5", keys, error));
    OBL_CHECK(keys == K({KeyCode::F5, KeyCode::Escape}));
    OBL_CHECK(ka::ParseKeys("None", keys, error) && keys.empty());
    OBL_CHECK(!ka::ParseKeys("F5, Nope", keys, error));
    OBL_CHECK(error.find("Nope") != std::string::npos);
    OBL_CHECK(!ka::ParseKeys("A,B,C,D,E", keys, error));
}

// docs/modding/reference/actions.md lists every engine action with its default keys and summary, and nothing else.
OBL_TEST_CASE("key_actions.the_reference_page_lists_every_engine_action_with_its_default_keys")
{
    std::ifstream in(std::filesystem::path(OBL_REPO_DIR) / "docs/modding/reference/actions.md", std::ios::binary);
    const std::string page((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    OBL_REQUIRE(!page.empty());
    const std::regex row(R"(^\| `([a-z_.]+)` \| ([^|]+) \| ([^|]+) \|)");
    std::vector<std::string> listed;
    bool inside = false;
    std::istringstream lines(page);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("## ", 0) == 0) inside = line == "## Engine actions";
        std::smatch match;
        if (!inside || !std::regex_search(line, match, row)) continue;
        const auto engine = ka::FindEngine(match[1].str());
        OBL_REQUIRE(engine.has_value());
        const ka::EngineAction& action = ka::EngineActions()[static_cast<size_t>(*engine)];
        std::string keys;
        for (uint8_t key : action.keys)
            if (key) keys += std::string(keys.empty() ? "`" : ", `") + std::string(input::KeyName(key)) + "`";
        OBL_CHECK_EQ(match[2].str(), keys);
        // The summary may add "(held)" for a held action.
        OBL_CHECK_EQ(match[3].str(), std::string(action.summary) + (action.held ? " (held)" : ""));
        listed.push_back(match[1].str());
    }
    OBL_CHECK_EQ(listed.size(), ka::kEngineCount);
    for (size_t i = 0; i < listed.size(); ++i) OBL_CHECK(listed[i] == ka::EngineActions()[i].name);
}

OBL_TEST_CASE("key_actions.a_rebinding_refuses_a_key_another_action_holds")
{
    const ka::Table defaults = Res({});
    const ka::Table now = Res({}, Ini({{"game.pause", "F10"}}));
    std::string error;
    // A free key, a list, "none" and the action's own key pass.
    OBL_CHECK(ka::CheckBind(now, defaults, "game.speed_up", "F11", error));
    OBL_CHECK(ka::CheckBind(now, defaults, "game.speed_up", "F11, F12", error));
    OBL_CHECK(ka::CheckBind(now, defaults, "game.speed_up", "none", error));
    OBL_CHECK(ka::CheckBind(now, defaults, "game.pause", "F10", error));
    // Another action's key is refused, by name, ignoring case.
    OBL_CHECK(!ka::CheckBind(now, defaults, "game.speed_up", "f10", error));
    OBL_CHECK_EQ(error, std::string("key \"F10\" is bound to \"game.pause\""));
    OBL_CHECK(!ka::CheckBind(now, defaults, "game.speed_up", "F11, F10", error));
    // An unknown key, too many keys and an unknown action.
    OBL_CHECK(!ka::CheckBind(now, defaults, "game.speed_up", "NoSuchKey", error));
    OBL_CHECK_EQ(error, std::string("no key is named \"NoSuchKey\""));
    OBL_CHECK(!ka::CheckBind(now, defaults, "game.speed_up", "F1, F3, F6, F7, F8", error));
    OBL_CHECK(!ka::CheckBind(now, defaults, "no.such", "F11", error));
    OBL_CHECK_EQ(error, std::string("no action is named \"no.such\""));
}

OBL_TEST_CASE("key_actions.resetting_to_default_is_refused_while_another_action_holds_the_default_key")
{
    const ka::Table defaults = Res({});
    // game.pause moved to F10 and panel.music took P: giving game.pause its P back would steal it.
    const ka::Table now = Res({}, Ini({{"game.pause", "F10"}, {"panel.music", "P"}}));
    std::string error;
    OBL_CHECK(!ka::CheckBind(now, defaults, "game.pause", "default", error));
    OBL_CHECK_EQ(error, std::string("key \"P\" is bound to \"panel.music\""));
    OBL_CHECK(ka::CheckBind(now, defaults, "panel.music", "default", error)); // M is free
    OBL_CHECK(ka::CheckBind(Res({}), defaults, "game.pause", "default", error));
}
