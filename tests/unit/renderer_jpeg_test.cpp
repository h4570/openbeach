#include "unit_test.h"
#include "platform/renderer_jpeg.h"
#include <array>
#include <filesystem>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "../../third_party/stb/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
using namespace obl;

OBL_TEST_CASE("renderer JPEG: RGB555/565 expansion, pitch and invalid inputs")
{
    const uint16_t bits[]{0x7c00, 0x03e0, 0x001f, 0xffff, 0x7fff, 0, 0x4210, 0xffff};
    const auto rgb = renderer_jpeg::ToRgb(bits, 4, 3, 2, true);
    OBL_CHECK(rgb == std::vector<uint8_t>({255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 132, 132, 132}));
    const uint16_t green[]{0x07e0};
    OBL_CHECK(renderer_jpeg::ToRgb(green, 1, 1, 1, false) == std::vector<uint8_t>({0, 255, 0}));
    OBL_CHECK(renderer_jpeg::Encode(nullptr, 1, 1, 1, true, 80).empty());
    OBL_CHECK(renderer_jpeg::Encode(bits, 2, 3, 2, true, 80).empty());
    OBL_CHECK(renderer_jpeg::Encode(bits, 1, 0, 1, true, 80).empty());
    OBL_CHECK(!renderer_jpeg::Write("", bits, 4, 3, 2, true, 80));
}

OBL_TEST_CASE("renderer JPEG: decoder verifies orientation, colors and odd dimensions at both qualities")
{
    for (bool rgb555 : {false, true})
        for (int quality : {80, 100}) {
            constexpr int width = 35, height = 49, pitch = 39;
            std::array<uint16_t, pitch * height> bits{};
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) bits[y * pitch + x] = y < 24 ? (rgb555 ? 0x7c00 : 0xf800) : 0x001f;
            auto jpeg = renderer_jpeg::Encode(bits.data(), pitch, width, height, rgb555, quality);
            OBL_REQUIRE(!jpeg.empty());
            int w = 0, h = 0, channels = 0;
            auto* decoded = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &channels, 3);
            OBL_REQUIRE(decoded != nullptr);
            OBL_CHECK_EQ(w, width);
            OBL_CHECK_EQ(h, height);
            OBL_CHECK_EQ(channels, 3);
            const auto* top = decoded + (8 * width + 8) * 3;
            const auto* bottom = decoded + (40 * width + 8) * 3;
            OBL_CHECK(top[0] > 245 && top[1] < 10 && top[2] < 10);
            OBL_CHECK(bottom[0] < 10 && bottom[1] < 10 && bottom[2] > 245);
            stbi_image_free(decoded);
            const auto path = std::filesystem::current_path() / "obl_unit_renderer.jpg";
            OBL_CHECK(renderer_jpeg::Write(path.string().c_str(), bits.data(), pitch, width, height, rgb555, quality));
            decoded = stbi_load(path.string().c_str(), &w, &h, &channels, 3);
            OBL_REQUIRE(decoded != nullptr);
            OBL_CHECK_EQ(w, width);
            OBL_CHECK_EQ(h, height);
            stbi_image_free(decoded);
            std::filesystem::remove(path);
        }
}

OBL_TEST_CASE("photo album: DOS wildcard, case folding, max suffix and first file")
{
    OBL_CHECK(renderer_jpeg::NextPhotoName({}) == "BeachLife_Snapshot_0000.jpg");
    OBL_CHECK(renderer_jpeg::NextPhotoName({"BeachLife_Snapshot_0003.jpg", "BEACHLIFE_SNAPSHOT_0021.JPG", "BeachLife_Snapshot_099.jpg",
                      "other_9999.jpg", "BeachLife_Snapshot_99999.jpg"}) == "BeachLife_Snapshot_0022.jpg");
    OBL_CHECK(renderer_jpeg::NextPhotoName({"BeachLife_Snapshot_9999.jpg"}) == "BeachLife_Snapshot_10000.jpg");
    OBL_CHECK(renderer_jpeg::NextPhotoName({"BeachLife_Snapshot_abcd.jpg"}) == "BeachLife_Snapshot_0001.jpg");
    OBL_CHECK(renderer_jpeg::NextPhotoName({"BeachLife_Snapshot_099.jpg"}) == "BeachLife_Snapshot_0001.jpg");
    OBL_CHECK(renderer_jpeg::NextPhotoName({"BeachLife_Snapshot_.jpg"}) == "BeachLife_Snapshot_0001.jpg");
}

OBL_TEST_CASE("renderer JPEG: packed BGR writer decodes odd rows and rejects invalid input")
{
    const auto path = std::filesystem::temp_directory_path() / "obl_bgr_test.jpg";
    std::vector<uint8_t> pixels(35 * 49 * 3);
    for (int y = 0; y < 49; ++y)
        for (int x = 0; x < 35; ++x) pixels[(y * 35 + x) * 3 + (y < 24 ? 2 : 0)] = 255;
    OBL_REQUIRE(renderer_jpeg::WriteBgr(path.string().c_str(), pixels.data(), 35, 49, 95));
    int w, h, channels;
    auto* decoded = stbi_load(path.string().c_str(), &w, &h, &channels, 3);
    OBL_REQUIRE(decoded != nullptr);
    OBL_CHECK_EQ(w, 35);
    OBL_CHECK_EQ(h, 49);
    OBL_CHECK(decoded[0] > 240 && decoded[1] < 15 && decoded[2] < 15);
    const auto last = (35 * 48) * 3;
    OBL_CHECK(decoded[last] < 15 && decoded[last + 1] < 15 && decoded[last + 2] > 240);
    stbi_image_free(decoded);
    std::filesystem::remove(path);
    OBL_CHECK(!renderer_jpeg::WriteBgr("", pixels.data(), 35, 49, 95));
    OBL_CHECK(!renderer_jpeg::WriteBgr(path.string().c_str(), nullptr, 35, 49, 95));
    OBL_CHECK(!renderer_jpeg::WriteBgr(path.string().c_str(), pixels.data(), -1, 49, 95));
    OBL_CHECK(!renderer_jpeg::WriteBgr(path.string().c_str(), pixels.data(), 65536, 49, 95));
}
