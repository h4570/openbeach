
#include "unit_test.h"

#include "importer/src/readers/exe_tables.h"
#include "importer/src/source/pe_file.h"
#include "synthetic_footprint_exe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using obl::convert::ExeFootprint;
using obl::convert::FindFootprints;
using obl::convert::kFootprintBlockSize;
using obl::convert::kFootprintColumns;
using obl::convert::kFootprintCount;
using obl::convert::kFootprintRows;
using obl::convert::PeFile;

namespace {

constexpr uint32_t kImageBase = obl::test::SyntheticExe::kImageBase;

using obl::test::FootprintExe;
using obl::test::KeyName;
using obl::test::kFootprintMarkedCells;
using obl::test::kFootprintTextRva;
using obl::test::kFootprintTextSize;
using obl::test::PutU32;

// The building keys of the fixture's list: KEY00..KEY47 plus keys without a grid.
std::set<std::string> FixtureKeys()
{
    std::set<std::string> keys = {"NO_GRID", "SEA"};
    for (size_t i = 0; i < kFootprintCount; ++i) keys.insert(KeyName(i));
    return keys;
}

bool Find(const std::vector<uint8_t>& bytes, std::vector<ExeFootprint>& out, std::string& error,
        const std::set<std::string>& keys = FixtureKeys())
{
    auto exe = PeFile::Parse(bytes, error);
    if (!exe) return false;
    return FindFootprints(*exe, keys, out, error);
}

void CheckMatchesFixture(const FootprintExe& fixture, const std::vector<ExeFootprint>& out)
{
    OBL_REQUIRE(out.size() == kFootprintCount);
    for (size_t i = 0; i < kFootprintCount; ++i) {
        OBL_CHECK_EQ(out[i].building, KeyName(i));
        OBL_CHECK_EQ(out[i].address, fixture.gridVa[i]);
        OBL_CHECK_EQ(out[i].width, static_cast<int32_t>(1 + i % 20));
        OBL_CHECK_EQ(out[i].height, static_cast<int32_t>(2 + i % 5));
        OBL_REQUIRE(out[i].cells.size() == size_t{kFootprintColumns} * kFootprintRows);
        for (uint32_t c : kFootprintMarkedCells) OBL_CHECK_EQ(out[i].cells[c], static_cast<int32_t>(i * 1000 + c));
        OBL_CHECK_EQ(out[i].cells[1], 0);
    }
}

} // namespace

OBL_TEST_CASE("convert.pe_file_maps_addresses")
{
    FootprintExe fixture;
    std::string error;
    auto exe = PeFile::Parse(fixture.bytes, error);
    OBL_REQUIRE(exe.has_value());
    OBL_CHECK(error.empty());
    OBL_CHECK_EQ(exe->ImageBase(), kImageBase);
    OBL_REQUIRE(exe->Sections().size() == size_t{2});
    OBL_CHECK_EQ(exe->Sections()[0].name, std::string(".text"));
    OBL_CHECK_EQ(exe->Sections()[1].characteristics, 0xC0000040u);
    OBL_CHECK_EQ(exe->CString(fixture.keyVa[3]).value_or(""), std::string_view("KEY03"));
    OBL_CHECK_EQ(exe->ReadU32(fixture.gridVa[0]).value_or(0), 1u);
    OBL_CHECK_EQ(exe->VaOfFileOffset(fixture.DataOffset(fixture.messageVa)).value_or(0), fixture.messageVa);
    OBL_CHECK(exe->Read(kImageBase + 0x900000, 4).empty()); // outside every section
    OBL_CHECK(exe->Read(kImageBase + kFootprintTextRva + kFootprintTextSize - 2, 4).empty()); // crosses the end of the raw data

    OBL_CHECK(!PeFile::Parse({'M', 'Z'}, error).has_value());
    OBL_CHECK(!error.empty());
    std::vector<uint8_t> truncated(fixture.bytes.begin(), fixture.bytes.begin() + 0x200);
    OBL_CHECK(!PeFile::Parse(truncated, error).has_value()); // the sections' raw data is missing
}

