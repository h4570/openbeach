#include "formats/map_pack.h"

#include "core/ascii_case.h"
#include "core/pack_path.h"
#include "core/png_read.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace obl::map_pack {
namespace {

constexpr std::string_view kGridSuffix = "grid.dat";
constexpr std::string_view kBizSuffix = ".biz";

bool IsIdCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// `<packDir>/maps/<id>/<name>` for a grid path. False with `error` for a path that is not a grid.
bool MapFile(const std::filesystem::path& packDir, std::string_view gridPath, std::string_view name, std::filesystem::path& file,
        std::string& error)
{
    const std::string id = MapId(gridPath);
    if (id.empty()) {
        error = std::string(gridPath) + ": not a map grid path";
        return false;
    }
    file = pack_path::Resolve(packDir, std::filesystem::path("maps") / id / name);
    return true;
}

// The bytes of a pack file; `error` names it and carries the hint the game prints when the pack is missing (docs/build/game-data.md,
// "No fallback").
bool ReadPackFile(const std::filesystem::path& file, std::vector<uint8_t>& bytes, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = file.string() + ": cannot open (Import the game data again)";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// The lowercased id before `suffix` in the last part of `path`, "" when the name does not end with `suffix` or the id is not made of
// the characters a content key uses.
std::string IdBefore(std::string_view path, std::string_view suffix)
{
    const auto separator = path.find_last_of("/\\");
    std::string name(separator == std::string_view::npos ? path : path.substr(separator + 1));
    for (char& c : name) c = ascii::Lower(c);
    if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return "";
    name.resize(name.size() - suffix.size());
    for (char c : name)
        if (!IsIdCharacter(c)) return "";
    return name;
}

} // namespace

std::string MapId(std::string_view gridPath)
{
    // The last part of the path, lowercased, must be `<id>grid.dat`.
    return IdBefore(gridPath, kGridSuffix);
}

std::string BizMapId(std::string_view bizPath)
{
    return IdBefore(bizPath, kBizSuffix);
}

bool ReadIslandPlanes(const std::filesystem::path& packDir, std::string_view id, IslandPlanes& planes, std::string& error)
{
    const std::filesystem::path directory = std::filesystem::path("maps") / std::string(id);
    constexpr size_t kPixels = static_cast<size_t>(kIslandSide) * kIslandSide;
    {
        // The colour image is decoded first and released before the depth image, so the peak is one inflated image plus the planes.
        const auto file = pack_path::Resolve(packDir, directory / "color.png");
        std::vector<uint8_t> bytes;
        if (!ReadPackFile(file, bytes, error)) return false;
        png_read::Image image;
        if (!png_read::Decode(bytes, image, error)) {
            error = file.string() + ": " + error;
            return false;
        }
        if (image.width != static_cast<int>(kIslandSide) || image.height != static_cast<int>(kIslandSide) || image.channels != 3 ||
                image.bitDepth != 8) {
            error = file.string() + ": colour is not 4096 x 4096 RGB8 (Import the game data again)";
            return false;
        }
        planes.colour.resize(kPixels);
        for (size_t i = 0; i < kPixels; ++i) {
            const uint8_t* rgb = &image.bytes[i * 3];
            // The converter expands each channel by bit replication, so dropping the low bits gives back the 565 word exactly.
            planes.colour[i] = static_cast<uint16_t>(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
        }
    }
    {
        const auto file = pack_path::Resolve(packDir, directory / "depth.png");
        std::vector<uint8_t> bytes;
        if (!ReadPackFile(file, bytes, error)) return false;
        png_read::Image image;
        if (!png_read::Decode(bytes, image, error)) {
            error = file.string() + ": " + error;
            return false;
        }
        if (image.width != static_cast<int>(kIslandSide) || image.height != static_cast<int>(kIslandSide) || image.channels != 1 ||
                image.bitDepth != 16) {
            error = file.string() + ": depth is not 4096 x 4096 16-bit gray (Import the game data again)";
            return false;
        }
        planes.depth.resize(kPixels);
        for (size_t i = 0; i < kPixels; ++i) planes.depth[i] = static_cast<uint16_t>((image.bytes[i * 2] << 8) | image.bytes[i * 2 + 1]);
    }
    return true;
}

bool ReadTerrainWords(const std::filesystem::path& packDir, std::string_view gridPath, std::vector<uint32_t>& words, std::string& error)
{
    std::filesystem::path file;
    if (!MapFile(packDir, gridPath, "terrain.png", file, error)) return false;
    std::vector<uint8_t> bytes;
    if (!ReadPackFile(file, bytes, error)) return false;
    int width = 0, height = 0;
    std::vector<uint8_t> indices;
    if (!png_read::DecodeSamples(bytes, width, height, indices, error)) {
        error = file.string() + ": " + error;
        return false;
    }
    if (width != static_cast<int>(kGridSide) || height != static_cast<int>(kGridSide)) {
        error = file.string() + ": terrain is not 128 x 128 (Import the game data again)";
        return false;
    }
    words.assign(kGridSide * kGridSide, 0);
    for (uint32_t y = 0; y < kGridSide; ++y)
        for (uint32_t x = 0; x < kGridSide; ++x) {
            const uint8_t index = indices[y * kGridSide + x];
            if (index >= std::size(kTerrainFlags)) {
                error = file.string() + ": terrain index " + std::to_string(index) + " has no legend entry (Import the game data again)";
                return false;
            }
            words[x * kGridSide + y] = kTerrainFlags[index];
        }
    return true;
}

bool ReadEventArea(const std::filesystem::path& packDir, std::string_view gridPath, std::array<int, 4>& area, std::string& error)
{
    std::filesystem::path file;
    if (!MapFile(packDir, gridPath, "map.json", file, error)) return false;
    std::vector<uint8_t> bytes;
    if (!ReadPackFile(file, bytes, error)) return false;
    try {
        const auto doc = nlohmann::json::parse(bytes);
        const auto& bounds = doc.at("event_area");
        if (!bounds.is_array() || bounds.size() != 4) throw std::runtime_error("expected four coordinates");
        std::array<int, 4> result;
        for (size_t i = 0; i < result.size(); ++i) {
            if (!bounds[i].is_number_integer()) throw std::runtime_error("expected integer coordinates");
            if (bounds[i].is_number_unsigned() && bounds[i].get<uint64_t>() > INT32_MAX)
                throw std::runtime_error("coordinate out of range");
            const auto value = bounds[i].get<int64_t>();
            if (value < INT32_MIN || value > INT32_MAX) throw std::runtime_error("coordinate out of range");
            result[i] = static_cast<int>(value);
        }
        if (result[0] > result[2] || result[1] > result[3]) throw std::runtime_error("reversed bounds");
        area = result;
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": invalid event_area: " + failure.what();
        return false;
    }
}

bool HasHeightMap(const std::filesystem::path& packDir, std::string_view gridPath, bool& has, std::string& error)
{
    std::filesystem::path file;
    if (!MapFile(packDir, gridPath, "map.json", file, error)) return false;
    std::vector<uint8_t> bytes;
    if (!ReadPackFile(file, bytes, error)) return false;
    const auto doc = nlohmann::json::parse(std::string(bytes.begin(), bytes.end()), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = file.string() + ": not a map document (Import the game data again)";
        return false;
    }
    has = doc.contains("height");
    return true;
}

bool ReadHeightSamples(const std::filesystem::path& packDir, std::string_view gridPath, HeightSamples& samples, std::string& error)
{
    std::filesystem::path file;
    if (!MapFile(packDir, gridPath, "height.png", file, error)) return false;
    std::vector<uint8_t> bytes;
    if (!ReadPackFile(file, bytes, error)) return false;
    if (!png_read::DecodeSamples(bytes, samples.width, samples.height, samples.samples, error)) {
        error = file.string() + ": " + error;
        return false;
    }
    return true;
}

bool ReadWaterSheet(const std::filesystem::path& packDir, std::string_view waterPath, std::vector<uint8_t>& sheet, std::string& error)
{
    // IdBefore wants the key to be an id; the water key is the digits after "wateranim".
    std::string key = IdBefore(waterPath, ".bin");
    if (key.size() <= 9 || key.compare(0, 9, "wateranim") != 0)
        key.clear();
    else
        key.erase(0, 9);
    if (key.empty()) {
        error = std::string(waterPath) + ": not a water animation path";
        return false;
    }
    const auto file = pack_path::Resolve(packDir, "maps/water/" + key + ".png");
    std::vector<uint8_t> bytes;
    if (!ReadPackFile(file, bytes, error)) return false;
    int width = 0, height = 0;
    if (!png_read::DecodeSamples(bytes, width, height, sheet, error)) {
        error = file.string() + ": " + error;
        return false;
    }
    if (width != 1920 || height != 512) {
        error = file.string() + ": water sheet is not 1920 x 512 (Import the game data again)";
        return false;
    }
    return true;
}

} // namespace obl::map_pack
