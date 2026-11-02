#include "unit_test.h"
#include "core/image_decode.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/source/source.h"
#include "formats/map_pack.h"
#include "core/png_read.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using obl::map_pack::kTerrainFlags;

namespace {

void Put16(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    Put16(out, v >> 16);
    Put16(out, v & 0xffff);
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    // The decoder does not check CRCs, so they are written as zero.
    Put32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    Put32(out, 0);
}

// zlib stream of stored (uncompressed) deflate blocks, the simplest valid IDAT payload.
std::vector<uint8_t> ZlibStored(const std::vector<uint8_t>& raw)
{
    std::vector<uint8_t> out{0x78, 0x01};
    size_t at = 0;
    do {
        const size_t size = std::min<size_t>(raw.size() - at, 0xffff);
        const bool last = at + size == raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<uint8_t>(size));
        out.push_back(static_cast<uint8_t>(size >> 8));
        out.push_back(static_cast<uint8_t>(~size));
        out.push_back(static_cast<uint8_t>(~size >> 8));
        out.insert(out.end(), raw.begin() + at, raw.begin() + at + size);
        at += size;
    } while (at < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    Put32(out, (b << 16) | a);
    return out;
}

std::vector<uint8_t> Header(uint32_t width, uint32_t height, uint8_t depth, uint8_t colorType, uint8_t interlace = 0)
{
    std::vector<uint8_t> ihdr;
    Put32(ihdr, width);
    Put32(ihdr, height);
    ihdr.insert(ihdr.end(), {depth, colorType, 0, 0, interlace});
    return ihdr;
}

// A file of the given header and filtered rows (each row starts with its filter byte).
std::vector<uint8_t> Png(const std::vector<uint8_t>& ihdr, const std::vector<uint8_t>& rows)
{
    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    Chunk(out, "IHDR", ihdr);
    Chunk(out, "IDAT", ZlibStored(rows));
    Chunk(out, "IEND", {});
    return out;
}

int Paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// The rows of a width x height image of `raw` samples, each filtered with the filter of `filter(y)`.
std::vector<uint8_t> FilteredRows(const std::vector<uint8_t>& raw, int width, int height, int (*filter)(int y))
{
    std::vector<uint8_t> rows;
    for (int y = 0; y < height; ++y) {
        const int type = filter(y);
        rows.push_back(static_cast<uint8_t>(type));
        for (int x = 0; x < width; ++x) {
            const int value = raw[size_t(y) * width + x];
            const int left = x ? raw[size_t(y) * width + x - 1] : 0;
            const int above = y ? raw[size_t(y - 1) * width + x] : 0;
            const int upLeft = (x && y) ? raw[size_t(y - 1) * width + x - 1] : 0;
            int predictor = 0;
            switch (type) {
            case 1: predictor = left; break;
            case 2: predictor = above; break;
            case 3: predictor = (left + above) / 2; break;
            case 4: predictor = Paeth(left, above, upLeft); break;
            default: break;
            }
            rows.push_back(static_cast<uint8_t>(value - predictor));
        }
    }
    return rows;
}

// The rows of a width x height image of `raw` bytes, `bpp` per pixel, each filtered with the filter of `filter(y)`: the predictors look
// `bpp` bytes back (the pixel to the left), as the PNG filters do.
std::vector<uint8_t> FilteredBytes(const std::vector<uint8_t>& raw, int width, int height, int bpp, int (*filter)(int y))
{
    const size_t stride = size_t(width) * bpp;
    std::vector<uint8_t> rows;
    for (int y = 0; y < height; ++y) {
        const int type = filter(y);
        rows.push_back(static_cast<uint8_t>(type));
        for (size_t i = 0; i < stride; ++i) {
            const int value = raw[y * stride + i];
            const int left = i >= size_t(bpp) ? raw[y * stride + i - bpp] : 0;
            const int above = y ? raw[(y - 1) * stride + i] : 0;
            const int upLeft = (y && i >= size_t(bpp)) ? raw[(y - 1) * stride + i - bpp] : 0;
            int predictor = 0;
            switch (type) {
            case 1: predictor = left; break;
            case 2: predictor = above; break;
            case 3: predictor = (left + above) / 2; break;
            case 4: predictor = Paeth(left, above, upLeft); break;
            default: break;
            }
            rows.push_back(static_cast<uint8_t>(value - predictor));
        }
    }
    return rows;
}

void WriteFile(const std::filesystem::path& file, const std::vector<uint8_t>& bytes)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The 128 x 128 terrain of `index(x, y)`, as the converter writes it (indexed, filter 0).
std::vector<uint8_t> TerrainPng(int (*index)(int x, int y))
{
    std::vector<uint8_t> rows;
    for (int y = 0; y < 128; ++y) {
        rows.push_back(0);
        for (int x = 0; x < 128; ++x) rows.push_back(static_cast<uint8_t>(index(x, y)));
    }
    return Png(Header(128, 128, 8, 3), rows);
}

int DiagonalIndex(int x, int y)
{
    return (x + y) % 7;
}

} // namespace

