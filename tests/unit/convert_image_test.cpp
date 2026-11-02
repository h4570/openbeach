#include "game/ui/font_pack.h"
#include "core/code_pages.h"
#include "unit_test.h"
#include "core/json_schema.h"
#include "core/image_decode.h"
#include "importer/src/writers/image_convert.h"
#include "importer/src/writers/png_write.h"
#include "importer/src/writers/font_convert.h"
#include "importer/src/source/source.h"
#include "formats/schemas.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb/stb_image.h"
#include "temp_dir.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

using namespace obl::convert;
namespace {
void Set16(std::vector<uint8_t>& b, size_t at, unsigned v)
{
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}
void Set32(std::vector<uint8_t>& b, size_t at, unsigned v)
{
    Set16(b, at, v);
    Set16(b, at + 2, v >> 16);
}
std::vector<uint8_t> Tga(bool rle = false)
{
    std::vector<uint8_t> b(18);
    b[1] = 1;
    b[2] = rle ? 9 : 1;
    Set16(b, 5, 2);
    b[7] = 24;
    Set16(b, 12, 2);
    Set16(b, 14, 2);
    b[16] = 8;
    b.insert(b.end(), {30, 20, 10, 60, 50, 40});
    if (rle) b.push_back(3);
    b.insert(b.end(), {0, 1, 1, 0});
    return b;
}
std::vector<uint8_t> Tiff(bool packed = false)
{
    // Our own 2 x 2 displacement field; palette is irrelevant to haze.
    const unsigned tags[] = {256, 257, 258, 259, 262, 273, 277, 278, 279};
    const unsigned values[] = {2, 2, 8, packed ? 32773u : 1u, 3, 122, 1, 2, packed ? 5u : 4u};
    std::vector<uint8_t> b(122);
    Set16(b, 0, 0x4949);
    Set16(b, 2, 42);
    Set32(b, 4, 8);
    Set16(b, 8, 9);
    for (int i = 0; i < 9; ++i) {
        const size_t e = 10 + i * 12;
        Set16(b, e, tags[i]);
        Set16(b, e + 2, 4);
        Set32(b, e + 4, 1);
        Set32(b, e + 8, values[i]);
    }
    if (packed) b.push_back(3);
    b.insert(b.end(), {0, 17, 128, 255});
    return b;
}
void Pixels(const std::vector<uint8_t>& png, int channels, int w, int h, const std::vector<uint8_t>& expected)
{
    int width = 0, height = 0, n = 0;
    auto* p = stbi_load_from_memory(png.data(), int(png.size()), &width, &height, &n, channels);
    OBL_REQUIRE(p);
    OBL_CHECK_EQ(width, w);
    OBL_CHECK_EQ(height, h);
    OBL_CHECK_EQ(n, channels);
    OBL_CHECK(std::vector<uint8_t>(p, p + size_t(w) * h * channels) == expected);
    stbi_image_free(p);
}
obl::json_schema::Registry Schemas()
{
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    return schemas;
}
} // namespace
OBL_TEST_CASE("convert.image_palette_modifiers_round_trip")
{
    auto schemas = Schemas();
    std::string error;
    std::map<std::string, std::vector<uint8_t>> files;
    ConvertedFileSink sink = [&](const std::string& path, std::span<const uint8_t> bytes, std::string&) {
        files[path] = {bytes.begin(), bytes.end()};
        return true;
    };
    std::vector<uint8_t> source(2304);
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 37);
    OBL_REQUIRE(ConvertPaletteModifiers(source, schemas, sink, error));
    Pixels(files.at("sprites/palette_modifiers.png"), 3, 64, 12, source);
    auto doc = nlohmann::ordered_json::parse(files.at("sprites/palette_modifiers.json"));
    OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.palette_modifiers", doc, error));
    OBL_CHECK(!doc.contains("tone_groups")); // the toning rule stays in the engine
    files.clear();
    source.pop_back();
    OBL_CHECK(!ConvertPaletteModifiers(source, schemas, sink, error));
    OBL_CHECK(files.empty());
    source.resize(2305);
    OBL_CHECK(!ConvertPaletteModifiers(source, schemas, sink, error));
    source.resize(2304);
    obl::json_schema::Registry empty;
    OBL_CHECK(!ConvertPaletteModifiers(source, empty, sink, error));
    OBL_CHECK(files.empty());
    ConvertedFileSink reject = [](const std::string&, std::span<const uint8_t>, std::string& why) {
        why = "sink failed";
        return false;
    };
    OBL_CHECK(!ConvertPaletteModifiers(source, schemas, reject, error));
    OBL_CHECK_EQ(error, "sink failed");
}
OBL_TEST_CASE("convert.image_tga_palette_cursor_and_rle")
{
    const std::vector<uint8_t> colors = {40, 50, 60, 255, 10, 20, 30, 255, 10, 20, 30, 255, 40, 50, 60, 255};
    auto cursor = colors;
    cursor[7] = cursor[11] = 0;
    for (bool rle : {false, true}) {
        std::vector<uint8_t> png;
        std::string error;
        auto b = Tga(rle);
        OBL_REQUIRE(ConvertAssetImage(b, false, false, png, error));
        Pixels(png, 4, 2, 2, colors);
        OBL_REQUIRE(ConvertAssetImage(b, false, true, png, error));
        Pixels(png, 4, 2, 2, cursor);
        for (size_t n = 0; n < b.size(); ++n) OBL_CHECK(!ConvertAssetImage(std::span(b).first(n), false, false, png, error));
        b.back() = 2;
        OBL_CHECK(!ConvertAssetImage(b, false, false, png, error));
    }
    auto b = Tga(true);
    b[24] = 0x84;
    std::vector<uint8_t> png;
    std::string error;
    OBL_CHECK(!ConvertAssetImage(b, false, false, png, error));
    b = Tga();
    b[2] = 99;
    OBL_CHECK(!ConvertAssetImage(b, false, false, png, error));
}
OBL_TEST_CASE("convert.image_tga_true_color_and_origins")
{
    std::vector<uint8_t> b(18);
    b[2] = 2;
    Set16(b, 12, 2);
    Set16(b, 14, 1);
    b[16] = 24;
    b[17] = 0x30;
    b.insert(b.end(), {3, 2, 1, 6, 5, 4});
    std::vector<uint8_t> png;
    std::string error;
    OBL_REQUIRE(ConvertAssetImage(b, false, false, png, error));
    Pixels(png, 4, 2, 1, {4, 5, 6, 255, 1, 2, 3, 255});
    b.resize(18);
    b[16] = 16;
    b[17] = 0;
    b.insert(b.end(), {0, 124, 224, 3});
    OBL_REQUIRE(ConvertAssetImage(b, false, false, png, error));
    Pixels(png, 4, 2, 1, {255, 0, 0, 255, 0, 255, 0, 255});
    b.resize(18);
    b[16] = 32;
    b.insert(b.end(), {3, 2, 1, 4, 6, 5, 4, 7});
    OBL_REQUIRE(ConvertAssetImage(b, false, false, png, error));
    Pixels(png, 4, 2, 1, {1, 2, 3, 4, 4, 5, 6, 7});
}
OBL_TEST_CASE("convert.image_haze_indices_and_strict_strips")
{
    for (bool packed : {false, true}) {
        auto b = Tiff(packed);
        std::vector<uint8_t> png;
        std::string error;
        OBL_REQUIRE(ConvertAssetImage(b, true, false, png, error));
        Pixels(png, 1, 2, 2, {0, 17, 128, 255});
        for (size_t n = 0; n < b.size(); ++n) OBL_CHECK(!ConvertAssetImage(std::span(b).first(n), true, false, png, error));
        Set32(b, 10 + 3 * 12 + 8, 5);
        OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
        b = Tiff(packed);
        Set32(b, 10 + 8 * 12 + 8, 3);
        OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
    }
    auto b = Tiff(true);
    b[122] = 0x84;
    std::vector<uint8_t> png;
    std::string error;
    OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
}
OBL_TEST_CASE("data.convert_images_of_installation")
{
    auto root = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(root);
    auto run = root->parent_path().parent_path() / "run";
    if (!std::filesystem::exists(run / "Data/cursor.tga")) OBL_SKIP("installation unavailable");
    auto schemas = Schemas();
    std::string error;
    int files = 0;
    const auto out = run / "tmp/converted-images-unit";
    OBL_REQUIRE(ConvertImages(*obl::convert::OpenDirectorySource(run), *obl::convert::OpenDirectorySink(out), schemas, files, error));
    OBL_CHECK(files > 10);
    std::ifstream stream(out / "images/cursor.json");
    nlohmann::ordered_json doc;
    stream >> doc;
    OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.image", doc, error));
    OBL_CHECK(doc["hotspot"] == nlohmann::ordered_json({0, 0}));
}

