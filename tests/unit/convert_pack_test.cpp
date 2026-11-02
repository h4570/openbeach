#include "unit_test.h"
#include "core/json_schema.h"
#include "importer/src/writers/pack_manifest.h"
#include "formats/schemas.h"
#include "core/sha256.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace obl::convert;
namespace fs = std::filesystem;

namespace {

void WriteFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Source SampleSource()
{
    Source source;
    source.sha256["BeachLife.exe"] = std::string(64, 'a');
    source.sha256["Data/Sprites/BLSprite.hdr"] = std::string(64, 'b');
    return source;
}

// Fills `dir` (a fresh per-test directory) with a text table per language, a JSON, a sprite sheet and a map terrain PNG.
void SamplePack(const fs::path& dir, std::initializer_list<const char*> languages)
{
    for (const char* language : languages)
        WriteFile(dir / "text" / (std::string(language) + ".json"), std::string("{\"language\":\"") + language + "\"}\n");
    WriteFile(dir / "catalog/index.json", "{}\n");
    WriteFile(dir / "sprites/gail/sheet_0.png", "png bytes");
    WriteFile(dir / "maps/island/terrain.png", "terrain");
}

} // namespace

OBL_TEST_CASE("convert.pack_tiers")
{
    OBL_CHECK_EQ(PackFileTier("catalog/buildings/beachbar.json"), 1);
    OBL_CHECK_EQ(PackFileTier("sprites/gail/sheet_0.png"), 2);
    OBL_CHECK_EQ(PackFileTier("sounds/splash.wav"), 2);
    OBL_CHECK_EQ(PackFileTier("music/theme.mp3"), 2);
    OBL_CHECK_EQ(PackFileTier("maps/island/terrain.png"), 1);
    OBL_CHECK_EQ(PackFileTier("maps/island/height.png"), 1);
    OBL_CHECK_EQ(PackFileTier("maps/island/color.png"), 2);
}

OBL_TEST_CASE("convert.pack_manifest")
{
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    const obl::test::TempPath tmp("obl_convert_pack_single");
    const auto& dir = tmp.path;
    SamplePack(dir, {"pl"});
    WriteFile(dir / "pack.json", "stale"); // an earlier manifest is not part of the file list

    int files = 0;
    OBL_REQUIRE(WritePackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::nullopt, schemas, files, error));
    OBL_CHECK_EQ(files, 4);
    const std::string first = ReadFile(dir / "pack.json");
    const auto manifest = nlohmann::ordered_json::parse(first);
    OBL_CHECK_EQ(manifest["id"], "beachlife-2002");
    OBL_CHECK_EQ(manifest["language"], "pl");
    OBL_CHECK_EQ(manifest["provenance"], "converted");
    OBL_CHECK_EQ(manifest["version"], 2);
    OBL_CHECK(!manifest.contains("source"));
    OBL_CHECK_EQ(manifest["import"]["reference"].get<std::string>(), obl::convert::ImportReference(SampleSource().sha256));
    OBL_CHECK(first.find("drm-free") == std::string::npos && first.find("BeachLife.exe") == std::string::npos);
    OBL_CHECK(!manifest["files"].contains("pack.json"));
    OBL_CHECK_EQ(manifest["files"]["catalog/index.json"]["sha256"].get<std::string>(),
            obl::sha256::OfFile(dir / "catalog/index.json").value_or(""));
    OBL_CHECK_EQ(manifest["files"]["catalog/index.json"]["tier"], 1);
    OBL_CHECK_EQ(manifest["files"]["sprites/gail/sheet_0.png"]["tier"], 2);
    OBL_CHECK_EQ(manifest["files"]["maps/island/terrain.png"]["tier"], 1);
    OBL_CHECK_EQ(manifest["files"].begin().key(), "catalog/index.json"); // sorted by path

    // Deterministic: a second run over the same pack (now holding pack.json) writes the same bytes.
    OBL_REQUIRE(WritePackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::string("pl"), schemas, files, error));
    OBL_CHECK_EQ(ReadFile(dir / "pack.json"), first);
}

OBL_TEST_CASE("convert.pack_manifest_language")
{
    nlohmann::ordered_json manifest;
    std::string error;
    const obl::test::TempPath several("obl_convert_pack_several");
    const auto& dir = several.path;
    SamplePack(dir, {"de", "pl"});
    OBL_REQUIRE(BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::nullopt, manifest, error));
    OBL_CHECK_EQ(manifest["language"], "de");
    WriteFile(dir / "text/en.json", "{}");
    OBL_REQUIRE(BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::nullopt, manifest, error));
    OBL_CHECK_EQ(manifest["language"], "en");
    OBL_REQUIRE(BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::string("pl"), manifest, error));
    OBL_CHECK_EQ(manifest["language"], "pl");
    OBL_CHECK(!BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::string("fr"), manifest, error));
    OBL_CHECK(error.find("text/fr.json") != std::string::npos);
    OBL_REQUIRE(BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::string("de"), manifest, error));
    OBL_CHECK_EQ(manifest["language"], "de");

    const obl::test::TempPath none("obl_convert_pack_none");
    SamplePack(none.path, {});
    OBL_CHECK(!BuildPackManifest(*obl::convert::OpenDirectorySink(none.path), SampleSource(), std::nullopt, manifest, error));
}

OBL_TEST_CASE("convert.pack_manifest_bad_path")
{
    nlohmann::ordered_json manifest;
    std::string error;
    const obl::test::TempPath tmp("obl_convert_pack_badpath");
    const auto& dir = tmp.path;
    SamplePack(dir, {"pl"});
    WriteFile(dir / "sprites/Gail.json", "{}");
    OBL_CHECK(!BuildPackManifest(*obl::convert::OpenDirectorySink(dir), SampleSource(), std::nullopt, manifest, error));
    OBL_CHECK(error.find("sprites/Gail.json") != std::string::npos);
}

OBL_TEST_CASE("convert.import_reference_ignores_path_spelling")
{
    OBL_CHECK_EQ(obl::convert::ImportReference({{"BeachLife.exe", "aa"}, {"Data/x", "bb"}}),
            obl::convert::ImportReference({{"beachlife.exe", "aa"}, {"data/X", "bb"}}));
    OBL_CHECK(obl::convert::ImportReference({{"a", "aa"}}) != obl::convert::ImportReference({{"a", "ab"}}));
}