OBL_TEST_CASE("map_pack.map_id")
{
    using obl::map_pack::MapId;
    OBL_CHECK_EQ(MapId("data/Scenario/scenario1/scenario1grid.dat"), "scenario1");
    OBL_CHECK_EQ(MapId("data\\Scenario\\Scenario12\\Scenario12GRID.DAT"), "scenario12");
    OBL_CHECK_EQ(MapId("scenario3grid.dat"), "scenario3");
    OBL_CHECK_EQ(MapId("data/Scenario/scenario1/scenario1.biz"), "");
    OBL_CHECK_EQ(MapId("data/Scenario/scenario1/grid.dat"), "");
    OBL_CHECK_EQ(MapId("data/Scenario/scenario1/scenario-1grid.dat"), "");
}

OBL_TEST_CASE("png_read.filters_round_trip")
{
    // Every filter type, in both colour types the converter writes, over rows whose filters differ.
    const int width = 5, height = 6;
    std::vector<uint8_t> raw;
    for (int i = 0; i < width * height; ++i) raw.push_back(static_cast<uint8_t>((i * 37 + (i / 3) * 11) & 0xff));
    const auto filter = [](int y) { return y % 5; };
    for (uint8_t colorType : {uint8_t(0), uint8_t(3)}) {
        const auto file = Png(Header(width, height, 8, colorType), FilteredRows(raw, width, height, filter));
        int w = 0, h = 0;
        std::vector<uint8_t> samples;
        std::string error;
        OBL_REQUIRE(obl::png_read::DecodeSamples(file, w, h, samples, error));
        OBL_CHECK_EQ(w, width);
        OBL_CHECK_EQ(h, height);
        OBL_CHECK(samples == raw);
    }
}

OBL_TEST_CASE("png_read.decodes_rgb_and_gray16_rows_by_bytes_per_pixel")
{
    // The filters look `bpp` bytes back: 3 for RGB8, 2 for gray 16-bit. Each row uses its own filter type.
    const int width = 5, height = 6;
    const auto filter = [](int y) { return y % 5; };
    const auto bytes = [&](int bpp) {
        std::vector<uint8_t> raw;
        for (int i = 0; i < width * height * bpp; ++i) raw.push_back(static_cast<uint8_t>((i * 53 + (i / 4) * 19 + 7) & 0xff));
        return raw;
    };
    const auto rgbRaw = bytes(3), grayRaw = bytes(2);
    const auto rgbFile = Png(Header(width, height, 8, 2), FilteredBytes(rgbRaw, width, height, 3, filter));
    const auto grayFile = Png(Header(width, height, 16, 0), FilteredBytes(grayRaw, width, height, 2, filter));
    std::string error;
    obl::png_read::Image image;
    OBL_REQUIRE(obl::png_read::Decode(rgbFile, image, error));
    OBL_CHECK_EQ(image.channels, 3);
    OBL_CHECK_EQ(image.bitDepth, 8);
    OBL_CHECK(image.bytes == rgbRaw);
    OBL_REQUIRE(obl::png_read::Decode(grayFile, image, error));
    OBL_CHECK_EQ(image.channels, 1);
    OBL_CHECK_EQ(image.bitDepth, 16);
    OBL_CHECK(image.bytes == grayRaw);
}

