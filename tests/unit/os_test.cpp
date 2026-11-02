// Portable operating-system services (src/core/os.h) that replace the game code's direct Windows calls.

#include "unit_test.h"

#include "core/os.h"
#include "temp_dir.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace obl;

OBL_TEST_CASE("os: TickMs advances in milliseconds")
{
    const uint32_t start = os::TickMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const uint32_t elapsed = os::TickMs() - start; // wrapping difference, like GetTickCount's
    OBL_CHECK(elapsed >= 25);
    OBL_CHECK(elapsed < 5000);
}

OBL_TEST_CASE("os: GetEnv reads set and missing variables")
{
#ifdef _WIN32
    _putenv_s("OBL_OS_TEST_VALUE", "a b/c");
#else
    setenv("OBL_OS_TEST_VALUE", "a b/c", 1);
#endif
    const auto value = os::GetEnv("OBL_OS_TEST_VALUE");
    OBL_REQUIRE(value.has_value());
    OBL_CHECK(*value == "a b/c");
    OBL_CHECK(os::HasEnv("OBL_OS_TEST_VALUE"));
    OBL_CHECK(!os::GetEnv("OBL_OS_TEST_SURELY_MISSING").has_value());
    OBL_CHECK(!os::HasEnv("OBL_OS_TEST_SURELY_MISSING"));
}

OBL_TEST_CASE("os: ListDirectory sorts ignoring case and skips dot entries")
{
    const obl::test::TempPath tmp("obl_os_list");
    const auto& root = tmp.path;
    std::error_code ec;
    std::filesystem::create_directories(root / "Beta", ec);
    for (const char* name : {"save2.dat", "SAVE10.DAT", "alpha.txt", "save_x.bak"}) std::ofstream(root / name) << "x";
    const auto entries = os::ListDirectory(root.string());
    OBL_REQUIRE(entries.size() == 5);
    OBL_CHECK(entries[0].name == "alpha.txt");
    OBL_CHECK(entries[1].name == "Beta");
    OBL_CHECK(entries[1].directory);
    OBL_CHECK(!entries[0].directory);
    OBL_CHECK(entries[2].name == "SAVE10.DAT");
    OBL_CHECK(entries[3].name == "save2.dat");
    OBL_CHECK(entries[4].name == "save_x.bak");
    OBL_CHECK(os::ListDirectory((root / "missing").string()).empty());
}

OBL_TEST_CASE("os: MatchesPattern is a case-insensitive prefix*suffix")
{
    OBL_CHECK(os::MatchesPattern("save3.dat", "save", ".dat"));
    OBL_CHECK(os::MatchesPattern("SAVE3.DAT", "save", ".dat"));
    OBL_CHECK(os::MatchesPattern("save.dat", "save", ".dat"));
    OBL_CHECK(!os::MatchesPattern("sav.dat", "save", ".dat"));
    OBL_CHECK(!os::MatchesPattern("save3.dat.bak", "save", ".dat"));
    OBL_CHECK(!os::MatchesPattern("game3.dat", "save", ".dat"));
}

OBL_TEST_CASE("os: CreateDirectoryIfMissing leaves an existing file alone")
{
    const obl::test::TempPath tmp("obl_os_mkdir");
    const auto& root = tmp.path;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    os::CreateDirectoryIfMissing((root / "photos").string().c_str());
    OBL_CHECK(std::filesystem::is_directory(root / "photos"));
    os::CreateDirectoryIfMissing((root / "photos").string().c_str()); // already there
    std::ofstream(root / "SaveGames") << "x";
    os::CreateDirectoryIfMissing((root / "SaveGames").string().c_str());
    OBL_CHECK(std::filesystem::is_regular_file(root / "SaveGames"));
}

OBL_TEST_CASE("os: ResolvePath resolves the game's separators and case")
{
    const obl::test::TempPath tmp("obl_os_path");
    const auto& root = tmp.path;
    std::error_code ec;
    std::filesystem::create_directories(root / "SaveGames", ec);
    std::filesystem::create_directories(root / "data" / "Script", ec);
    std::ofstream(root / "SaveGames" / "save3.dat") << "x";
    const std::string base = root.generic_string() + "/";
#ifdef _WIN32
    OBL_CHECK(os::ResolvePath(base + "savegames\\SAVE3.dat") == base + "savegames\\SAVE3.dat"); // NTFS resolves it itself
#else
    OBL_CHECK(os::ResolvePath(base + "savegames\\SAVE3.dat") == base + "SaveGames/save3.dat");
    OBL_CHECK(os::ResolvePath(base + "DATA/script/") == base + "data/Script/");
    // Nothing matches from `new`: the rest stays as written, ready to be created.
    OBL_CHECK(os::ResolvePath(base + "savegames\\new\\Save9.dat") == base + "SaveGames/new/Save9.dat");
    OBL_CHECK(os::ResolvePath(base + "SaveGames/save3.dat") == base + "SaveGames/save3.dat");
    // Relative to the current directory, like the game's own paths.
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(root);
    OBL_CHECK(os::ResolvePath("Data\\SCRIPT") == "data/Script");
    std::filesystem::current_path(previous);
#endif
}