OBL_TEST_CASE("data.convert_images_match_engine_loaders")
{
    auto schemas = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(schemas);
    const auto run = schemas->parent_path().parent_path() / "run";
    if (!std::filesystem::exists(run / "Data/cursor.tga")) OBL_SKIP("installation unavailable");
    const auto out = run / "tmp/engine-images-unit";
    auto registry = Schemas();
    std::string error;
    int files = 0;
    OBL_REQUIRE(ConvertImages(*obl::convert::OpenDirectorySource(run), *obl::convert::OpenDirectorySink(out), registry, files, error));
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(run / "Data/Screens"))
        if (entry.path().extension() == ".tga") paths.push_back(entry.path());
    paths.push_back(run / "Data/cursor.tga");
    for (int i = 0; i < 6; ++i) paths.push_back(run / ("Data/Haze/wibble_0" + std::to_string(i) + ".tif"));
    for (const auto& path : paths) {
        std::ifstream stream(path, std::ios::binary);
        const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(stream), {});
        obl::image::Image reference, converted;
        OBL_REQUIRE(path.extension() == ".tif" ? obl::image::DecodeTiff(bytes.data(), bytes.size(), reference)
                                               : obl::image::DecodeTga(bytes.data(), bytes.size(), reference));
        const auto pack = obl::image::PackPath(std::filesystem::relative(path, run).string());
        OBL_REQUIRE(!pack.empty());
        OBL_REQUIRE(obl::image::LoadPng(out / std::filesystem::relative(pack, "packs/base"), converted, error));
        OBL_CHECK(obl::image::Equal(reference, converted));
    }
    obl::image::Image missing;
    OBL_CHECK(!obl::image::LoadPng(out / "missing.png", missing, error));
}

