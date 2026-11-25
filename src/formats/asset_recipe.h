#pragma once

// Mod asset recipes (docs/modding/reference/recipes.md): deterministic integer/fixed-point image pipelines that derive
// mod assets from logical PNGs of the player's converted pack. Outputs are cached next to the mod (mod-cache/<id>/)
// keyed by recipe and source SHA-256 so each recipe re-runs only when its source or definition changes.

#include "formats/pack_sink.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

// Canonical 8-bit RGBA image, top row first, 4 bytes per pixel.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

enum class RecipeOpKind { Crop, Mirror, Scale, Blur, Pad };
enum class MirrorAxis { Horizontal, Vertical };
enum class ScaleFilter { Bilinear, Nearest };
enum class PadMode { Color, Edge, Mirror };

struct RecipeOp {
    RecipeOpKind kind = RecipeOpKind::Crop;
    int x = 0;
    int y = 0;
    bool hasOffset = false; // for Pad: false means center the image
    int width = 0;
    int height = 0;
    MirrorAxis axis = MirrorAxis::Horizontal;
    ScaleFilter filter = ScaleFilter::Bilinear;
    int radius = 1;
    int passes = 1;
    PadMode padMode = PadMode::Color;
    std::array<uint8_t, 4> color = {0, 0, 0, 255};
};

struct RecipeSpriteOptions {
    std::string kind = "ui";
    std::string animation = "idle";
    int hotspotX = 0;
    int hotspotY = 0;
    int paletteColors = 256;
    bool hasPaletteColors = false;
};

struct AssetRecipe {
    std::string source; // logical pack path (.png)
    std::string output; // logical mod path (.png or sprites/<key>.json)
    std::vector<RecipeOp> operations;
    RecipeSpriteOptions sprite;
    bool hasSpriteOptions = false;
};

struct DisabledRecipe {
    std::string recipe; // relative path in the mod (e.g. recipes/banner.json)
    std::string source;
    std::string output;
    std::string message;
};

struct ModRecipesResult {
    int built = 0;
    int cached = 0;
    std::vector<DisabledRecipe> disabled;
};

// Maximum image dimension a recipe may read or produce (keeps memory bounded on wasm32 as well).
inline constexpr int kMaxRecipeImageSide = 4096;

// Parses and validates one `obl.asset_recipe` JSON document against `schemas`.
bool ParseAssetRecipe(std::string_view text, const json_schema::Registry& schemas, AssetRecipe& recipe, std::string& error);

// Applies `operations` in order using integer/fixed-point math only (bit-identical across Windows, Linux and WebAssembly).
bool ApplyRecipeOperations(const RgbaImage& input, std::span<const RecipeOp> operations, RgbaImage& output, std::string& error);

// Encodes `image` as a deterministic PNG (RGB when every pixel is opaque, RGBA otherwise).
std::vector<uint8_t> EncodeRecipePng(const RgbaImage& image);

// Encodes `image` as an indexed `obl.sprite` (JSON, palette PNG, gray+alpha sheet PNG), validated against `schemas`.
// `outputJsonPath` must be `sprites/<key>.json`; returns the relative paths and bytes to write.
bool EncodeRecipeSprite(const RgbaImage& image, std::string_view outputJsonPath, const RecipeSpriteOptions& options,
        const json_schema::Registry& schemas, std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, std::string& error);

// Runs all `recipes/*.json` of one mod against `pack`, caching outputs and `recipe-cache.json` in `cache`.
// A missing source disables only that recipe's output(s) and records a message in `result.disabled`; a malformed recipe
// or I/O error returns false with `error`. With `errors`, continue across recipes and report each failure.
bool BuildModRecipes(const PackSink& pack, const PackSink& mod, PackSink& cache, const json_schema::Registry& schemas,
        ModRecipesResult& result, std::string& error, std::vector<std::string>* errors = nullptr);

// Desktop convenience: runs recipes for every installed mod under `modsDir` (or only `onlyModId` when non-empty)
// against `packDir`, writing cached outputs to `<cacheRoot>/<mod id>/`.
bool BuildInstalledModRecipes(const std::filesystem::path& packDir, const std::filesystem::path& modsDir,
        const std::filesystem::path& cacheRoot, const json_schema::Registry& schemas, ModRecipesResult& result, std::string& error,
        std::string_view onlyModId = {});

} // namespace obl::convert
