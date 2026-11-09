// The settings window's model (src/game/game/start_settings.h): what it reads from openbeach.ini and BeachLife.ini, and that it
// writes only what the player changed, in the form the game reads back.

#include "unit_test.h"

#include "core/ini_file.h"
#include "game/game/start_settings.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace obl;
namespace settings = obl::game::start_settings;

namespace {

namespace fs = std::filesystem;

void WriteFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A game directory with a pack imported in `language` that has the tables `tables` (language codes).
fs::path MakePack(const fs::path& gameDir, const std::string& language, std::initializer_list<const char*> tables)
{
    const fs::path pack = gameDir / "packs" / "base";
    WriteFile(pack / "pack.json", "{\"language\": \"" + language + "\"}");
    for (const char* table : tables) WriteFile(pack / "text" / (std::string(table) + ".json"), "{}");
    return pack;
}

} // namespace

OBL_TEST_CASE("start settings: a fresh game directory gives the defaults and the game's language")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "pl", {"en", "pl"});
    const settings::Values values = settings::Load(dir.path, pack);
    OBL_CHECK_EQ(values.languages.size(), 2u);
    OBL_CHECK_EQ(values.languages[0], 0u);
    OBL_CHECK_EQ(values.languages[1], 5u);
    // No BeachLife.ini: the game requests English, which this pack has.
    OBL_CHECK_EQ(values.language, 0u);
    // A pack without English: the game takes the language the copy was imported in.
    const fs::path polish = MakePack(dir.path / "polish", "pl", {"it", "pl"});
    OBL_CHECK_EQ(settings::Load(dir.path / "polish", polish).language, 5u);
    OBL_CHECK(values.windowed);
    OBL_CHECK(values.confineMouse);
    OBL_CHECK(values.playMovie);
    OBL_CHECK(values.showAtStart);
    OBL_CHECK_EQ(values.maxFps, 60);
    OBL_CHECK(values.scale == 1.0);
    // Reading writes nothing.
    OBL_CHECK(!fs::exists(dir.path / settings::kFileName));
    OBL_CHECK(!fs::exists(dir.path / "BeachLife.ini"));
}

OBL_TEST_CASE("start settings: an unchanged window writes no file")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "en", {"en"});
    const settings::Values values = settings::Load(dir.path, pack);
    OBL_CHECK(settings::Save(dir.path, values, values));
    OBL_CHECK(!fs::exists(dir.path / settings::kFileName));
    OBL_CHECK(!fs::exists(dir.path / "BeachLife.ini"));
}

OBL_TEST_CASE("start settings: a change creates the commented openbeach.ini and the game reads it")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "en", {"en"});
    const settings::Values before = settings::Load(dir.path, pack);
    settings::Values after = before;
    after.windowed = false;
    after.scale = 2.5;
    after.maxFps = 0;
    after.showAtStart = false;
    OBL_REQUIRE(settings::Save(dir.path, before, after));
    const std::string text = ReadFile(dir.path / settings::kFileName);
    // The comments of the default file stay, so the file explains itself to a player who opens it.
    OBL_CHECK(text.find("; Alt+Enter switches between the two") != std::string::npos);
    OBL_CHECK(text.find("Windowed=0\r\n") != std::string::npos);
    OBL_CHECK(text.find("Scale=2.5\r\n") != std::string::npos);
    OBL_CHECK(text.find("MaxFps=0\r\n") != std::string::npos);
    OBL_CHECK(text.find("ShowSettings=0\r\n") != std::string::npos);
    OBL_CHECK(text.find("PlayMovie=1\r\n") != std::string::npos);
    OBL_CHECK(settings::Load(dir.path, pack) == after);
    // The language did not change: BeachLife.ini is the game's, left alone.
    OBL_CHECK(!fs::exists(dir.path / "BeachLife.ini"));
}

OBL_TEST_CASE("start settings: a language change writes BeachLife.ini as the game reads it")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "en", {"en", "de"});
    const settings::Values before = settings::Load(dir.path, pack);
    OBL_CHECK_EQ(before.language, 0u);
    settings::Values after = before;
    after.language = 3;
    OBL_REQUIRE(settings::Save(dir.path, before, after));
    const fs::path main = dir.path / "BeachLife.ini";
    // Without Version the game reads nothing of the file (settings::LoadMain).
    OBL_CHECK_EQ(ini::ReadString(main, "Beach Life Options", "Version", "", 16), std::string("1"));
    OBL_CHECK_EQ(ini::ReadInt(main, "Language", "Language", -1), 3);
    OBL_CHECK_EQ(settings::Load(dir.path, pack).language, 3u);
}

OBL_TEST_CASE("start settings: a language change keeps the rest of an existing BeachLife.ini")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "en", {"en", "fr"});
    WriteFile(dir.path / "BeachLife.ini", "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=0\r\n[Video]\r\nResWidth=1024\r\n");
    const settings::Values before = settings::Load(dir.path, pack);
    settings::Values after = before;
    after.language = 1;
    OBL_REQUIRE(settings::Save(dir.path, before, after));
    OBL_CHECK_EQ(ReadFile(dir.path / "BeachLife.ini"),
            std::string("[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=1\r\n[Video]\r\nResWidth=1024\r\n"));
}

OBL_TEST_CASE("start settings: a requested language the pack lacks shows the game's fallback")
{
    test::TempPath dir("obl-start-settings");
    const fs::path pack = MakePack(dir.path, "it", {"en", "it"});
    WriteFile(dir.path / "BeachLife.ini", "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=5\r\n");
    OBL_CHECK_EQ(settings::Load(dir.path, pack).language, 2u);
    // A file without Version is not read by the game, so its language does not count either: English, the default.
    WriteFile(dir.path / "BeachLife.ini", "[Language]\r\nLanguage=2\r\n");
    OBL_CHECK_EQ(settings::Load(dir.path, pack).language, 0u);
}

OBL_TEST_CASE("start settings: the default file is created once, never replaced")
{
    test::TempPath dir("obl-start-settings");
    fs::create_directories(dir.path);
    OBL_REQUIRE(settings::CreateDefaultFile(dir.path / settings::kFileName));
    const std::string text = ReadFile(dir.path / settings::kFileName);
    OBL_CHECK_EQ(text, std::string(settings::kDefaultFile));
    OBL_CHECK_EQ(ini::ReadInt(dir.path / settings::kFileName, "Launcher", "ShowSettings", -1), 1);
    // An existing file is never replaced.
    WriteFile(dir.path / settings::kFileName, "[Display]\r\nWindowed=0\r\n");
    OBL_CHECK(settings::CreateDefaultFile(dir.path / settings::kFileName));
    OBL_CHECK_EQ(ReadFile(dir.path / settings::kFileName), std::string("[Display]\r\nWindowed=0\r\n"));
}