OBL_TEST_CASE("image.pack_png_preserves_colors_and_duplicate_indices")
{
    const obl::test::TempPath tmp("obl-image-pack-unit");
    std::filesystem::create_directories(tmp.path);
    auto write = [&](const std::vector<uint8_t>& png) {
        std::ofstream stream(tmp.path / "image.png", std::ios::binary);
        stream.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    };
    std::string error;
    obl::image::Image loaded;
    for (int channels : {1, 3, 4}) {
        std::vector<uint8_t> pixels(size_t(2 * channels));
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i * 31);
        write(EncodePng(2, 1, channels, pixels));
        OBL_REQUIRE(obl::image::LoadPng(tmp.path / "image.png", loaded, error));
        OBL_CHECK_EQ(loaded.bpp, channels * 8);
        OBL_CHECK(loaded.pixels == pixels);
        OBL_CHECK(loaded.palette.empty());
    }
    const std::vector<uint8_t> indices{0, 1}, palette{17, 23, 42, 17, 23, 42};
    write(EncodeIndexedPng(2, 1, indices, palette));
    OBL_REQUIRE(obl::image::LoadPng(tmp.path / "image.png", loaded, error));
    OBL_CHECK(loaded.pixels == indices);
    OBL_CHECK_EQ(loaded.palette.size(), size_t{768});
    OBL_CHECK(std::equal(palette.begin(), palette.end(), loaded.palette.begin()));
    write({137, 80});
    OBL_CHECK(!obl::image::LoadPng(tmp.path / "image.png", loaded, error));
    write(EncodePng(2, 1, 2, std::vector<uint8_t>{0, 0, 1, 255}));
    OBL_CHECK(!obl::image::LoadPng(tmp.path / "image.png", loaded, error));
}

OBL_TEST_CASE("image.pack_paths")
{
    OBL_CHECK(obl::image::PackPath(".\\Data\\Haze\\wibble_05.tif") == "packs/base/images/haze/5.png");
    OBL_CHECK(obl::image::PackPath("DATA/CURSOR.TGA") == "packs/base/images/cursor.png");
    OBL_CHECK(obl::image::PackPath("data/screens/Loading_1.tga") == "packs/base/images/screens/loading_1.png");
    OBL_CHECK(obl::image::PackPath("data/screens/../cursor.tga").empty());
    OBL_CHECK(obl::image::PackPath("data/fonts/font.tiff").empty());
}

