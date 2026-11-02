// The table conversion and the mixing plan of PaletteModifiers (src/game/ui/palette_modifiers.h).

#include "unit_test.h"

#include "game/ui/palette_modifiers.h"
#include "importer/src/writers/png_write.h"
#include "temp_dir.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::palette_modifiers;

namespace {

bool Has(const Plan& plan, int first, int last)
{
    for (int r = 0; r < plan.rangeCount; ++r)
        if (plan.ranges[r].first == first && plan.ranges[r].last == last) return true;
    return false;
}

int Entries(const Plan& plan)
{
    int n = 0;
    for (int r = 0; r < plan.rangeCount; ++r) n += plan.ranges[r].last - plan.ranges[r].first + 1;
    return n;
}

// The converted pack's files in `dir`: the PNG of 12 rows of 64 colours, all `value`, and the metadata that names it. Returns the RGB
// bytes written, so a test can convert them itself.
std::vector<uint8_t> WritePackFiles(const std::filesystem::path& dir, uint8_t value, const std::string& format = "obl.palette_modifiers")
{
    std::filesystem::create_directories(dir);
    const std::vector<uint8_t> rgb(kFileBytes, value);
    const auto png = convert::EncodePng(kTableEntries, kTableCount, 3, rgb);
    std::ofstream(dir / "palette_modifiers.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    const nlohmann::json doc = {
            {"format", format}, {"version", 1}, {"image", "palette_modifiers.png"}, {"colors", kTableEntries}, {"tables", kTableCount}};
    std::ofstream(dir / "palette_modifiers.json") << doc.dump();
    return rgb;
}

std::vector<uint16_t> Loaded(const std::filesystem::path& pack, bool rgb565, bool& ok)
{
    std::vector<uint16_t> tables(kTableCount * kTableEntries);
    std::string error;
    ok = LoadPack(pack, rgb565, tables.data(), error);
    return tables;
}

} // namespace

OBL_TEST_CASE("palette_modifiers: the file conversion drops the low bits of each channel")
{
    std::vector<uint8_t> rgb(kFileBytes, 0);
    rgb[0] = 0xff;
    rgb[1] = 0xff;
    rgb[2] = 0xff;
    rgb[3] = 0x80;
    rgb[4] = 0x40;
    rgb[5] = 0x08;
    rgb[6] = 0x03;
    rgb[7] = 0x03;
    rgb[8] = 0x03; // below every channel's resolution: 0
    std::vector<uint16_t> t565(kTableCount * kTableEntries), t555(t565.size());
    ConvertTables(rgb.data(), true, t565.data());
    ConvertTables(rgb.data(), false, t555.data());
    OBL_CHECK(t565[0] == 0xffff);
    OBL_CHECK(t555[0] == 0x7fff);
    OBL_CHECK(t565[1] == ((0x80 << 8) | ((0x40 & 0xfc) << 3) | 1));
    OBL_CHECK(t555[1] == ((0x80 << 7) | ((0x40 & 0xf8) << 2) | 1));
    OBL_CHECK(t565[2] == 0 && t555[2] == 0);
}

OBL_TEST_CASE("palette_modifiers: no modifier id means no result")
{
    OBL_CHECK(!MakePlan(0, 0, 1.0f, 0.0f, true).valid);
    OBL_CHECK(MakePlan(5, 0, 1.0f, 0.0f, true).valid);
    OBL_CHECK(MakePlan(5, 0, 1.0f, 0.0f, true).rangeCount == 0);
    OBL_CHECK(MakePlan(-3, 0, 1.0f, 0.0f, true).rangeCount == 0);
}

OBL_TEST_CASE("palette_modifiers: table pair and weight follow scale and blend")
{
    // scale <= 1 + epsilon: the first pair; the weight is 1 - scale.
    Plan p = MakePlan(1, 0, 0.25f, 0.0f, false);
    OBL_CHECK(p.lowTable == 0 && p.withTable == 1 && p.weight == 0.75f);
    // scale above 1: the next pair and 1 - (scale - 1).
    p = MakePlan(1, 0, 1.5f, 0.0f, false);
    OBL_CHECK(p.lowTable == 1 && p.withTable == 2 && p.weight == 0.5f);
    // exactly 1 + epsilon is not above the epsilon
    p = MakePlan(2, 0, 1.0f + 1.1920929e-07f, 0.0f, false);
    OBL_CHECK(p.lowTable == 4 && p.withTable == 5);
    // a blend replaces the weight and picks the fourth table of the id
    p = MakePlan(2, 0, 0.5f, 0.25f, false);
    OBL_CHECK(p.lowTable == 4 && p.withTable == 7 && p.weight == 0.75f);
    p = MakePlan(3, 0, 1.5f, 0.5f, false);
    OBL_CHECK(p.lowTable == 9 && p.withTable == 11 && p.weight == 0.5f);
    p = MakePlan(4, 0, 0.5f, 0.0f, false);
    OBL_CHECK(p.lowTable == 8 && p.withTable == 9);
}