OBL_TEST_CASE("convert.footprints_found_by_content_pattern")
{
    FootprintExe fixture;
    std::vector<ExeFootprint> out;
    std::string error;
    if (!Find(fixture.bytes, out, error)) std::printf("  error: %s\n", error.c_str());
    OBL_CHECK(error.empty());
    CheckMatchesFixture(fixture, out);
}

OBL_TEST_CASE("convert.footprints_independent_of_addresses")
{
    // Verify location independence: CD data sits 0x1000 lower, but any shift must work.
    for (uint32_t shift : {0x1000u, 0x23000u}) {
        FootprintExe fixture(shift);
        std::vector<ExeFootprint> out;
        std::string error;
        OBL_CHECK(Find(fixture.bytes, out, error));
        CheckMatchesFixture(fixture, out);
    }
    FootprintExe dataOnly(0x1000, 0xC0000040); // section flags say nothing about code
    std::vector<ExeFootprint> out;
    std::string error;
    OBL_CHECK(Find(dataOnly.bytes, out, error));
    CheckMatchesFixture(dataOnly, out);
}

OBL_TEST_CASE("convert.footprints_refuse_broken_patterns")
{
    std::vector<ExeFootprint> out;
    std::string error;

    FootprintExe noMessage; // the lookup's message is no anchor
    noMessage.bytes[noMessage.DataOffset(noMessage.messageVa)] = 'X';
    OBL_CHECK(Find(noMessage.bytes, out, error));
    CheckMatchesFixture(noMessage, out);

    FootprintExe shortRun; // a grid with an impossible size ends the run of grids
    PutU32(shortRun.bytes, shortRun.DataOffset(shortRun.gridVa[12]), 0);
    OBL_CHECK(!Find(shortRun.bytes, out, error));
    OBL_CHECK(error.find("run of") != std::string::npos);
    OBL_CHECK(out.empty());

    FootprintExe cellOutside; // a non-zero cell right of the area: not a grid
    PutU32(cellOutside.bytes, cellOutside.DataOffset(cellOutside.gridVa[3]) + 8 + 24 * 4, 1);
    OBL_CHECK(!Find(cellOutside.bytes, out, error));
    OBL_CHECK(error.find("run of") != std::string::npos);

    FootprintExe storedTwice;
    PutU32(storedTwice.bytes, storedTwice.storeOffsets[30] + 7, storedTwice.gridVa[29]);
    OBL_CHECK(!Find(storedTwice.bytes, out, error));
    OBL_CHECK(error.find("stored twice") != std::string::npos);

    FootprintExe missingStore;
    missingStore.bytes[missingStore.storeOffsets[30]] = 0x90;
    OBL_CHECK(!Find(missingStore.bytes, out, error));
    OBL_CHECK(error.find("expected 48") != std::string::npos);

    FootprintExe duplicateKey; // the inlined key 5 and the called key 30 both push KEY04
    PutU32(duplicateKey.bytes, duplicateKey.keyPushOffsets[5] + 1, duplicateKey.keyVa[4]);
    OBL_CHECK(!Find(duplicateKey.bytes, out, error));
    OBL_CHECK(error.find("twice") != std::string::npos);
    PutU32(duplicateKey.bytes, duplicateKey.keyPushOffsets[5] + 1, duplicateKey.keyVa[5]);
    PutU32(duplicateKey.bytes, duplicateKey.keyPushOffsets[30] + 1, duplicateKey.keyVa[4]);
    OBL_CHECK(!Find(duplicateKey.bytes, out, error));

    FootprintExe fixture; // a key missing from the user's list is no building key
    std::set<std::string> keys = FixtureKeys();
    keys.erase(KeyName(40));
    OBL_CHECK(!Find(fixture.bytes, out, error, keys));
    OBL_CHECK(out.empty());
    OBL_CHECK(!Find(fixture.bytes, out, error, {}));
}
