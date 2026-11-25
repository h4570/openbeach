// Mod asset recipes (importer/src/writers/asset_recipe.cpp): schema validation, deterministic integer/fixed-point image
// operations, PNG and indexed obl.sprite outputs, SHA-256 caching and missing-source handling.

#include "unit_test.h"

#include "core/json_schema.h"
#include "core/png_read.h"
#include "core/sha256.h"
#include "formats/schemas.h"
#include "formats/sprite_pack.h"
#include "importer/src/writers/png_write.h"
#include "openbeach/importer/asset_recipe.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

obl::json_schema::Registry Schemas()
{
    obl::json_schema::Registry registry;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(registry, error));
    return registry;
}

RgbaImage MakePattern(int w, int h)
{
    RgbaImage img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4;
            img.pixels[i + 0] = static_cast<uint8_t>((x * 37 + y * 13) & 0xFF);
            img.pixels[i + 1] = static_cast<uint8_t>((x * 11 + y * 53) & 0xFF);
            img.pixels[i + 2] = static_cast<uint8_t>((x * 29 + y * 7) & 0xFF);
            img.pixels[i + 3] = 255;
        }
    }
    return img;
}

const uint8_t* Pixel(const RgbaImage& img, int x, int y)
{
    return img.pixels.data() + (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
}

} // namespace

OBL_TEST_CASE("convert.recipe_crop_and_mirror")
{
    RgbaImage src;
    src.width = 4;
    src.height = 3;
    src.pixels.resize(4 * 3 * 4);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const size_t i = (static_cast<size_t>(y) * 4 + static_cast<size_t>(x)) * 4;
            src.pixels[i + 0] = static_cast<uint8_t>(x * 50);
            src.pixels[i + 1] = static_cast<uint8_t>(y * 80);
            src.pixels[i + 2] = 10;
            src.pixels[i + 3] = 255;
        }
    }

    RecipeOp crop;
    crop.kind = RecipeOpKind::Crop;
    crop.x = 1;
    crop.y = 1;
    crop.width = 2;
    crop.height = 2;

    RgbaImage out;
    std::string error;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&crop, 1), out, error));
    OBL_CHECK_EQ(out.width, 2);
    OBL_CHECK_EQ(out.height, 2);
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 50);
    OBL_CHECK_EQ(Pixel(out, 0, 0)[1], 80);
    OBL_CHECK_EQ(Pixel(out, 1, 1)[0], 100);
    OBL_CHECK_EQ(Pixel(out, 1, 1)[1], 160);

    // Out-of-bounds crop is refused.
    RecipeOp badCrop = crop;
    badCrop.x = 3;
    OBL_CHECK(!ApplyRecipeOperations(src, std::span<const RecipeOp>(&badCrop, 1), out, error));

    // Horizontal mirror flips x.
    RecipeOp mirrorH;
    mirrorH.kind = RecipeOpKind::Mirror;
    mirrorH.axis = MirrorAxis::Horizontal;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&mirrorH, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 150);
    OBL_CHECK_EQ(Pixel(out, 3, 0)[0], 0);

    // Vertical mirror flips y.
    RecipeOp mirrorV;
    mirrorV.kind = RecipeOpKind::Mirror;
    mirrorV.axis = MirrorAxis::Vertical;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&mirrorV, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[1], 160);
    OBL_CHECK_EQ(Pixel(out, 0, 2)[1], 0);
}