OBL_TEST_CASE("png_read.samples_reject_rgb_and_16_bit_images")
{
    const std::vector<uint8_t> rows(2 * (2 * 3 + 1), 0);
    int w = 0, h = 0;
    std::vector<uint8_t> samples;
    std::string error;
    OBL_CHECK(!obl::png_read::DecodeSamples(Png(Header(2, 2, 8, 2), rows), w, h, samples, error));
    OBL_CHECK_EQ(error, "PNG is not an 8-bit gray or palette image");
    const std::vector<uint8_t> gray16Rows(2 * (2 * 2 + 1), 0); // two bytes a sample
    OBL_CHECK(!obl::png_read::DecodeSamples(Png(Header(2, 2, 16, 0), gray16Rows), w, h, samples, error));
    OBL_CHECK_EQ(error, "PNG is not an 8-bit gray or palette image");
}

OBL_TEST_CASE("png_read.rejects_unsupported_files")
{
    const std::vector<uint8_t> rows(2 * 3, 0); // a 2 x 2 image with filter 0 rows
    const auto decode = [](const std::vector<uint8_t>& file, std::string& error) {
        int w = 0, h = 0;
        std::vector<uint8_t> samples;
        return obl::png_read::DecodeSamples(file, w, h, samples, error);
    };
    std::string error;

    auto notPng = Png(Header(2, 2, 8, 0), rows);
    notPng[0] = 0;
    OBL_CHECK(!decode(notPng, error));
    OBL_CHECK_EQ(error, "not a PNG file");

    OBL_CHECK(!decode(Png(Header(2, 2, 4, 0), rows), error));
    OBL_CHECK_EQ(error, "unsupported PNG bit depth or colour type");
    OBL_CHECK(!decode(Png(Header(2, 2, 8, 6), std::vector<uint8_t>(18, 0)), error)); // RGBA is not a sample plane
    OBL_CHECK_EQ(error, "PNG is not an 8-bit gray or palette image");
    OBL_CHECK(!decode(Png(Header(2, 2, 8, 0, 1), rows), error));
    OBL_CHECK_EQ(error, "unsupported PNG compression, filter method or interlace");
    OBL_CHECK(!decode(Png(Header(0, 2, 8, 0), rows), error));
    OBL_CHECK_EQ(error, "PNG size out of range");

    auto badFilter = rows;
    badFilter[0] = 5;
    OBL_CHECK(!decode(Png(Header(2, 2, 8, 0), badFilter), error));
    OBL_CHECK_EQ(error, "unrecognized PNG filter");

    auto truncated = Png(Header(2, 2, 8, 0), rows);
    truncated.resize(40);
    OBL_CHECK(!decode(truncated, error));
    OBL_CHECK_EQ(error, "truncated PNG chunk");

    OBL_CHECK(!decode(Png(Header(2, 3, 8, 0), rows), error));
    OBL_CHECK_EQ(error, "PNG image data does not inflate to the image size");
}

OBL_TEST_CASE("map_pack.biz_map_id")
{
    using obl::map_pack::BizMapId;
    OBL_CHECK_EQ(BizMapId("data/Scenario/Scenario1/scenario1.biz"), "scenario1");
    OBL_CHECK_EQ(BizMapId("DATA\\Scenario\\Scenario12\\SCENARIO12.BIZ"), "scenario12");
    OBL_CHECK_EQ(BizMapId("data/Scenario/scenario1/scenario1grid.dat"), "");
    OBL_CHECK_EQ(BizMapId("data/Scenario/scenario1/scenario1.pis"), "");
    OBL_CHECK_EQ(BizMapId("scenario1.biz.bak"), "");
}