OBL_TEST_CASE("data.convert_image_pixels_match_engine_decoders")
{
    auto schemas = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(schemas);
    const auto run = schemas->parent_path().parent_path() / "run";
    if (!std::filesystem::exists(run / "Data/cursor.tga")) OBL_SKIP("installation unavailable");
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(run / "Data/Screens"))
        if (entry.path().extension() == ".tga") paths.push_back(entry.path());
    paths.push_back(run / "Data/cursor.tga");
    for (int i = 0; i < 6; ++i) paths.push_back(run / ("Data/Haze/wibble_0" + std::to_string(i) + ".tif"));
    for (const auto& path : paths) {
        std::ifstream stream(path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
        const bool haze = path.extension() == ".tif", cursor = path.filename() == "cursor.tga";
        obl::image::Image image;
        OBL_REQUIRE(haze ? obl::image::DecodeTiff(bytes.data(), bytes.size(), image)
                         : obl::image::DecodeTga(bytes.data(), bytes.size(), image));
        std::vector<uint8_t> png, expected;
        std::string error;
        OBL_REQUIRE(ConvertAssetImage(bytes, haze, cursor, png, error));
        if (haze)
            expected = image.pixels;
        else
            for (size_t i = 0; i < size_t(image.width) * image.height; ++i) {
                if (image.bpp == 8) {
                    const auto index = image.pixels[i];
                    for (int c = 0; c < 3; ++c) expected.push_back(image.palette[index * 3 + c]);
                    expected.push_back(cursor && index == 0 ? 0 : 255);
                } else {
                    for (int c = 0; c < 3; ++c) expected.push_back(image.pixels[i * (image.bpp / 8) + c]);
                    expected.push_back(image.bpp == 32 ? image.pixels[i * 4 + 3] : 255);
                }
            }
        Pixels(png, haze ? 1 : 4, image.width, image.height, expected);
    }
}