OBL_TEST_CASE("palette_modifiers: the mixed entry ranges per id and variant")
{
    Plan p = MakePlan(1, 0, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 3 && Has(p, 1, 24) && Has(p, 40, 47) && Has(p, 32, 39));
    p = MakePlan(1, 1, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 2 && Has(p, 1, 24) && Has(p, 32, 39));
    p = MakePlan(1, 2, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 1 && Has(p, 1, 24));
    OBL_CHECK(MakePlan(1, 3, 1.0f, 0.0f, false).rangeCount == 0);
    OBL_CHECK(MakePlan(1, -1, 1.0f, 0.0f, false).rangeCount == 0);

    p = MakePlan(2, 0, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 4 && Has(p, 48, 55) && Has(p, 56, 63) && Has(p, 32, 39));
    OBL_CHECK(Entries(MakePlan(2, 5, 1.0f, 0.0f, false)) == 24);
    OBL_CHECK(MakePlan(2, 6, 1.0f, 0.0f, false).rangeCount == 0);

    // ids 3 and 4: the last block only with the Ex flag
    p = MakePlan(3, 0, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 3 && !Has(p, 56, 63));
    p = MakePlan(3, 0, 1.0f, 0.0f, true);
    OBL_CHECK(p.rangeCount == 4 && Has(p, 56, 63));
    p = MakePlan(4, 3, 1.0f, 0.0f, false);
    OBL_CHECK(p.rangeCount == 1 && Has(p, 1, 24));
    p = MakePlan(4, 3, 1.0f, 0.0f, true);
    OBL_CHECK(p.rangeCount == 3 && Has(p, 48, 55) && Has(p, 56, 63));
    OBL_CHECK(MakePlan(4, 4, 1.0f, 0.0f, true).rangeCount == 0);
}

OBL_TEST_CASE("palette_modifiers: Apply mixes the planned entries from the planned tables only")
{
    std::vector<uint16_t> tables(kTableCount * kTableEntries);
    for (int t = 0; t < kTableCount; ++t)
        for (int i = 0; i < kTableEntries; ++i) tables[t * kTableEntries + i] = static_cast<uint16_t>(t * 1000 + i);
    std::vector<uint16_t> result(kTableEntries, 0xeeee);
    const Plan plan = MakePlan(1, 2, 1.0f, 0.0f, false);
    Apply(plan, tables.data(), result.data(), [](uint32_t a, uint32_t b, float w) { return a * 10000 + b + static_cast<uint32_t>(w * 0); });
    OBL_CHECK(result[0] == 0xeeee);
    OBL_CHECK(result[25] == 0xeeee);
    OBL_CHECK(result[1] == static_cast<uint16_t>((0u * 1000 + 1) * 10000 + (1000 + 1)));
    OBL_CHECK(result[24] == static_cast<uint16_t>(24u * 10000 + 1024));
}

OBL_TEST_CASE("palette_modifiers: id 2 mixes the body plus the outfit blocks its variant names")
{
    struct Row {
        int variant;
        bool b32, b48, b56;
    };
    const Row rows[] = {{0, true, true, true}, {1, true, false, true}, {2, true, false, false}, {3, false, true, true},
            {4, false, false, true}, {5, false, false, false}};
    for (const Row& row : rows) {
        Plan plan = MakePlan(2, row.variant, 1.0f, 0.0f, false);
        OBL_CHECK(plan.valid);
        OBL_CHECK(Has(plan, 1, 24));
        OBL_CHECK(Has(plan, 32, 39) == row.b32);
        OBL_CHECK(Has(plan, 48, 55) == row.b48);
        OBL_CHECK(Has(plan, 56, 63) == row.b56);
        OBL_CHECK(!Has(plan, 40, 47));
    }
}

