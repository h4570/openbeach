#include "unit_test.h"
#include "importer/src/writers/resource_convert.h"
#include "importer/src/source/source.h"
#include "synthetic_exe.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

void P16(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void P32(std::vector<uint8_t>& b, uint32_t v)
{
    P16(b, v & 0xFFFF);
    P16(b, v >> 16);
}

constexpr uint32_t kRsrcRva = 0x1000;

// A PE32 with one section, .rsrc, holding `resources`.
std::vector<uint8_t> MakeExe(const obl::test::SyntheticResources& resources)
{
    obl::test::SyntheticExe exe;
    exe.AddSection(".rsrc", kRsrcRva, obl::test::ResourceSection(resources, kRsrcRva), obl::test::SyntheticExe::kDataFlags);
    return exe.bytes;
}

} // namespace

OBL_TEST_CASE("convert.resources_icon_and_cursor_files")
{
    // Icon group 0x68: one 16x16 image (id 1). Cursor group 101: one 32x32 image (id 2), 4 hotspot bytes first.
    std::vector<uint8_t> iconGroup, cursorGroup;
    P16(iconGroup, 0);
    P16(iconGroup, 1);
    P16(iconGroup, 1);
    iconGroup.insert(iconGroup.end(), {16, 16, 0, 0});
    P16(iconGroup, 1);
    P16(iconGroup, 8);
    P32(iconGroup, 5);
    P16(iconGroup, 1);
    P16(cursorGroup, 0);
    P16(cursorGroup, 2);
    P16(cursorGroup, 1);
    P16(cursorGroup, 32);
    P16(cursorGroup, 64); // doubled: XOR and AND masks
    P16(cursorGroup, 1);
    P16(cursorGroup, 1);
    P32(cursorGroup, 9);
    P16(cursorGroup, 2);
    const std::vector<uint8_t> iconImage = {1, 2, 3, 4, 5};
    const std::vector<uint8_t> cursorImage = {3, 0, 7, 0, 9, 8, 7, 6, 5}; // hotspot (3, 7), then 5 image bytes
    const auto bytes = MakeExe({{1, {{2, cursorImage}}}, {3, {{1, iconImage}}}, {12, {{101, cursorGroup}}}, {14, {{0x68, iconGroup}}}});
    std::string error;
    auto pe = PeFile::Parse(bytes, error);
    OBL_REQUIRE(pe.has_value());

    std::vector<uint8_t> file;
    OBL_REQUIRE(BuildIconFile(*pe, 0x68, false, file, error));
    OBL_CHECK_EQ(file.size(), size_t{6 + 16 + 5});
    OBL_CHECK_EQ(file[2], uint8_t(1)); // type icon
    OBL_CHECK_EQ(file[6], uint8_t(16));
    OBL_CHECK_EQ(file[14], uint8_t(5)); // bytes in resource
    OBL_CHECK_EQ(file[18], uint8_t(22)); // image offset
    OBL_CHECK(std::equal(iconImage.begin(), iconImage.end(), file.begin() + 22));

    OBL_REQUIRE(BuildIconFile(*pe, 101, true, file, error));
    OBL_CHECK_EQ(file.size(), size_t{6 + 16 + 5});
    OBL_CHECK_EQ(file[2], uint8_t(2)); // type cursor
    OBL_CHECK_EQ(file[6], uint8_t(32));
    OBL_CHECK_EQ(file[7], uint8_t(32)); // height without the mask
    OBL_CHECK_EQ(file[10], uint8_t(3)); // hotspot x
    OBL_CHECK_EQ(file[12], uint8_t(7)); // hotspot y
    OBL_CHECK_EQ(file[14], uint8_t(5));
    OBL_CHECK(std::equal(cursorImage.begin() + 4, cursorImage.end(), file.begin() + 22));

    OBL_CHECK(!BuildIconFile(*pe, 102, true, file, error));
    OBL_CHECK(error.find("102") != std::string::npos);
}

OBL_TEST_CASE("convert.resources_write_icon_and_seven_cursors")
{
    // The game icon group and every cursor group, each with one image; the images differ so each file is traceable.
    obl::test::SyntheticResources resources;
    auto group = [](uint16_t imageId, bool cursor) {
        std::vector<uint8_t> g;
        P16(g, 0);
        P16(g, cursor ? 2 : 1);
        P16(g, 1);
        if (cursor) {
            P16(g, 32);
            P16(g, 64);
        } else {
            g.insert(g.end(), {16, 16, 0, 0});
        }
        P16(g, 1);
        P16(g, cursor ? 1 : 8);
        P32(g, cursor ? 6 : 2);
        P16(g, imageId);
        return g;
    };
    resources[14][kGameIconId] = group(1, false);
    resources[3][1] = {0xAA, 0xBB};
    uint16_t imageId = 10;
    for (const uint16_t id : kCursorIds) {
        resources[12][id] = group(imageId, true);
        resources[1][imageId] = {uint8_t(id), 0, 2, 0, uint8_t(imageId), 0xCC}; // hotspot (id, 2), 2 image bytes
        ++imageId;
    }
    MemorySource source;
    source.Add("BEACHLIFE.EXE", MakeExe(resources));
    MemorySink out;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertResources(source, out, files, error));
    OBL_CHECK_EQ(files, 8);
    OBL_CHECK_EQ(out.files().size(), size_t{8});
    const auto icon = out.Read("icon.ico");
    OBL_REQUIRE(icon.has_value());
    OBL_CHECK_EQ(icon->size(), size_t{6 + 16 + 2});
    OBL_CHECK_EQ((*icon)[22], uint8_t(0xAA));
    imageId = 10;
    for (const uint16_t id : kCursorIds) {
        const auto cursor = out.Read("cursor_" + std::to_string(id) + ".cur");
        OBL_REQUIRE(cursor.has_value());
        OBL_CHECK_EQ(cursor->size(), size_t{6 + 16 + 2});
        OBL_CHECK_EQ((*cursor)[10], uint8_t(id)); // hotspot x
        OBL_CHECK_EQ((*cursor)[22], uint8_t(imageId++));
    }

    // A missing cursor group fails the conversion and names the group.
    resources[12].erase(kCursorIds[3]);
    source.Add("BEACHLIFE.EXE", MakeExe(resources));
    MemorySink partial;
    OBL_CHECK(!ConvertResources(source, partial, files, error));
    OBL_CHECK(error.find(std::to_string(kCursorIds[3])) != std::string::npos);
    MemorySource empty;
    OBL_CHECK(!ConvertResources(empty, partial, files, error));
    OBL_CHECK(error.find("BeachLife.exe not found") != std::string::npos);
}

OBL_TEST_CASE("data.resources_installation_writes_icon_and_seven_cursors")
{
    std::string error;
    const auto exe = FindInstalledFile(OBL_GAME_DIR, "BeachLife.exe");
    if (!exe) OBL_SKIP("no installation");
    const obl::test::TempPath output("obl-resource-test-installation");
    int files = 0;
    OBL_REQUIRE(ConvertResources(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(output.path), files, error));
    OBL_CHECK_EQ(files, 8);
    OBL_CHECK(std::filesystem::file_size(output.path / "icon.ico") > 6);
    for (const uint16_t id : kCursorIds)
        OBL_CHECK(std::filesystem::file_size(output.path / ("cursor_" + std::to_string(id) + ".cur")) > 22);
}