OBL_TEST_CASE("convert.image_repeated_packets_and_multistrip_tiff")
{
    auto b = Tga(true);
    b.resize(24);
    b.insert(b.end(), {0x83, 1});
    std::vector<uint8_t> png;
    std::string error;
    OBL_REQUIRE(ConvertAssetImage(b, false, false, png, error));
    Pixels(png, 4, 2, 2, {40, 50, 60, 255, 40, 50, 60, 255, 40, 50, 60, 255, 40, 50, 60, 255});
    b = Tiff(true);
    b.resize(122);
    b.insert(b.end(), {0xfd, 17});
    Set32(b, 10 + 8 * 12 + 8, 2);
    OBL_REQUIRE(ConvertAssetImage(b, true, false, png, error));
    Pixels(png, 1, 2, 2, {17, 17, 17, 17});
    b = Tiff();
    b.resize(138);
    Set32(b, 10 + 5 * 12 + 4, 2);
    Set32(b, 10 + 5 * 12 + 8, 122);
    Set32(b, 10 + 7 * 12 + 8, 1);
    Set32(b, 10 + 8 * 12 + 4, 2);
    Set32(b, 10 + 8 * 12 + 8, 130);
    Set32(b, 122, 138);
    Set32(b, 126, 140);
    Set32(b, 130, 2);
    Set32(b, 134, 2);
    b.insert(b.end(), {1, 2, 3, 4});
    OBL_REQUIRE(ConvertAssetImage(b, true, false, png, error));
    Pixels(png, 1, 2, 2, {1, 2, 3, 4});
    Set32(b, 134, 1);
    OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
    b = Tiff();
    Set16(b, 10 + 2 * 12, 256);
    OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
    b = Tiff();
    Set16(b, 10 + 2 * 12, 324);
    OBL_CHECK(!ConvertAssetImage(b, true, false, png, error));
}
OBL_TEST_CASE("convert.image_refuses_malformed_files")
{
    const auto refusal = [](const std::vector<uint8_t>& bytes, bool haze) {
        std::vector<uint8_t> png;
        std::string error;
        return ConvertAssetImage(bytes, haze, false, png, error) ? std::string("<converted>") : error;
    };
    using Change = void (*)(std::vector<uint8_t>&);
    const std::pair<Change, const char*> tga[] = {
            {[](auto& b) { b.resize(17); }, "truncated TGA header"},
            {[](auto& b) { b[2] = 3; }, "unsupported TGA layout"},
            {[](auto& b) { b[7] = 16; }, "unsupported TGA layout"},
            {[](auto& b) { b[17] = 0x40; }, "unsupported TGA layout"},
            {[](auto& b) { Set16(b, 12, 0); }, "invalid TGA dimensions"},
            {[](auto& b) { Set16(b, 12, 0xFFFF), Set16(b, 14, 0xFFFF); }, "invalid TGA dimensions"},
            {[](auto& b) { Set16(b, 5, 0); }, "invalid TGA palette"},
            {[](auto& b) { Set16(b, 5, 300); }, "invalid TGA palette"},
            {[](auto& b) { b.pop_back(); }, "invalid TGA run"},
            {[](auto& b) { b[b.size() - 2] = 9; }, "TGA index outside palette"},
    };
    for (const auto& [change, why] : tga) {
        auto bytes = Tga();
        change(bytes);
        OBL_CHECK_EQ(refusal(bytes, false), std::string(why));
    }
    auto rle = Tga(true);
    rle.resize(rle.size() - 5); // the packet byte is gone
    OBL_CHECK_EQ(refusal(rle, false), std::string("truncated TGA packet"));

    const std::pair<Change, const char*> tiff[] = {
            {[](auto& b) { b.resize(7); }, "unsupported TIFF header"},
            {[](auto& b) { b[0] = 'M'; }, "unsupported TIFF header"},
            {[](auto& b) { Set32(b, 4, 0xFFFFFFF0u); }, "truncated TIFF IFD"},
            {[](auto& b) { Set16(b, 8, 0xFFFF); }, "truncated TIFF tags"},
            {[](auto& b) { Set16(b, 10, 322); }, "tiled indexed TIFF is unsupported"},
            {[](auto& b) { Set16(b, 12, 2); }, "invalid TIFF tag"},
            {[](auto& b) { Set16(b, 22, 256); }, "invalid TIFF tag"},
            {[](auto& b) {
                 Set32(b, 14, 100);
                 Set32(b, 18, 0xFFFFFF00u);
             },
                    "truncated TIFF tag values"},
            {[](auto& b) { Set32(b, 18, 0); }, "unsupported indexed TIFF layout"},
            {[](auto& b) { Set32(b, 10 + 2 * 12 + 8, 4); }, "unsupported indexed TIFF layout"},
            {[](auto& b) { Set16(b, 10 + 5 * 12, 300); }, "invalid TIFF strips"},
            {[](auto& b) { Set32(b, 10 + 5 * 12 + 8, 0xFFFFFF00u); }, "truncated TIFF strip"},
            {[](auto& b) { Set32(b, 10 + 8 * 12 + 8, 3); }, "invalid TIFF strip size"},
    };
    for (const auto& [change, why] : tiff) {
        auto bytes = Tiff();
        change(bytes);
        OBL_CHECK_EQ(refusal(bytes, true), std::string(why));
    }
    auto packed = Tiff(true);
    packed[122] = 9; // a literal run past the strip
    OBL_CHECK_EQ(refusal(packed, true), std::string("invalid TIFF PackBits run"));
    packed = Tiff(true);
    packed[122] = 1; // two literals, then the strip ends
    Set32(packed, 10 + 8 * 12 + 8, 3);
    OBL_CHECK_EQ(refusal(packed, true), std::string("short TIFF PackBits strip"));

    // Every cut is refused, and any damaged byte stays within bounds (the sanitizer job runs this too).
    for (const auto& whole : {Tga(), Tga(true)}) {
        for (size_t size = 0; size < whole.size(); ++size)
            OBL_CHECK(refusal({whole.begin(), whole.begin() + std::ptrdiff_t(size)}, false) != "<converted>");
        for (size_t at = 0; at < whole.size(); ++at) {
            auto damaged = whole;
            damaged[at] = 0xFF;
            refusal(damaged, false);
        }
    }
    for (const auto& whole : {Tiff(), Tiff(true)})
        for (size_t at = 0; at < whole.size(); ++at) {
            auto damaged = whole;
            damaged[at] ^= 0xFF;
            refusal(damaged, true);
        }
}