OBL_TEST_CASE("palette_modifiers: ids 3 and 4 add the last block only with extra")
{
    for (int id = 3; id <= 4; ++id) {
        for (int variant = 0; variant <= 3; ++variant) {
            Plan without = MakePlan(id, variant, 1.0f, 0.0f, false);
            Plan with = MakePlan(id, variant, 1.0f, 0.0f, true);
            OBL_CHECK(Has(without, 1, 24) && Has(with, 1, 24));
            OBL_CHECK(Entries(with) >= Entries(without));
            OBL_CHECK(Has(with, 56, 63) || Entries(with) == Entries(without));
        }
        // variant 3 is the body alone, plus the outfit blocks only when extra is set
        OBL_CHECK(Entries(MakePlan(id, 3, 1.0f, 0.0f, false)) == 24);
        OBL_CHECK(Has(MakePlan(id, 3, 1.0f, 0.0f, true), 48, 55));
        OBL_CHECK(Has(MakePlan(id, 3, 1.0f, 0.0f, true), 56, 63));
    }
}

OBL_TEST_CASE("palette_modifiers.pack_loads_both_renderer_formats_from_the_converted_files")
{
    test::TempPath game("palette-pack-load");
    const auto pack = game.path / "packs" / "base";
    const auto rgb = WritePackFiles(pack / "sprites", 0x80);
    for (bool rgb565 : {true, false}) {
        std::vector<uint16_t> expected(kTableCount * kTableEntries);
        ConvertTables(rgb.data(), rgb565, expected.data());
        bool ok = false;
        OBL_CHECK(Loaded(pack, rgb565, ok) == expected);
        OBL_CHECK(ok);
    }
}

OBL_TEST_CASE("palette_modifiers.pack_mod_layer_shadows_the_converted_files")
{
    test::TempPath game("palette-pack-mod");
    const auto pack = game.path / "packs" / "base";
    WritePackFiles(pack / "sprites", 0x80);
    const auto mod = WritePackFiles(game.path / "mod" / "sprites", 0x10);
    std::vector<uint16_t> expected(kTableCount * kTableEntries);
    ConvertTables(mod.data(), true, expected.data());
    bool ok = false;
    OBL_CHECK(Loaded(pack, true, ok) == expected);
    OBL_CHECK(ok);
}

OBL_TEST_CASE("palette_modifiers.pack_rejects_missing_and_mismatched_files")
{
    test::TempPath game("palette-pack-reject");
    const auto pack = game.path / "packs" / "base";
    std::string error;
    std::vector<uint16_t> tables(kTableCount * kTableEntries);
    OBL_CHECK(!LoadPack(pack, true, tables.data(), error)); // nothing converted yet
    WritePackFiles(pack / "sprites", 0x80, "obl.image");
    OBL_CHECK(!LoadPack(pack, true, tables.data(), error)); // another format
    std::filesystem::remove(pack / "sprites" / "palette_modifiers.json");
    WritePackFiles(pack / "sprites", 0x80);
    std::filesystem::remove(pack / "sprites" / "palette_modifiers.png");
    OBL_CHECK(!LoadPack(pack, true, tables.data(), error)); // metadata without its image
    OBL_CHECK(!error.empty());
}

OBL_TEST_CASE("data.palette_modifiers_pack_matches_installed_file")
{
    // The converted tables against the installed Data/Sprites/PaletteModifiers.bin, converted the way the loader did before the pack.
    const std::filesystem::path game = OBL_GAME_DIR;
    const auto file = game / "Data" / "Sprites" / "PaletteModifiers.bin";
    if (!std::filesystem::exists(file)) OBL_SKIP("no installed palette modifiers");
    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> rgb(std::istreambuf_iterator<char>(in), {});
    OBL_CHECK_EQ(rgb.size(), kFileBytes);
    for (bool rgb565 : {true, false}) {
        std::vector<uint16_t> expected(kTableCount * kTableEntries);
        ConvertTables(rgb.data(), rgb565, expected.data());
        std::vector<uint16_t> loaded(expected.size());
        std::string error;
        OBL_REQUIRE(LoadPack(game / "packs" / "base", rgb565, loaded.data(), error));
        OBL_CHECK(loaded == expected);
    }
}