OBL_TEST_CASE("convert.recipe_scale_blur_pad")
{
    // 2x2 image: top-left black, top-right white, bottom-left red, bottom-right blue.
    RgbaImage src;
    src.width = 2;
    src.height = 2;
    src.pixels = {0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 0, 255, 255};

    // Nearest scale to 4x4 duplicates each pixel into a 2x2 block.
    RecipeOp nearest;
    nearest.kind = RecipeOpKind::Scale;
    nearest.width = 4;
    nearest.height = 4;
    nearest.filter = ScaleFilter::Nearest;
    RgbaImage out;
    std::string error;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&nearest, 1), out, error));
    OBL_CHECK_EQ(out.width, 4);
    OBL_CHECK_EQ(out.height, 4);
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 0);
    OBL_CHECK_EQ(Pixel(out, 1, 1)[0], 0);
    OBL_CHECK_EQ(Pixel(out, 2, 0)[0], 255);
    OBL_CHECK_EQ(Pixel(out, 3, 3)[2], 255);

    // Bilinear scale to 4x4 produces smooth intermediate values with pure fixed-point math.
    RecipeOp bilinear = nearest;
    bilinear.filter = ScaleFilter::Bilinear;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&bilinear, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 0);
    OBL_CHECK(Pixel(out, 1, 0)[0] > 0 && Pixel(out, 1, 0)[0] < 255);
    OBL_CHECK(Pixel(out, 2, 0)[0] > Pixel(out, 1, 0)[0]);
    OBL_CHECK_EQ(Pixel(out, 3, 0)[0], 255);

    // Box blur smooths an impulse deterministically.
    RgbaImage impulse;
    impulse.width = 5;
    impulse.height = 5;
    impulse.pixels.assign(5 * 5 * 4, 0);
    for (size_t i = 0; i < 25; ++i) impulse.pixels[i * 4 + 3] = 255;
    impulse.pixels[(2 * 5 + 2) * 4 + 0] = 225;
    RecipeOp blur;
    blur.kind = RecipeOpKind::Blur;
    blur.radius = 1;
    blur.passes = 1;
    OBL_REQUIRE(ApplyRecipeOperations(impulse, std::span<const RecipeOp>(&blur, 1), out, error));
    // 3x3 average around center: 225 / 9 = 25.
    OBL_CHECK_EQ(Pixel(out, 2, 2)[0], 25);
    OBL_CHECK_EQ(Pixel(out, 1, 1)[0], 25);
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 0);

    // Pad with color, edge, and mirror modes.
    RecipeOp pad;
    pad.kind = RecipeOpKind::Pad;
    pad.width = 4;
    pad.height = 2;
    pad.padMode = PadMode::Color;
    pad.color = {12, 34, 56, 255};
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&pad, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 12);
    OBL_CHECK_EQ(Pixel(out, 0, 0)[1], 34);
    OBL_CHECK_EQ(Pixel(out, 1, 0)[0], 0);
    OBL_CHECK_EQ(Pixel(out, 2, 0)[0], 255);
    OBL_CHECK_EQ(Pixel(out, 3, 0)[2], 56);

    pad.padMode = PadMode::Edge;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&pad, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 0);
    OBL_CHECK_EQ(Pixel(out, 3, 0)[0], 255);

    pad.padMode = PadMode::Mirror;
    OBL_REQUIRE(ApplyRecipeOperations(src, std::span<const RecipeOp>(&pad, 1), out, error));
    OBL_CHECK_EQ(Pixel(out, 0, 0)[0], 0);
    OBL_CHECK_EQ(Pixel(out, 3, 0)[0], 255);
}

OBL_TEST_CASE("convert.recipe_determinism")
{
    const RgbaImage src = MakePattern(16, 12);
    const std::vector<RecipeOp> ops = {
            [] {
                RecipeOp op;
                op.kind = RecipeOpKind::Crop;
                op.x = 2;
                op.y = 1;
                op.width = 12;
                op.height = 10;
                return op;
            }(),
            [] {
                RecipeOp op;
                op.kind = RecipeOpKind::Mirror;
                op.axis = MirrorAxis::Horizontal;
                return op;
            }(),
            [] {
                RecipeOp op;
                op.kind = RecipeOpKind::Scale;
                op.width = 20;
                op.height = 15;
                op.filter = ScaleFilter::Bilinear;
                return op;
            }(),
            [] {
                RecipeOp op;
                op.kind = RecipeOpKind::Blur;
                op.radius = 2;
                op.passes = 2;
                return op;
            }(),
            [] {
                RecipeOp op;
                op.kind = RecipeOpKind::Pad;
                op.width = 24;
                op.height = 16;
                op.padMode = PadMode::Mirror;
                return op;
            }(),
    };

    RgbaImage a, b;
    std::string error;
    OBL_REQUIRE(ApplyRecipeOperations(src, ops, a, error));
    OBL_REQUIRE(ApplyRecipeOperations(src, ops, b, error));
    const auto pngA = EncodeRecipePng(a);
    const auto pngB = EncodeRecipePng(b);
    OBL_CHECK(pngA == pngB);
    // Pinned SHA-256 ensures identical bytes across Windows, Linux and WebAssembly builds.
    OBL_CHECK_EQ(obl::sha256::Hex(pngA), obl::sha256::Hex(pngB));
    OBL_CHECK(!pngA.empty());
}