OBL_TEST_CASE("convert.image_installation_paths_and_failures")
{
    // This test directory contains only invented data and the test's output.
    const obl::test::TempPath tmp("obl-convert-image-unit");
    const auto& root = tmp.path;
    auto write = [&](const std::string& name, const std::vector<uint8_t>& b) {
        const auto path = root / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    write("dAtA/sPrItEs/pAlEtTeMoDiFiErS.bIn", std::vector<uint8_t>(2304, 23));
    write("dAtA/CURSOR.TGA", Tga());
    write("dAtA/sCrEeNs/MENU.TGA", Tga());
    for (int i = 0; i < 6; ++i) write("dAtA/hAzE/WIBBLE_0" + std::to_string(i) + ".TIF", Tiff());
    auto schemas = Schemas();
    std::string error;
    int files = 0;
    OBL_REQUIRE(
            ConvertImages(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, files, error));
    OBL_CHECK_EQ(files, 11);
    obl::image::Image reference, loaded;
    const auto source = Tga();
    OBL_REQUIRE(obl::image::DecodeTga(source.data(), source.size(), reference));
    OBL_REQUIRE(obl::image::LoadPng(root / "out/images/cursor.png", loaded, error));
    OBL_CHECK(obl::image::Equal(reference, loaded));
    OBL_REQUIRE(obl::image::LoadPng(root / "out/images/screens/menu.png", loaded, error));
    OBL_CHECK(obl::image::Equal(reference, loaded));
    const auto haze = Tiff();
    OBL_REQUIRE(obl::image::DecodeTiff(haze.data(), haze.size(), reference));
    OBL_REQUIRE(obl::image::LoadPng(root / "out/images/haze/0.png", loaded, error));
    OBL_CHECK(obl::image::Equal(reference, loaded));
    OBL_CHECK(std::filesystem::exists(root / "out/images/screens/menu.png"));
    write("dAtA/sCrEeNs/MeNu.tga", Tga());
    // A case-insensitive filesystem cannot store the two conflicting source names.
    if (std::distance(std::filesystem::directory_iterator(root / "dAtA/sCrEeNs"), std::filesystem::directory_iterator{}) == 2) {
        OBL_CHECK(!ConvertImages(
                *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, files, error));
        OBL_CHECK(error.find("duplicate screen key") != std::string::npos);
    }
    std::filesystem::remove(root / "dAtA/sCrEeNs/MeNu.tga");
    write("dAtA/sCrEeNs/MENU.TGA", Tga());
    write("dAtA/sCrEeNs/bad-name.tga", Tga());
    OBL_CHECK(!ConvertImages(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, files, error));
    std::filesystem::remove(root / "dAtA/sCrEeNs/bad-name.tga");
    std::filesystem::remove(root / "dAtA/hAzE/WIBBLE_05.TIF");
    OBL_CHECK(!ConvertImages(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, files, error));
    OBL_CHECK(error.find("missing installation file") != std::string::npos);
    write("blocked", {1});
    OBL_CHECK(!ConvertImages(
            *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "blocked"), schemas, files, error));
}

