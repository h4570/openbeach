// The pack path resolver (src/core/pack_path.h): the pack selection (OBL_PACK) and the layer order mod/ > packs/<id>.

#include "unit_test.h"

#include "core/pack_path.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>

using namespace obl;

namespace {

void Touch(const std::filesystem::path& file)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

void SetPackEnv(const char* value)
{
#ifdef _WIN32
    _putenv_s("OBL_PACK", value); // "" removes it
#else
    if (*value)
        setenv("OBL_PACK", value, 1);
    else
        unsetenv("OBL_PACK");
#endif
}

} // namespace

OBL_TEST_CASE("pack_path.mod_layer_shadows_the_converted_pack")
{
    test::TempPath game("pack-path");
    const auto pack = game.path / "packs" / "base";
    Touch(pack / "gui" / "a.json");
    Touch(pack / "gui" / "b.json");
    Touch(game.path / "mod" / "gui" / "b.json");

    OBL_CHECK(pack_path::ModLayer(pack) == game.path / "mod");
    // Only the converted file exists: the pack answers.
    OBL_CHECK(pack_path::Resolve(pack, "gui/a.json") == pack / "gui" / "a.json");
    // Both exist: the mod file shadows.
    OBL_CHECK(pack_path::Resolve(pack, "gui/b.json") == game.path / "mod" / "gui" / "b.json");
    // Neither exists: the pack's path, so the loader names the converted file in its error.
    OBL_CHECK(pack_path::Resolve(pack, "gui/c.json") == pack / "gui" / "c.json");
    // A loader handed `packs/base/gui` (with or without a trailing separator) gets the same answer.
    OBL_CHECK(pack_path::ResolveIn(pack / "gui", "b.json") == game.path / "mod" / "gui" / "b.json");
    OBL_CHECK(pack_path::ResolveIn((pack / "gui").string() + "/", "a.json") == pack / "gui" / "a.json");
    // A directory in the mod layer does not shadow a file.
    std::filesystem::create_directories(game.path / "mod" / "gui" / "a.json");
    OBL_CHECK(pack_path::Resolve(pack, "gui/a.json") == pack / "gui" / "a.json");
}

OBL_TEST_CASE("pack_path.list_stems_union_both_layers_once_and_sorted")
{
    test::TempPath game("pack-path-list");
    const auto pack = game.path / "packs" / "base";
    Touch(pack / "fonts" / "b.json");
    Touch(pack / "fonts" / "c.png"); // another extension
    Touch(pack / "fonts" / "c.json");
    Touch(game.path / "mod" / "fonts" / "b.json"); // a shadowing name is listed once
    Touch(game.path / "mod" / "fonts" / "a.json");
    const std::vector<std::string> expected = {"a", "b", "c"};
    OBL_CHECK(pack_path::ListStems(pack, "fonts", ".json") == expected);
    OBL_CHECK(pack_path::ListStems(pack, "sounds", ".wav").empty()); // no such directory in either layer
}

OBL_TEST_CASE("pack_path.selection_names_a_directory_under_packs")
{
    OBL_CHECK(pack_path::PackDirectory("base") == std::filesystem::path("packs") / "base");
    OBL_CHECK(pack_path::PackDirectory("l0") == std::filesystem::path("packs") / "l0");
    OBL_CHECK(pack_path::PackDirectory("my_pack-2").has_value());
    // Nothing that could leave packs/ or differ by case on another file system.
    for (const char* bad : {"", "..", "a/b", "a\\b", "L0", "a.b", " base"}) OBL_CHECK(!pack_path::PackDirectory(bad).has_value());

    test::TempPath game("pack-select");
    Touch(game.path / "packs" / "base" / "pack.json");
    Touch(game.path / "packs" / "l0" / "pack.json");
    std::string error;

    SetPackEnv("");
    // The default pack is left to its loaders: a game directory without packs still starts (standalone_no_exe).
    OBL_CHECK(pack_path::CheckSelected(game.path / "empty", error));
    OBL_CHECK(pack_path::SelectedId() == "base");
    OBL_CHECK(pack_path::Selected() == std::filesystem::path("packs") / "base");
    OBL_CHECK(pack_path::CheckSelected(game.path, error));

    SetPackEnv("l0");
    OBL_CHECK(pack_path::Selected() == std::filesystem::path("packs") / "l0");
    OBL_CHECK(pack_path::CheckSelected(game.path, error));
    // The mod layer of any pack is the same `mod/`.
    OBL_CHECK(pack_path::ModLayer(game.path / pack_path::Selected()) == game.path / "mod");

    SetPackEnv("missing");
    OBL_CHECK(!pack_path::CheckSelected(game.path, error));
    OBL_CHECK(error.find("pack.json") != std::string::npos);

    SetPackEnv("../base");
    OBL_CHECK(pack_path::Selected().empty());
    OBL_CHECK(!pack_path::CheckSelected(game.path, error));
    OBL_CHECK(error.find("not a pack id") != std::string::npos);
    SetPackEnv("");
}