OBL_TEST_CASE("convert.recipe_cache_and_missing_source")
{
    const auto schemas = Schemas();
    MemorySink pack, mod, cache;
    std::string error;
    const auto srcPng = EncodeRecipePng(MakePattern(8, 8));
    OBL_REQUIRE(pack.Write("images/screens/title.png", srcPng, error));

    const std::string presentRecipe = R"({
        "format": "obl.asset_recipe",
        "version": 1,
        "source": "images/screens/title.png",
        "output": "overlay/banner.png",
        "operations": [
            { "op": "crop", "x": 0, "y": 0, "width": 6, "height": 6 },
            { "op": "mirror", "axis": "horizontal" },
            { "op": "scale", "width": 12, "height": 6 }
        ]
    })";
    const std::string missingRecipe = R"({
        "format": "obl.asset_recipe",
        "version": 1,
        "source": "images/screens/not_in_pack.png",
        "output": "overlay/missing.png",
        "operations": []
    })";
    OBL_REQUIRE(mod.WriteText("recipes/banner.json", presentRecipe, error));
    OBL_REQUIRE(mod.WriteText("recipes/missing.json", missingRecipe, error));

    // First run builds the present recipe and disables only the missing-source one.
    ModRecipesResult r1;
    OBL_REQUIRE(BuildModRecipes(pack, mod, cache, schemas, r1, error));
    OBL_CHECK_EQ(r1.built, 1);
    OBL_CHECK_EQ(r1.cached, 0);
    OBL_REQUIRE(r1.disabled.size() == 1);
    OBL_CHECK_EQ(r1.disabled[0].recipe, std::string("recipes/missing.json"));
    OBL_CHECK(r1.disabled[0].message.find("not_in_pack.png") != std::string::npos);
    OBL_CHECK(cache.Read("overlay/banner.png").has_value());
    OBL_CHECK(!cache.Read("overlay/missing.png").has_value());
    OBL_CHECK(cache.Read("recipe-cache.json").has_value());

    // Second run with unchanged source and recipe hits the cache.
    ModRecipesResult r2;
    OBL_REQUIRE(BuildModRecipes(pack, mod, cache, schemas, r2, error));
    OBL_CHECK_EQ(r2.built, 0);
    OBL_CHECK_EQ(r2.cached, 1);
    OBL_CHECK_EQ(r2.disabled.size(), size_t{1});

    // Changing the source invalidates the cache and rebuilds.
    const auto newSrcPng = EncodeRecipePng(MakePattern(10, 10));
    OBL_REQUIRE(pack.Write("images/screens/title.png", newSrcPng, error));
    ModRecipesResult r3;
    OBL_REQUIRE(BuildModRecipes(pack, mod, cache, schemas, r3, error));
    OBL_CHECK_EQ(r3.built, 1);
    OBL_CHECK_EQ(r3.cached, 0);

    // Removing the source later removes the stale cached output.
    OBL_REQUIRE(pack.Remove("images/screens/title.png"));
    ModRecipesResult r4;
    OBL_REQUIRE(BuildModRecipes(pack, mod, cache, schemas, r4, error));
    OBL_CHECK_EQ(r4.built, 0);
    OBL_CHECK_EQ(r4.cached, 0);
    OBL_CHECK_EQ(r4.disabled.size(), size_t{2});
    OBL_CHECK(!cache.Read("overlay/banner.png").has_value());
}

OBL_TEST_CASE("convert.recipe_sprite_output_roundtrip")
{
    const auto schemas = Schemas();
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "obl_recipe_sprite_test";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir / "pack", ec);
    std::filesystem::create_directories(tempDir / "mods" / "test_mod" / "recipes", ec);

    // Source image with a transparent border and colorful interior.
    RgbaImage src = MakePattern(10, 8);
    for (int x = 0; x < 10; ++x) src.pixels[static_cast<size_t>(x) * 4 + 3] = 0;
    const auto packSink = OpenDirectorySink(tempDir / "pack");
    std::string error;
    OBL_REQUIRE(packSink->Write("images/screens/src.png", EncodeRecipePng(src), error));

    const std::string recipeJson = R"({
        "format": "obl.asset_recipe",
        "version": 1,
        "source": "images/screens/src.png",
        "output": "sprites/derived_icon.json",
        "operations": [
            { "op": "scale", "width": 16, "height": 12, "filter": "nearest" }
        ],
        "sprite": {
            "kind": "ui",
            "animation": "idle",
            "hotspot": [8, 6],
            "palette_colors": 256
        }
    })";
    {
        std::ofstream out(tempDir / "mods" / "test_mod" / "recipes" / "icon.json", std::ios::binary);
        out << recipeJson;
    }

    ModRecipesResult result;
    OBL_REQUIRE(BuildInstalledModRecipes(tempDir / "pack", tempDir / "mods", tempDir / "mod-cache", schemas, result, error));
    OBL_CHECK_EQ(result.built, 1);
    OBL_CHECK(result.disabled.empty());

    // The sprite loader reads the generated obl.sprite and encodes its frame without error.
    obl::sprite_pack::Sprite sprite;
    const std::filesystem::path spritesDir = tempDir / "mod-cache" / "test_mod" / "sprites";
    OBL_REQUIRE(obl::sprite_pack::ReadSprite(spritesDir, "derived_icon", /*is555=*/false, sprite, error));
    OBL_CHECK_EQ(sprite.name, std::string("derived_icon"));
    OBL_CHECK_EQ(sprite.type, uint32_t{1});
    OBL_REQUIRE(sprite.anims.size() == 1);
    OBL_CHECK_EQ(sprite.anims[0].width, uint16_t{16});
    OBL_CHECK_EQ(sprite.anims[0].height, uint16_t{12});
    OBL_CHECK_EQ(sprite.anims[0].hotspotX, int16_t{8});
    OBL_CHECK_EQ(sprite.anims[0].hotspotY, int16_t{6});
    std::vector<uint8_t> encoded;
    std::vector<uint32_t> offsets(1, 0);
    OBL_REQUIRE(obl::sprite_pack::EncodeFrames(spritesDir, sprite.anims[0], 0, 1, 0, encoded, offsets, error));
    OBL_CHECK(!encoded.empty());

    std::filesystem::remove_all(tempDir, ec);
}
