
#include "unit_test.h"

#include "importer/src/readers/building_list.h"
#include "importer/src/readers/exe_tables.h"
#include "importer/src/source/pe_file.h"
#include "core/sha256.h"
#include "importer/src/source/source.h"
#include "og_data_dir.h"
#include "synthetic_exe.h"
#include "temp_dir.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

std::span<const uint8_t> Bytes(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

void WriteFile(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::vector<ExeFootprint> FootprintsOf(const std::filesystem::path& exePath, const std::set<std::string>& keys)
{
    std::ifstream in(exePath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto exe = PeFile::Parse(std::move(bytes), error);
    std::vector<ExeFootprint> out;
    if (!exe || !FindFootprints(*exe, keys, out, error)) std::printf("  %s: %s\n", exePath.string().c_str(), error.c_str());
    return out;
}

} // namespace

OBL_TEST_CASE("convert.sha256_fips_vectors")
{
    OBL_CHECK_EQ(obl::sha256::Hex(Bytes("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    OBL_CHECK_EQ(obl::sha256::Hex(Bytes("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    OBL_CHECK_EQ(obl::sha256::Hex(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    const std::string million(1000000, 'a');
    OBL_CHECK_EQ(obl::sha256::Hex(Bytes(million)), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    obl::sha256::Hasher split; // the same digest when fed in uneven pieces
    for (size_t at = 0; at < million.size(); at += 6007) split.Update(Bytes(std::string_view(million).substr(at, 6007)));
    OBL_CHECK_EQ(split.HexDigest(), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

OBL_TEST_CASE("convert.identify_exe_and_data")
{
    const auto supported = SupportedExes();
    OBL_REQUIRE(supported.size() == 4);
    OBL_CHECK(IdentifyExe(supported[0].sha256) == ExeBuild::DigitalOct2002);
    OBL_CHECK(IdentifyExe(supported[1].sha256) == ExeBuild::CdAug2002);
    OBL_CHECK(IdentifyExe(supported[2].sha256) == ExeBuild::CdAug2002);
    OBL_CHECK(IdentifyExe(supported[3].sha256) == ExeBuild::CdAug2002);
    OBL_CHECK(!IdentifyExe(std::string(64, '0')).has_value());
    std::string uppercaseHash(supported[0].sha256);
    for (char& c : uppercaseHash)
        if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
    OBL_CHECK(!IdentifyExe(uppercaseHash).has_value()); // Identification requires lowercase hex.

    OBL_CHECK(IdentifyData(Bytes(std::string_view("2002:08:20 14:19:21\0\4\0\0\0", 24))) == DataRevision::EuAug2002);
    OBL_CHECK(IdentifyData(Bytes(std::string_view("2002:08:06 16:16:02\0", 20))) == DataRevision::UkAug2002);
    OBL_CHECK(!IdentifyData(Bytes(std::string_view("2002:08:06 16:16:03\0", 20))).has_value());
    OBL_CHECK(!IdentifyData(Bytes(std::string_view("2002:08:06 16:16:02", 19))).has_value()); // truncated file
    OBL_CHECK(!IdentifyData(Bytes(std::string_view("2002:08:06 16:16:02X", 20))).has_value()); // no terminator

    OBL_CHECK_EQ(Name(ExeBuild::DigitalOct2002), std::string_view("digital-2002-10"));
    OBL_CHECK_EQ(Name(ExeBuild::CdAug2002), std::string_view("cd-2002-08"));
    OBL_CHECK_EQ(Name(DataRevision::EuAug2002), std::string_view("eu-2002-08"));
    OBL_CHECK_EQ(Name(DataRevision::UkAug2002), std::string_view("uk-2002-08"));
}

OBL_TEST_CASE("convert.identify_source_in_directory")
{
    const obl::test::TempPath tmp("obl_convert_source_test");
    const auto& root = tmp.path;
    // Spelled differently from kSpriteHeaderPath: the lookup ignores ASCII case on every file system.
    WriteFile(root / "DATA" / "sprites" / "blsprite.HDR", std::string_view("2002:08:06 16:16:02\0rest", 24));
    auto header = FindInstalledFile(root, kSpriteHeaderPath);
    OBL_REQUIRE(header.has_value());
    OBL_CHECK_EQ(header->filename().string(), std::string("blsprite.HDR"));
    OBL_CHECK(!FindInstalledFile(root, "Data/Sprites/Missing.hdr").has_value());

    Source source;
    std::string error;
    OBL_CHECK(!IdentifySource(*obl::convert::OpenDirectorySource(root), source, error));
    OBL_CHECK(error.find("BeachLife.exe not found") != std::string::npos);
    WriteFile(root / "beachlife.exe", "not the game");
    OBL_CHECK(!IdentifySource(*obl::convert::OpenDirectorySource(root), source, error)); // unrecognized hash
    OBL_CHECK(error.find("not a supported BeachLife.exe") != std::string::npos);
}

OBL_TEST_CASE("convert.identify_source_with_injected_hashes")
{
    // A synthetic executable stands for a build once a test table knows its hash; the supported table never does.
    obl::test::SyntheticExe image;
    image.AddSection(".text", 0x1000, 0x200, obl::test::SyntheticExe::kCodeFlags);
    const std::string hash = obl::sha256::Hex(image.bytes);
    const KnownExe known[] = {{hash, ExeBuild::CdAug2002}};
    const std::string_view header("2002:08:06 16:16:02\0header", 26);
    MemorySource files;
    files.Add("beachlife.exe", image.bytes);
    files.Add("data/sprites/blsprite.hdr", {header.begin(), header.end()});

    Source source;
    std::string error;
    OBL_CHECK(!IdentifySource(files, source, error));
    OBL_CHECK(error.find("not a supported BeachLife.exe (sha256 " + hash + ")") != std::string::npos);
    OBL_REQUIRE(IdentifySource(files, source, error, known));
    OBL_CHECK(error.empty());
    OBL_CHECK(source.exe == ExeBuild::CdAug2002);
    OBL_CHECK(source.data == DataRevision::UkAug2002);
    OBL_CHECK_EQ(source.sha256.size(), size_t{2});
    OBL_CHECK_EQ(source.sha256["beachlife.exe"], hash); // keyed by the path as the source spells it
    OBL_CHECK_EQ(source.sha256[std::string(kSpriteHeaderPath)], obl::sha256::Hex(Bytes(header)));
    OBL_CHECK(IdentifyExe(hash, known) == ExeBuild::CdAug2002);
    OBL_CHECK(!IdentifyExe(hash).has_value());
    OBL_CHECK(!IdentifyExe(hash, {}).has_value());

    // A known executable with a sprite stamp of no supported data revision is still refused.
    const std::string_view later("2002:08:06 16:16:03\0header", 26);
    files.Add("data/sprites/blsprite.hdr", {later.begin(), later.end()});
    OBL_CHECK(!IdentifySource(files, source, error, known));
    OBL_CHECK(error.ends_with(" build stamp"));
}

OBL_TEST_CASE("convert.identify_every_supported_revision")
{
    // Both builds with both data revisions, in an installation and with the header in a CD's place.
    const std::vector<uint8_t> exe = {'M', 'Z'};
    const std::string hash = obl::sha256::Hex(exe);
    for (const ExeBuild build : {ExeBuild::DigitalOct2002, ExeBuild::CdAug2002})
        for (const auto& [stamp, data] : {std::pair{std::string_view("2002:08:20 14:19:21\0", 20), DataRevision::EuAug2002},
                     std::pair{std::string_view("2002:08:06 16:16:02\0", 20), DataRevision::UkAug2002}}) {
            const KnownExe known[] = {{hash, build}};
            MemorySource files;
            files.Add("BeachLife.exe", exe);
            files.Add("Data/Sprites/BLSprite.hdr", {stamp.begin(), stamp.end()});
            Source source;
            std::string error;
            OBL_REQUIRE(IdentifySource(files, source, error, known));
            OBL_CHECK(source.exe == build);
            OBL_CHECK(source.data == data);
        }
}

OBL_TEST_CASE("convert.identify_source_discovery")
{
    const std::vector<uint8_t> exe = {'M', 'Z'};
    const std::string hash = obl::sha256::Hex(exe);
    const KnownExe known[] = {{hash, ExeBuild::DigitalOct2002}};
    const std::string_view stamp("2002:08:20 14:19:21\0", 20);
    Source source;
    std::string error;

    MemorySource noData;
    noData.Add("BeachLife.exe", exe);
    OBL_CHECK(!IdentifySource(noData, source, error, known));
    OBL_CHECK(error.find("(or a CD's data1.hdr) not found") != std::string::npos);

    // A CD's header that is no cabinet refuses the source instead of passing as a missing file.
    MemorySource badCd;
    badCd.Add("BeachLife.exe", exe);
    badCd.Add("data1.hdr", {'X'});
    OBL_CHECK(!IdentifySource(badCd, source, error, known));
    OBL_CHECK(error.find("not an InstallShield cabinet header") != std::string::npos);

    // Two spellings of one file: the exact one wins, so the choice does not depend on listing order.
    MemorySource spellings;
    spellings.Add("BEACHLIFE.EXE", {'o', 't', 'h', 'e', 'r'});
    spellings.Add("BeachLife.exe", exe);
    spellings.Add("Data/Sprites/BLSprite.hdr", {stamp.begin(), stamp.end()});
    OBL_REQUIRE(IdentifySource(spellings, source, error, known));
    OBL_CHECK(source.data == DataRevision::EuAug2002);
    OBL_CHECK_EQ(source.sha256["BeachLife.exe"], hash);

    // Only another spelling: the file is still judged by its bytes, never by its name.
    MemorySource onlyOther;
    onlyOther.Add("BEACHLIFE.EXE", {'o', 't', 'h', 'e', 'r'});
    onlyOther.Add("Data/Sprites/BLSprite.hdr", {stamp.begin(), stamp.end()});
    OBL_CHECK(!IdentifySource(onlyOther, source, error, known));
    OBL_CHECK(error.find("not a supported BeachLife.exe") != std::string::npos);
}

OBL_TEST_CASE("convert.pe_file_refuses_malformed_images")
{
    obl::test::SyntheticExe image;
    image.AddSection(".text", 0x1000, 0x200, obl::test::SyntheticExe::kCodeFlags);
    const uint32_t pe = image.bytes[0x3C] | image.bytes[0x3D] << 8;
    const auto refused = [](std::vector<uint8_t> bytes) {
        std::string error;
        const bool parsed = PeFile::Parse(std::move(bytes), error).has_value();
        return parsed ? std::string("<parsed>") : error;
    };
    OBL_CHECK(refused(image.bytes) == "<parsed>");
    OBL_CHECK_EQ(refused({}), std::string("not an MZ executable"));
    auto changed = image.bytes;
    changed[0] = 'X';
    OBL_CHECK_EQ(refused(changed), std::string("not an MZ executable"));
    changed = image.bytes;
    obl::test::PutU32(changed, 0x3C, 0xFFFFFFFFu);
    OBL_CHECK_EQ(refused(changed), std::string("no PE header"));
    changed = image.bytes;
    changed[pe] = 'X';
    OBL_CHECK_EQ(refused(changed), std::string("no PE header"));
    changed = image.bytes;
    obl::test::PutU16(changed, pe + 4, 0x8664);
    OBL_CHECK_EQ(refused(changed), std::string("not an i386 image"));
    changed = image.bytes;
    obl::test::PutU16(changed, pe + 24, 0x20B); // PE32+
    OBL_CHECK(refused(changed).starts_with("not a PE32 optional header"));
    changed = image.bytes;
    obl::test::PutU16(changed, pe + 6, 0xFFFF); // more sections than the file holds
    OBL_CHECK(refused(changed).starts_with("not a PE32 optional header"));
    // Every cut of the headers and sections is refused.
    for (size_t size = 0; size < image.bytes.size(); ++size) {
        std::vector<uint8_t> cut(image.bytes.begin(), image.bytes.begin() + static_cast<std::ptrdiff_t>(size));
        OBL_CHECK(refused(std::move(cut)) != "<parsed>");
    }
}

OBL_TEST_CASE("data.convert_source_executables")
{
    auto ogData = obl::test::FindOgData();
    if (!ogData) OBL_SKIP("the installation was not found (set OBL_OGDATA)");
    const std::filesystem::path digital = *ogData / "program-files" / "Beach Life";
    if (!std::filesystem::exists(digital / "BeachLife.exe")) OBL_SKIP("the installation has no BeachLife.exe");

    Source source;
    std::string error;
    if (!IdentifySource(*obl::convert::OpenDirectorySource(digital), source, error)) std::printf("  %s\n", error.c_str());
    OBL_CHECK(source.exe == ExeBuild::DigitalOct2002);
    OBL_CHECK(source.data == DataRevision::EuAug2002);
    OBL_CHECK_EQ(source.sha256.size(), size_t{2});

    // Read building keys from the reference installation; the older UK/US list must supply the same keys.
    BuildingList list;
    OBL_REQUIRE(ReadBuildingList(*obl::convert::OpenDirectorySource(digital), list, error));
    std::set<std::string> keys;
    for (const BlBuilding& building : list.buildings) keys.insert(building.key);
    const std::vector<ExeFootprint> reference = FootprintsOf(digital / "BeachLife.exe", keys);
    OBL_REQUIRE(reference.size() == kFootprintCount);
    // Both CDs must preserve key order, locate grids 0x1000 lower and differ in exactly four grids
    // (docs/architecture/data-formats/converter/index.md).
    const std::set<std::string> expectedChanged = {"NJPEDLOEHIRE", "BAR1", "CASINO", "BEACHDISCO"};
    int checked = 0;
    for (const char* cd : obl::test::kCdCopies) {
        const std::filesystem::path exePath = *ogData / cd / "BeachLife.exe";
        if (!std::filesystem::exists(exePath)) continue;
        ++checked;
        auto hash = obl::sha256::OfFile(exePath);
        OBL_REQUIRE(hash.has_value());
        OBL_CHECK(IdentifyExe(*hash) == ExeBuild::CdAug2002);
        const std::vector<ExeFootprint> found = FootprintsOf(exePath, keys);
        OBL_REQUIRE(found.size() == reference.size());
        std::set<std::string> changed;
        for (size_t i = 0; i < found.size(); ++i) {
            OBL_CHECK_EQ(found[i].building, reference[i].building);
            OBL_CHECK_EQ(found[i].address, reference[i].address - 0x1000u);
            if (found[i].width != reference[i].width || found[i].height != reference[i].height || found[i].cells != reference[i].cells)
                changed.insert(found[i].building);
        }
        OBL_CHECK(changed == expectedChanged);
    }
    if (checked == 0) std::printf("  no second executable found; only the main one was checked\n");
}
