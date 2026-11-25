#include "unit_test.h"

#include "core/byte_reader.h"
#include "game/mods/mod_archive.h"
#include "temp_dir.h"

#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string_view>

using namespace obl;

namespace {

using Bytes = std::vector<uint8_t>;
using Result = mods::ModArchive::Result;
namespace fs = std::filesystem;

constexpr std::string_view kManifest = R"({"format":"obl.mod","version":1,"id":"sample","title":"Sample","mod_version":"1","pack":"l0"})";
constexpr std::string_view kPresentation =
        R"({"format":"obl.mod","version":1,"id":"sample","title":"Sample","mod_version":"1","pack":"l0","scope":"presentation"})";
constexpr std::string_view kRecipe =
        R"({"format":"obl.asset_recipe","version":1,"source":"images/screens/l0_title.png","output":"overlay/preview.png","operations":[{"op":"crop","x":0,"y":0,"width":8,"height":8}]})";

struct File {
    std::string_view name;
    std::string_view bytes;
    uint32_t attributes = 0;
};

void Put(Bytes& zip, uint32_t value, int width = 4)
{
    for (int i = 0; i < width; ++i) zip.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void Append(Bytes& zip, std::string_view bytes)
{
    zip.insert(zip.end(), bytes.begin(), bytes.end());
}

Bytes Zip(std::initializer_list<File> files)
{
    Bytes zip;
    Bytes central;
    for (const File& file : files) {
        const auto offset = static_cast<uint32_t>(zip.size());
        const auto size = static_cast<uint32_t>(file.bytes.size());
        const auto length = static_cast<uint32_t>(file.name.size());
        const auto crc = static_cast<uint32_t>(crc32(0, std::bit_cast<const uint8_t*>(file.bytes.data()), size));
        Put(zip, 0x04034b50);
        Put(zip, 20, 2);
        for (int i = 0; i < 4; ++i) Put(zip, 0, 2); // flags, method, time, date
        Put(zip, crc);
        Put(zip, size);
        Put(zip, size);
        Put(zip, length, 2);
        Put(zip, 0, 2); // extra length
        Append(zip, file.name);
        Append(zip, file.bytes);

        Put(central, 0x02014b50);
        Put(central, 0x0314, 2); // Unix attributes
        Put(central, 20, 2);
        for (int i = 0; i < 4; ++i) Put(central, 0, 2);
        Put(central, crc);
        Put(central, size);
        Put(central, size);
        Put(central, length, 2);
        for (int i = 0; i < 4; ++i) Put(central, 0, 2);
        Put(central, file.attributes);
        Put(central, offset);
        Append(central, file.name);
    }
    const auto offset = static_cast<uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    Put(zip, 0x06054b50);
    Put(zip, 0);
    Put(zip, static_cast<uint32_t>(files.size()), 2);
    Put(zip, static_cast<uint32_t>(files.size()), 2);
    Put(zip, static_cast<uint32_t>(central.size()));
    Put(zip, offset);
    Put(zip, 0, 2);
    return zip;
}

void ExpectRefused(std::initializer_list<File> files, std::string_view reason)
{
    mods::ModArchive archive;
    std::string error;
    OBL_CHECK(!archive.Read(Zip(files), "l0", error));
    OBL_CHECK(error.find(reason) != std::string::npos);
}

} // namespace

OBL_TEST_CASE("mod_archive.refuses_unsafe_paths_and_links")
{
    for (const std::string_view name : {"../out", "sample/../out", "/sample/readme", "sample/C:/out", "sample/a\\b", "sample/readme.",
                 "sample/con.txt", "sample/a//b", "another/readme", "sample/library.DLL"}) {
        ExpectRefused({{"sample/mod.json", kManifest}, {name, "x"}}, "ZIP path");
    }
    ExpectRefused({{"sample/mod.json", kManifest}, {"sample/Readme", "x"}, {"sample/readme", "y"}}, "duplicate");
    ExpectRefused({{"sample/mod.json", kManifest}, {"sample/readme", "x"}, {"sample/readme/note", "y"}}, "duplicate");
    ExpectRefused({{"sample/mod.json", kManifest}, {"sample/link", "outside", 0120777u << 16}}, "unsupported ZIP entry");
}

OBL_TEST_CASE("mod_archive.refuses_invalid_manifest_and_scope")
{
    ExpectRefused({{"sample/note.txt", "x"}}, "mod.json");
    ExpectRefused({{"wrong/mod.json", kManifest}}, "id does not match");
    ExpectRefused({{"sample/mod.json", "[]"}}, "JSON object");
    auto manifest = std::string(kPresentation.substr(0, kPresentation.size() - 1));
    ExpectRefused({{"sample/mod.json", manifest + R"(,"api":999})"}}, "script API");
    ExpectRefused({{"sample/mod.json", kPresentation}, {"sample/scripts/presentation/main.lua", "x"}}, "no \"api\"");
    // A mod for a newer game is refused before anything is written, with the version it needs.
    auto world = std::string(kManifest.substr(0, kManifest.size() - 1));
    ExpectRefused({{"sample/mod.json", world + R"(,"engine_min":"999.0.0"})"}}, "needs OpenBeach 999.0.0 or newer; update the game");
    ExpectRefused({{"sample/mod.json", world + R"(,"engine_min":"1.x"})"}}, "engine_min");
    ExpectRefused({{"sample/mod.json", kPresentation}, {"sample/rules/game_parameters.json", "{}"}}, "may not ship");
    ExpectRefused({{"sample/mod.json", kPresentation},
                          {"sample/recipes/bad.json", R"({"format":"obl.asset_recipe","output":"sprites/bad.json"})"}},
            "may not ship");

    mods::ModArchive archive;
    std::string error;
    OBL_CHECK(!archive.Read(Zip({{"sample/mod.json", kManifest}}), "base", error));
    OBL_CHECK(error.find("made for the pack") != std::string::npos);
}

OBL_TEST_CASE("mod_archive.refuses_truncated_corrupt_and_oversized_entries")
{
    mods::ModArchive archive;
    std::string error;
    Bytes zip = Zip({{"sample/mod.json", kManifest}});
    zip[30 + std::string_view("sample/mod.json").size()] ^= 1;
    OBL_CHECK(!archive.Read(zip, "l0", error));
    OBL_CHECK(error.find("corrupt") != std::string::npos);

    zip = Zip({{"sample/mod.json", kManifest}});
    const auto central = Read<uint32_t>(zip, zip.size() - 6);
    zip[central + 27] = 16; // expands to more than the per-file limit
    OBL_CHECK(!archive.Read(zip, "l0", error));
    OBL_CHECK(error.find("expanded size") != std::string::npos);

    zip = Zip({{"sample/mod.json", kManifest}});
    zip[zip.size() - 13] = 32;
    zip[zip.size() - 11] = 32; // 8193 entries
    OBL_CHECK(!archive.Read(zip, "l0", error));
    OBL_CHECK(error.find("entry limit") != std::string::npos);

    zip.pop_back();
    OBL_CHECK(!archive.Read(zip, "l0", error));
}

OBL_TEST_CASE("mod_archive.replaces_only_with_confirmation_and_removes_cache")
{
    test::TempPath game("mod-archive");
    fs::create_directory(game.path);
    const auto pack = game.path / "packs/l0/images/screens";
    fs::create_directories(pack);
    fs::copy_file(fs::path(OBL_REPO_DIR) / "data/packs/l0/images/screens/l0_title.png", pack / "l0_title.png");

    mods::ModArchive archive;
    std::string error;
    const auto installed = game.path / "mods/sample";
    const auto cache = game.path / "mod-cache/sample";
    OBL_REQUIRE(archive.Read(Zip({{"sample/mod.json", kPresentation}, {"sample/recipes/image.json", kRecipe}}), "l0", error));
    OBL_CHECK(!fs::exists(game.path / "mods"));
    OBL_REQUIRE(archive.Write(game.path, false, error) == Result::Installed);
    OBL_CHECK(fs::is_regular_file(cache / "overlay/preview.png"));

    const auto other = Zip({{"sample/mod.json", kManifest}, {"sample/updated.txt", "new"}});
    OBL_REQUIRE(archive.Read(other, "l0", error));
    OBL_CHECK(archive.Write(game.path, false, error) == Result::AlreadyInstalled);
    OBL_CHECK(!fs::exists(installed / "updated.txt"));
    OBL_CHECK(fs::exists(cache / "overlay/preview.png"));

    OBL_REQUIRE(archive.Read(Zip({{"sample/mod.json", kManifest}, {"sample/recipes/bad.json", "{}"}}), "l0", error));
    OBL_CHECK(archive.Write(game.path, true, error) == Result::Failed);
    OBL_CHECK(fs::exists(installed / "recipes/image.json"));
    OBL_CHECK(fs::exists(cache / "overlay/preview.png"));

    OBL_REQUIRE(archive.Read(other, "l0", error));
    OBL_REQUIRE(archive.Write(game.path, true, error) == Result::Installed);
    OBL_CHECK(fs::exists(installed / "updated.txt"));
    OBL_CHECK(!fs::exists(installed / "recipes/image.json"));
    OBL_CHECK(!fs::exists(cache));
    fs::create_directories(cache);
    std::ofstream(cache / "test.png") << "generated";
    OBL_CHECK(mods::RemoveMod(game.path, "sample", error));
    OBL_CHECK(!fs::exists(installed));
    OBL_CHECK(!fs::exists(cache));
}

OBL_TEST_CASE("mod_archive.refuses_an_existing_id_with_different_case")
{
    test::TempPath game("mod-archive-collision");
    fs::create_directories(game.path / "mods/Sample");
    mods::ModArchive archive;
    std::string error;
    OBL_REQUIRE(archive.Read(Zip({{"sample/mod.json", kManifest}}), "l0", error));
    OBL_CHECK(archive.Write(game.path, false, error) == Result::Failed);
    OBL_CHECK(error.find("capitalization") != std::string::npos);
    OBL_CHECK(!fs::exists(game.path / "mods/Sample/mod.json"));
}