OBL_TEST_CASE("map_pack.reads_island_planes_from_pack")
{
    using obl::map_pack::IslandPlanes;
    using obl::map_pack::kIslandSide;
    constexpr uint32_t side = kIslandSide;
    // The 565 word of a pixel and its depth, both functions of the position, so a transposed or shifted plane is a mismatch.
    const auto colour = [](uint32_t x, uint32_t y) { return static_cast<uint16_t>(x * 31 + y * 17 + 5); };
    const auto depth = [](uint32_t x, uint32_t y) { return static_cast<uint16_t>(0x8000 + x * 7 + y * 11); };
    std::vector<uint8_t> rgb(size_t(side) * side * 3);
    std::vector<uint16_t> depths(size_t(side) * side);
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) {
            const size_t i = size_t(y) * side + x;
            const uint16_t word = colour(x, y);
            // The converter's expansion: each channel by bit replication, so the 565 word comes back exactly.
            const uint8_t r = (word >> 11) & 31, g = (word >> 5) & 63, b = word & 31;
            rgb[i * 3] = static_cast<uint8_t>((r << 3) | (r >> 2));
            rgb[i * 3 + 1] = static_cast<uint8_t>((g << 2) | (g >> 4));
            rgb[i * 3 + 2] = static_cast<uint8_t>((b << 3) | (b >> 2));
            depths[i] = depth(x, y);
        }
    const obl::test::TempPath tmp("obl-map-pack-island");
    const auto directory = tmp.path / "maps" / "scenario4";
    WriteFile(directory / "color.png", obl::convert::EncodePng(side, side, 3, rgb));
    WriteFile(directory / "depth.png", obl::convert::EncodeGray16Png(side, side, depths));
    IslandPlanes planes;
    std::string error;
    OBL_REQUIRE(obl::map_pack::ReadIslandPlanes(tmp.path, "scenario4", planes, error));
    OBL_REQUIRE(planes.colour.size() == size_t(side) * side);
    OBL_REQUIRE(planes.depth.size() == size_t(side) * side);
    size_t mismatches = 0;
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) {
            const size_t i = size_t(y) * side + x;
            mismatches += planes.colour[i] != colour(x, y);
            mismatches += planes.depth[i] != depth(x, y);
        }
    OBL_CHECK_EQ(mismatches, 0u);

    // A colour image of another size stops the load and names the file.
    WriteFile(directory / "color.png", obl::convert::EncodePng(2, 2, 3, std::vector<uint8_t>(12, 0)));
    OBL_CHECK(!obl::map_pack::ReadIslandPlanes(tmp.path, "scenario4", planes, error));
    OBL_CHECK(error.find("colour is not 4096 x 4096 RGB8") != std::string::npos);
    OBL_CHECK(!obl::map_pack::ReadIslandPlanes(tmp.path, "scenario5", planes, error)); // no images for this map
    OBL_CHECK(error.find("cannot open") != std::string::npos);
}

OBL_TEST_CASE("map_pack.reads_terrain_words_from_pack")
{
    using obl::map_pack::ReadTerrainWords;
    const obl::test::TempPath tmp("obl-map-pack-words");
    std::string error;
    std::vector<uint32_t> words;

    WriteFile(tmp.path / "maps" / "scenario7" / "terrain.png", TerrainPng(DiagonalIndex));
    OBL_REQUIRE(ReadTerrainWords(tmp.path, "data/Scenario/Scenario7/scenario7grid.dat", words, error));
    OBL_REQUIRE(words.size() == size_t(128 * 128));
    for (int x = 0; x < 128; x += 13)
        for (int y = 0; y < 128; y += 11) OBL_CHECK_EQ(words[size_t(x) * 128 + y], kTerrainFlags[(x + y) % 7]);

    WriteFile(tmp.path / "maps" / "scenario8" / "terrain.png", TerrainPng([](int x, int y) { return x == 3 && y == 4 ? 7 : 0; }));
    OBL_CHECK(!ReadTerrainWords(tmp.path, "data/Scenario/Scenario8/scenario8grid.dat", words, error));
    OBL_CHECK(error.find("terrain index 7 has no legend entry") != std::string::npos);

    WriteFile(tmp.path / "maps" / "scenario9" / "terrain.png", Png(Header(64, 64, 8, 3), std::vector<uint8_t>(64 * 65, 0)));
    OBL_CHECK(!ReadTerrainWords(tmp.path, "data/Scenario/Scenario9/scenario9grid.dat", words, error));
    OBL_CHECK(error.find("terrain is not 128 x 128") != std::string::npos);

    OBL_CHECK(!ReadTerrainWords(tmp.path, "data/Scenario/Scenario10/scenario10grid.dat", words, error));
    OBL_CHECK(error.find("cannot open") != std::string::npos);
    OBL_CHECK(!ReadTerrainWords(tmp.path, "data/Scenario/Scenario10/scenario10.biz", words, error));
    OBL_CHECK(error.find("not a map grid path") != std::string::npos);
}

