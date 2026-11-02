#include "unit_test.h"
#include "importer/src/writers/script_convert.h"
#include "importer/src/source/source.h"
#include "formats/script_pack.h"
#include "temp_dir.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using obl::script_pack::Kind;

namespace {

// The text of an installed script: every byte XOR 0xFF.
std::string Decoded(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    for (char& c : data) c = static_cast<char>(c ^ 0xFF);
    return data;
}

void WriteFile(const std::filesystem::path& file, const std::string& content)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
}

} // namespace

OBL_TEST_CASE("script_pack.header_and_scenario_ids")
{
    OBL_CHECK(obl::script_pack::Header(Kind::Object) == "#obl-script 1 object\n");
    OBL_CHECK(obl::script_pack::Header(Kind::World) == "#obl-script 1 world\n");

    std::string id;
    OBL_CHECK(obl::script_pack::ScenarioId("Scenario1", id) && id == "s1");
    OBL_CHECK(obl::script_pack::ScenarioId("scenario12", id) && id == "s12");
    OBL_CHECK(obl::script_pack::ScenarioId("SandScenario3", id) && id == "sandbox3");
    OBL_CHECK(!obl::script_pack::ScenarioId("Scenario", id));
    OBL_CHECK(!obl::script_pack::ScenarioId("Scenario1x", id));
    OBL_CHECK(!obl::script_pack::ScenarioId("Sandbox3", id));
    OBL_CHECK(!obl::script_pack::ScenarioId("Guests01", id));
}

OBL_TEST_CASE("script_pack.pack_path_maps_engine_paths")
{
    // Object logics: lowercased name, any slash direction, object kind only.
    OBL_CHECK(obl::script_pack::PackPath("data/script/object/guest.lgc", Kind::Object) == "scripts/object/guest.obs");
    OBL_CHECK(obl::script_pack::PackPath("Data\\Script\\Object\\Guest.lgc", Kind::Object) == "scripts/object/guest.obs");
    OBL_CHECK(obl::script_pack::PackPath("data/script/object/guest.lgc", Kind::World).empty());
    OBL_CHECK(obl::script_pack::PackPath("data/script/object/.lgc", Kind::Object).empty());
    OBL_CHECK(obl::script_pack::PackPath("data/script/object/guest.ble", Kind::Object).empty());

    // Scenario scripts: the folder names the id, and the file repeats the folder name.
    OBL_CHECK(obl::script_pack::PackPath("Data/Scenario/Scenario1/Scenario1.lgc", Kind::World) == "scenarios/s1/world.wsc");
    OBL_CHECK(
            obl::script_pack::PackPath("data\\scenario\\sandscenario3\\SandScenario3.lgc", Kind::World) == "scenarios/sandbox3/world.wsc");
    OBL_CHECK(obl::script_pack::PackPath("Data/Scenario/Scenario1/Scenario2.lgc", Kind::World).empty());
    OBL_CHECK(obl::script_pack::PackPath("Data/Scenario/Other/Other.lgc", Kind::World).empty());
    OBL_CHECK(obl::script_pack::PackPath("Data/Scenario/Scenario1/Scenario1.lgc", Kind::Object).empty());
    OBL_CHECK(obl::script_pack::PackPath("Scenario1.lgc", Kind::World).empty());
}

OBL_TEST_CASE("script_pack.read_script_checks_the_header_and_keeps_the_text")
{
    const obl::test::TempPath tmp("obl-script-pack-read");
    const auto& dir = tmp.path;
    const std::string body = "State FIRST\r\n  Message MT_ONE Me 1 2;\r\nEnd\r\n";
    std::string text, error;

    WriteFile(dir / "good.obs", "#obl-script 1 object\n" + body);
    OBL_REQUIRE(obl::script_pack::ReadScript(dir / "good.obs", Kind::Object, text, error));
    OBL_CHECK(text == body); // CRLF line ends stay

    WriteFile(dir / "wrong_kind.obs", "#obl-script 1 world\n" + body);
    OBL_CHECK(!obl::script_pack::ReadScript(dir / "wrong_kind.obs", Kind::Object, text, error));
    OBL_CHECK(error.find("wrong_kind.obs") != std::string::npos);

    WriteFile(dir / "no_header.obs", body);
    OBL_CHECK(!obl::script_pack::ReadScript(dir / "no_header.obs", Kind::Object, text, error));

    WriteFile(dir / "empty.obs", "#obl-script 1 object\n");
    OBL_CHECK(!obl::script_pack::ReadScript(dir / "empty.obs", Kind::Object, text, error));

    OBL_CHECK(!obl::script_pack::ReadScript(dir / "missing.obs", Kind::Object, text, error));
    OBL_CHECK(error.find("Import the game data") != std::string::npos);
}

OBL_TEST_CASE("script_pack.read_pack_script_refuses_a_path_of_the_other_kind")
{
    const obl::test::TempPath tmp("obl-script-pack-kind");
    std::string text, error;
    OBL_CHECK(!obl::script_pack::ReadPackScript(tmp.path, "data/script/object/guest.lgc", Kind::World, text, error));
    OBL_CHECK(error.find("not a script path") != std::string::npos);
}

OBL_TEST_CASE("data.script_pack_matches_installed_scripts")
{
    const auto objectDir = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Script/Object");
    const auto scenarioDir = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Scenario");
    if (!objectDir || !scenarioDir) OBL_SKIP("no installation scripts");

    const obl::test::TempPath tmp("obl-script-pack-data");
    int files = 0;
    std::string error;
    OBL_REQUIRE(obl::convert::ConvertScripts(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(tmp.path), files, error));
    const std::filesystem::path packs[] = {tmp.path, std::filesystem::path(OBL_GAME_DIR) / "packs" / "base"};

    const auto check = [&](const std::string& enginePath, Kind kind, const std::filesystem::path& installed) {
        const std::string expected = Decoded(installed);
        for (const auto& pack : packs) {
            std::string text;
            OBL_REQUIRE(obl::script_pack::ReadPackScript(pack, enginePath, kind, text, error));
            OBL_CHECK(text == expected);
        }
    };

    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(*objectDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ble") continue;
        std::string name = entry.path().stem().string();
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        check("data/script/object/" + name + ".lgc", Kind::Object, entry.path());
        ++checked;
    }
    OBL_CHECK_EQ(checked, 8);

    for (const auto& entry : std::filesystem::directory_iterator(*scenarioDir)) {
        if (!entry.is_directory()) continue;
        const std::string folder = entry.path().filename().string();
        std::filesystem::path script;
        for (const auto& file : std::filesystem::directory_iterator(entry.path()))
            if (file.path().extension() == ".ble") script = file.path();
        OBL_REQUIRE(!script.empty());
        check("Data/Scenario/" + folder + "/" + folder + ".lgc", Kind::World, script);
        ++checked;
    }
    OBL_CHECK_EQ(checked, 32);
    OBL_CHECK_EQ(files, 32);
}