OBL_TEST_CASE("convert.font_unicode_effective_kerning_and_pixels")
{
    auto atlas = Tiff();
    Set32(atlas, 42, 4); // BitsPerSample tag value.
    Set32(atlas, 114, 2); // StripByteCounts.
    atlas.resize(124);
    atlas[122] = 0x01;
    atlas[123] = 0x23;
    std::vector<uint8_t> metrics(2056), kerning(65536);
    Set32(metrics, 0, 8);
    Set16(metrics, 8 + 0xa5 * 8 + 4, 2);
    Set16(metrics, 8 + 0xa5 * 8 + 6, 2);
    kerning[65 * 255 + 66] = 255;
    kerning[65 * 256 + 66] = 5;
    std::map<std::string, std::vector<uint8_t>> files;
    auto sink = [&](const std::string& name, std::span<const uint8_t> bytes, std::string&) {
        files[name] = {bytes.begin(), bytes.end()};
        return true;
    };
    auto schemas = Schemas();
    std::string error;
    OBL_REQUIRE(ConvertFont("ExamplePol", metrics, kerning, atlas, schemas, sink, error));
    auto doc = nlohmann::ordered_json::parse(files["fonts/examplepol.json"]);
    OBL_CHECK(doc["glyphs"].contains("0104"));
    OBL_CHECK(!doc["glyphs"].contains("fffd"));
    OBL_CHECK(doc["glyphs"]["0104"] == nlohmann::ordered_json::array({0, 0, 2, 2}));
    OBL_CHECK(doc["kerning"][0] == nlohmann::ordered_json::array({"0041", "0042", -1}));
    Pixels(files["fonts/examplepol.png"], 2, 2, 2, {0, 0, 1, 255, 2, 255, 3, 255});
    const obl::test::TempPath tmp("obl-font-pack");
    auto write = [&](const std::string& name, const std::vector<uint8_t>& bytes) {
        auto path = tmp.path / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    for (const auto& [name, bytes] : files) write(name, bytes);
    obl::font_pack::Font loaded;
    OBL_REQUIRE(obl::font_pack::ReadFont(tmp.path, "ExamplePol", loaded, error));
    OBL_CHECK_EQ(loaded.size, 8u);
    OBL_CHECK_EQ(loaded.glyphs[0xa5].right, 2);
    OBL_CHECK_EQ(loaded.kerning[65 * 255 + 66], -1);
    OBL_CHECK(loaded.pixels == std::vector<uint8_t>({0, 1, 2, 3}));
    OBL_CHECK(!obl::font_pack::ReadFont(tmp.path, "../examplepol", loaded, error));
    OBL_CHECK(!obl::font_pack::ReadFont(tmp.path, "missing", loaded, error));
    write("fonts/examplepol.png", {1, 2, 3});
    OBL_CHECK(!obl::font_pack::ReadFont(tmp.path, "examplepol", loaded, error));
    OBL_CHECK(error.find("examplepol.png") != std::string::npos);
    write("fonts/examplepol.png", files["fonts/examplepol.png"]);
    doc["glyphs"]["0041"] = {0, 0, 3, 2};
    const auto brokenDoc = doc.dump();
    write("fonts/examplepol.json", {brokenDoc.begin(), brokenDoc.end()});
    OBL_CHECK(!obl::font_pack::ReadFont(tmp.path, "examplepol", loaded, error));
    files.clear();
    OBL_REQUIRE(ConvertFont("Example", metrics, kerning, atlas, schemas, sink, error));
    doc = nlohmann::ordered_json::parse(files["fonts/example.json"]);
    OBL_CHECK(doc["glyphs"].contains("00a5"));
    for (size_t n = 0; n < atlas.size(); ++n)
        OBL_CHECK(!ConvertFont("Example", metrics, kerning, std::span(atlas).first(n), schemas, sink, error));
    auto reject = [&](const std::vector<uint8_t>& m, const std::vector<uint8_t>& k, const std::vector<uint8_t>& a) {
        files.clear();
        OBL_CHECK(!ConvertFont("Example", m, k, a, schemas, sink, error));
        OBL_CHECK(files.empty());
    };
    auto broken = metrics;
    Set16(broken, 8 + 4, 3);
    reject(broken, kerning, atlas);
    broken = kerning;
    broken.pop_back();
    reject(metrics, broken, atlas);
    broken = atlas;
    broken[122] = 0xf1;
    reject(metrics, kerning, broken);
    auto failSink = [](const std::string&, std::span<const uint8_t>, std::string& why) {
        why = "injected write failure";
        return false;
    };
    OBL_CHECK(!ConvertFont("Example", metrics, kerning, atlas, schemas, failSink, error));
    obl::json_schema::Registry missingSchemas;
    files.clear();
    OBL_CHECK(!ConvertFont("Example", metrics, kerning, atlas, missingSchemas, sink, error));
    OBL_CHECK(files.empty());
    metrics.pop_back();
    OBL_CHECK(!ConvertFont("Example", metrics, kerning, atlas, schemas, sink, error));
}
OBL_TEST_CASE("data.convert_fonts_match_engine_loaders")
{
    auto dir = FindInstalledFile(OBL_GAME_DIR, "Data/Fonts");
    if (!dir) OBL_SKIP("no installation fonts");
    auto schemas = Schemas();
    std::string error;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(*dir)) {
        auto name = entry.path().filename().string();
        const std::string suffix = "TexCoord.dat";
        if (!name.ends_with(suffix)) continue;
        name.resize(name.size() - suffix.size());
        auto read = [](const std::filesystem::path& p) {
            std::ifstream in(p, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
        };
        auto metrics = read(entry.path()), kerning = read(*FindInstalledFile(OBL_GAME_DIR, "Data/Fonts/" + name + ".dat")),
             atlas = read(*FindInstalledFile(OBL_GAME_DIR, "Data/Fonts/" + name + ".tiff"));
        obl::image::Image image;
        OBL_REQUIRE(obl::image::DecodeTiff(atlas.data(), atlas.size(), image));
        const obl::test::TempPath tmp("obl-font-engine");
        const auto root = tmp.path;
        ConvertedFileSink sink = [&](const std::string& path, std::span<const uint8_t> bytes, std::string&) {
            auto file = root / path;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return bool(out);
        };
        OBL_REQUIRE(ConvertFont(name, metrics, kerning, atlas, schemas, sink, error));
        obl::font_pack::Font loaded;
        OBL_REQUIRE(obl::font_pack::ReadFont(root, name, loaded, error));
        OBL_CHECK_EQ(loaded.size,
                unsigned(metrics[0]) | (unsigned(metrics[1]) << 8) | (unsigned(metrics[2]) << 16) | (unsigned(metrics[3]) << 24));
        OBL_CHECK_EQ(loaded.bold, unsigned(metrics[4]));
        OBL_CHECK_EQ(loaded.width, image.width);
        OBL_CHECK_EQ(loaded.height, image.height);
        OBL_CHECK(loaded.pixels == image.pixels);
        std::string lower = name;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        bool polish = lower.find("pol") != std::string::npos;
        for (unsigned a = 0; a < 256; ++a) {
            if (obl::code_pages::CodePoint(polish, a) == obl::code_pages::kUndefinedCodePoint) continue;
            OBL_CHECK(std::memcmp(&loaded.glyphs[a], metrics.data() + 8 + a * 8, 8) == 0);
            for (unsigned b = 0; b < 256; ++b) {
                if (obl::code_pages::CodePoint(polish, b) == obl::code_pages::kUndefinedCodePoint) continue;
                OBL_CHECK_EQ(loaded.kerning[a * 255 + b], static_cast<int8_t>(kerning[a * 255 + b]));
            }
        }
        ++count;
    }
    OBL_CHECK(count > 0);
}

OBL_TEST_CASE("convert.l0_fonts")
{
    const auto pack = std::filesystem::path(OBL_REPO_DIR) / "data/packs/l0";
    std::ifstream in(pack / "text/en.json");
    const auto texts = nlohmann::ordered_json::parse(in);
    std::string characters = "0123456789:$%+-/.,";
    for (const auto& entry : texts.at("entries")) characters += entry.at(1).get<std::string>();
    for (const char* name : {"guifont7", "guifont8", "guifont8b", "guifont12b", "guifont14b", "photo16"}) {
        obl::font_pack::Font font;
        std::string error;
        OBL_REQUIRE(obl::font_pack::ReadFont(pack, name, font, error));
        for (unsigned char c : characters) {
            OBL_REQUIRE(c < 128); // L0's own English texts have no code-page conversion.
            const auto& g = font.glyphs[c];
            OBL_CHECK(g.right > g.left);
            bool ink = false;
            for (int y = g.top; y < g.bottom; ++y)
                for (int x = g.left; x < g.right; ++x) ink |= font.pixels[y * font.width + x] != 0;
            OBL_CHECK_EQ(ink, c != ' ');
        }
    }
}

OBL_TEST_CASE("convert.font_installation_missing_inputs")
{
    const obl::test::TempPath tmp("obl-font-test");
    const auto& root = tmp.path;
    auto schemas = Schemas();
    std::string error;
    int count = 9;
    OBL_CHECK(
            !ConvertFonts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, count, error));
    OBL_CHECK_EQ(count, 0);
    std::filesystem::create_directories(root / "dAtA" / "fOnTs");
    OBL_CHECK(
            !ConvertFonts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, count, error));
    std::ofstream(root / "dAtA" / "fOnTs" / "ExampleTexCoord.dat").put(0);
    OBL_CHECK(
            !ConvertFonts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(root / "out"), schemas, count, error));
    OBL_CHECK(error.find("companion") != std::string::npos);
}