OBL_TEST_CASE("map_pack.reads_height_samples_from_pack")
{
    using obl::map_pack::HasHeightMap;
    using obl::map_pack::HeightSamples;
    using obl::map_pack::ReadHeightSamples;
    const obl::test::TempPath tmp("obl-map-pack-height");
    std::string error;
    HeightSamples height;

    const auto text = [](std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    bool has = true;

    // The map's metadata says whether it has a height map: no map.json is an error, and a map.json without `height` is flat.
    OBL_CHECK(!HasHeightMap(tmp.path, "data/Scenario/Scenario4/scenario4grid.dat", has, error));
    OBL_CHECK(error.find("map.json") != std::string::npos);
    WriteFile(tmp.path / "maps" / "scenario4" / "map.json", text(R"({"format": "obl.map"})"));
    OBL_REQUIRE(HasHeightMap(tmp.path, "data/Scenario/Scenario4/scenario4grid.dat", has, error));
    OBL_CHECK(!has);

    // Gray levels, row-major, top row first; odd sizes are kept as they are.
    std::vector<uint8_t> gray(5 * 3);
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = static_cast<uint8_t>(i * 17);
    WriteFile(tmp.path / "maps" / "scenario2" / "height.png", Png(Header(5, 3, 8, 0), FilteredRows(gray, 5, 3, [](int) { return 0; })));
    WriteFile(tmp.path / "maps" / "scenario2" / "map.json", text(R"({"format": "obl.map", "height": "height.png"})"));
    OBL_REQUIRE(HasHeightMap(tmp.path, "data/Scenario/Scenario2/scenario2grid.dat", has, error));
    OBL_CHECK(has);
    OBL_REQUIRE(ReadHeightSamples(tmp.path, "data/Scenario/Scenario2/scenario2grid.dat", height, error));
    OBL_CHECK_EQ(height.width, 5);
    OBL_CHECK_EQ(height.height, 3);
    OBL_CHECK(height.samples == gray);

    // The grid path names the map: a height map name is not looked up.
    OBL_CHECK(!HasHeightMap(tmp.path, "data/Scenario/Scenario2/scenario2height.tga", has, error));
    OBL_CHECK(!ReadHeightSamples(tmp.path, "data/Scenario/Scenario2/scenario2.biz", height, error));
    OBL_CHECK(error.find("not a map grid path") != std::string::npos);

    WriteFile(tmp.path / "maps" / "scenario3" / "height.png", std::vector<uint8_t>{'n', 'o', 't'});
    OBL_CHECK(!ReadHeightSamples(tmp.path, "data/Scenario/Scenario3/scenario3grid.dat", height, error));
    OBL_CHECK(error.find("height.png: ") != std::string::npos);

    // A declared height map whose file is missing: the flag is set and the load fails, naming the file (the game stops there).
    WriteFile(tmp.path / "maps" / "scenario6" / "map.json", text(R"({"height": "height.png"})"));
    OBL_REQUIRE(HasHeightMap(tmp.path, "data/Scenario/Scenario6/scenario6grid.dat", has, error));
    OBL_CHECK(has);
    OBL_CHECK(!ReadHeightSamples(tmp.path, "data/Scenario/Scenario6/scenario6grid.dat", height, error));
    OBL_CHECK(error.find("height.png: cannot open") != std::string::npos);
}

OBL_TEST_CASE("data.map_height_matches_installed_tgas")
{
    // A map has a height.png exactly when its installed height TGA exists, and the samples are the TGA's pixels (the 8-bit indices
    // DecodeTga returns, which the tga load routine keeps as the samples).
    const std::filesystem::path packDir = std::filesystem::path(OBL_GAME_DIR) / "packs" / "base";
    if (!obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Scenario")) OBL_SKIP("no installation maps");
    OBL_REQUIRE(std::filesystem::exists(packDir / "maps"));

    int withHeight = 0;
    for (int n = 1; n <= 12; ++n) {
        const std::string folder = "Scenario" + std::to_string(n);
        const std::string stem = "scenario" + std::to_string(n);
        const std::string gridPath = "data/Scenario/" + folder + "/" + stem + "grid.dat";
        const auto installed = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Scenario/" + folder + "/" + stem + "height.tga");
        bool declared = false;
        std::string heightError;
        OBL_REQUIRE(obl::map_pack::HasHeightMap(packDir, gridPath, declared, heightError));
        OBL_CHECK_EQ(declared, installed.has_value());
        if (!installed) continue;

        std::ifstream in(*installed, std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        obl::image::Image expected;
        OBL_REQUIRE(obl::image::DecodeTga(bytes.data(), bytes.size(), expected));
        OBL_CHECK_EQ(expected.bpp, 8);

        obl::map_pack::HeightSamples actual;
        std::string error;
        OBL_REQUIRE(obl::map_pack::ReadHeightSamples(packDir, gridPath, actual, error));
        OBL_CHECK_EQ(actual.width, expected.width);
        OBL_CHECK_EQ(actual.height, expected.height);
        OBL_CHECK(actual.samples == expected.pixels);
        ++withHeight;
    }
    OBL_CHECK_EQ(withHeight, 4);
}

OBL_TEST_CASE("data.map_terrain_matches_installed_grids")
{
    // Every installed scenario grid: the words the pack gives back are the words of the installed file (128 x 128 u32, little endian).
    const std::filesystem::path packDir = std::filesystem::path(OBL_GAME_DIR) / "packs" / "base";
    if (!obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Scenario")) OBL_SKIP("no installation maps");
    OBL_REQUIRE(std::filesystem::exists(packDir / "maps"));

    int checked = 0;
    for (int n = 1; n <= 12; ++n) {
        const std::string folder = "Scenario" + std::to_string(n);
        const std::string name = "scenario" + std::to_string(n) + "grid.dat";
        const auto installed = obl::convert::FindInstalledFile(OBL_GAME_DIR, "Data/Scenario/" + folder + "/" + name);
        OBL_REQUIRE(installed);
        std::ifstream in(*installed, std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        OBL_REQUIRE(bytes.size() == size_t(128 * 128 * 4));
        std::vector<uint32_t> expected(128 * 128);
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = bytes[i * 4] | (uint32_t(bytes[i * 4 + 1]) << 8) | (uint32_t(bytes[i * 4 + 2]) << 16) |
                    (uint32_t(bytes[i * 4 + 3]) << 24);

        std::vector<uint32_t> words;
        std::string error;
        OBL_REQUIRE(obl::map_pack::ReadTerrainWords(packDir, "data/Scenario/" + folder + "/" + name, words, error));
        OBL_CHECK(words == expected);
        ++checked;
    }
    OBL_CHECK_EQ(checked, 12);
}

OBL_TEST_CASE("map_pack.event_area_requires_ordered_integer_bounds")
{
    const obl::test::TempPath tmp("obl-map-event-area");
    const auto file = tmp.path / "maps" / "isle" / "map.json";
    const auto write = [&](const std::string& text) { WriteFile(file, std::vector<uint8_t>(text.begin(), text.end())); };
    std::array<int, 4> area{7, 8, 9, 10};
    std::string error;
    write(R"({"event_area":[-12,34,56,78]})");
    OBL_REQUIRE(obl::map_pack::ReadEventArea(tmp.path, "islegrid.dat", area, error));
    OBL_CHECK_EQ(area[0], -12);
    OBL_CHECK_EQ(area[3], 78);
    for (const char* text : {R"({})", R"({"event_area":[1,2,3]})", R"({"event_area":[5,2,3,4]})", R"({"event_area":[1,5,3,4]})",
                 R"({"event_area":[1,2,3.5,4]})", R"({"event_area":[1,2,2147483648,4]})",
                 R"({"event_area":[1,2,18446744073709551615,4]})"}) {
        write(text);
        OBL_CHECK(!obl::map_pack::ReadEventArea(tmp.path, "islegrid.dat", area, error));
        OBL_CHECK_EQ(area[0], -12);
        OBL_CHECK(error.find("event_area") != std::string::npos);
    }
}
