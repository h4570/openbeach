// Parallel conversion (importer/src/writers/ordered_pool.h and its use by sprite_convert.cpp and map_convert.cpp): the worker
// pool commits in item order and bounds its memory, and the converters give the same files, the same first error and the same
// progress with one job and with four. The same holds when the units are shared between shards that only meet through a
// UnitExchange (openbeach/importer/shard.h), as the browser's workers do. Every input is synthetic.

#include "unit_test.h"

#include "core/json_schema.h"
#include "formats/map_pack.h"
#include "formats/schemas.h"
#include "importer/src/writers/map_convert.h"
#include "openbeach/importer/shard.h"
#include "importer/src/writers/ordered_pool.h"
#include "importer/src/writers/sprite_convert.h"
#include "synthetic_map.h"
#include "synthetic_sprites.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace obl::convert;

namespace {

using obl::test::SpriteBytes;

// Consumes 0..count-1 in order whatever the order the items finish in (early items are the slow ones).
OBL_TEST_CASE("convert.ordered_pool_commits_in_item_order")
{
    constexpr size_t kCount = 40;
    for (int jobs : {1, 4, 16}) {
        std::vector<size_t> order;
        const bool ok = RunOrdered<size_t>(
                kCount, jobs, 1 << 20, [](size_t) { return uint64_t{1}; },
                [](size_t i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kCount - i));
                    return i * 3;
                },
                [&](size_t i, size_t& result) {
                    OBL_CHECK_EQ(result, i * 3);
                    order.push_back(i);
                    return true;
                });
        OBL_CHECK(ok);
        OBL_REQUIRE(order.size() == kCount);
        for (size_t i = 0; i < kCount; ++i) OBL_CHECK_EQ(order[i], i);
    }
}

OBL_TEST_CASE("convert.ordered_pool_stops_at_the_first_refusal_and_rethrows")
{
    for (int jobs : {1, 4}) {
        size_t consumed = 0;
        const bool ok = RunOrdered<int>(
                20, jobs, 1 << 20, [](size_t) { return uint64_t{1}; }, [](size_t i) { return static_cast<int>(i); },
                [&](size_t i, int&) {
                    consumed = i + 1;
                    return i != 6;
                });
        OBL_CHECK(!ok);
        OBL_CHECK_EQ(consumed, size_t{7});

        size_t before = 0;
        bool thrown = false;
        try {
            RunOrdered<int>(
                    20, jobs, 1 << 20, [](size_t) { return uint64_t{1}; },
                    [](size_t i) -> int {
                        if (i == 5) throw std::runtime_error("item 5");
                        return 0;
                    },
                    [&](size_t i, int&) {
                        before = i + 1;
                        return true;
                    });
        } catch (const std::runtime_error& e) {
            thrown = std::string(e.what()) == "item 5";
        }
        OBL_CHECK(thrown);
        OBL_CHECK_EQ(before, size_t{5}); // the items before the failing one were still committed
    }
}

OBL_TEST_CASE("convert.ordered_pool_bounds_the_memory_in_flight")
{
    // Items cost 40 against a budget of 100: at most two run together, a 250 item runs alone.
    std::atomic<uint64_t> running{0}, peak{0};
    const std::function<uint64_t(size_t)> cost = [](size_t i) { return i == 5 ? uint64_t{250} : uint64_t{40}; };
    const bool ok = RunOrdered<int>(
            12, 8, 100, cost,
            [&](size_t i) {
                const uint64_t now = running += cost(i);
                uint64_t seen = peak;
                while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                running -= cost(i);
                return 0;
            },
            [](size_t, int&) { return true; });
    OBL_CHECK(ok);
    OBL_CHECK(peak <= 250);
    OBL_CHECK(peak >= 40);
}

// --- shards ------------------------------------------------------------------------------------------------------------

// The units the shards made, by step and number: what the workers of the browser hand to the one that runs the whole import.
using Units = std::map<std::pair<std::string, size_t>, UnitOutput>;

UnitExchange Taking(const Units& units)
{
    UnitExchange exchange;
    exchange.source = [&units](std::string_view step, size_t unit, UnitOutput& output) {
        const auto it = units.find({std::string(step), unit});
        if (it == units.end()) return false;
        output = it->second;
        return true;
    };
    return exchange;
}

OBL_TEST_CASE("convert.shard_assignment_balances_and_is_stable")
{
    const std::vector<uint64_t> costs = {5, 100, 5, 5, 90, 5, 5, 80, 5, 5, 5, 70};
    for (int count : {1, 2, 3, 5}) {
        const std::vector<int> owner = AssignUnits(costs, count);
        OBL_CHECK(owner == AssignUnits(costs, count));
        std::vector<uint64_t> load(static_cast<size_t>(count), 0);
        for (size_t i = 0; i < costs.size(); ++i) {
            OBL_REQUIRE(owner[i] >= 0 && owner[i] < count);
            load[static_cast<size_t>(owner[i])] += costs[i];
        }
        // No shard carries more than the average plus the largest unit.
        uint64_t sum = 0;
        for (uint64_t value : load) sum += value;
        for (uint64_t value : load) OBL_CHECK(value <= sum / count + 100);
    }
    OBL_CHECK(AssignUnits({}, 3).empty());
}

// --- sprites -----------------------------------------------------------------------------------------------------------

struct SpriteArchiveBytes {
    std::vector<uint8_t> header, data;
};

// `names` sprites of type 1, each with one palette and the animation of Block(); the ones in `brokenBlocks` point past the end
// of the .dat.
SpriteArchiveBytes ManySprites(const std::vector<std::string>& names, const std::vector<size_t>& brokenBlocks = {})
{
    const std::string stamp = "2099:01:01 00:00:00";
    SpriteBytes dat, hdr;
    dat.b.assign(stamp.begin(), stamp.end());
    dat.b.push_back(0);
    dat.U32(4);
    hdr.b.assign(stamp.begin(), stamp.end());
    hdr.b.push_back(0);
    hdr.U32(4).U32(static_cast<uint32_t>(names.size()));
    for (size_t i = 0; i < names.size(); ++i) {
        obl::convert::SpriteAnimRecord anim;
        const auto block = obl::test::Block(anim);
        const uint32_t at = static_cast<uint32_t>(dat.b.size());
        dat.b.insert(dat.b.end(), block.begin(), block.end());
        const bool broken = std::find(brokenBlocks.begin(), brokenBlocks.end(), i) != brokenBlocks.end();
        hdr.Name(names[i]).U32(1).U32(1);
        for (int c = 0; c < 768; ++c) hdr.U8(static_cast<uint32_t>(c * 7 + i * 13));
        hdr.U32(1).Name("image").U16(anim.frames).U8(anim.directions).U16(anim.width).U16(anim.height).U16(0).U16(0);
        hdr.U32(broken ? 0x7ffffff0u : at).U32(anim.size).U16(anim.uniqueFrames);
    }
    return {hdr.b, dat.b};
}

struct SpriteRun {
    bool ok = false;
    std::string error;
    std::map<std::string, std::vector<uint8_t>, std::less<>> files;
    std::vector<std::pair<int, int>> reports;
    int sprites = 0, animations = 0, frames = 0, fileCount = 0;
};

SpriteRun ConvertWithJobs(const SpriteArchiveBytes& archive, int jobs)
{
    MemorySource source;
    source.Add("Data/Sprites/BLSprite.hdr", archive.header);
    source.Add("Data/Sprites/BLSprite.dat", archive.data);
    obl::json_schema::Registry schemas;
    SpriteRun run;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, run.error));
    MemorySink sink;
    SpriteConversionStats stats;
    stats.jobs = jobs;
    stats.report = [&](int done, int total) { run.reports.emplace_back(done, total); };
    run.ok = ConvertSprites(source, sink, schemas, "", stats, run.error);
    run.files = sink.files();
    run.sprites = stats.sprites;
    run.animations = stats.animations;
    run.frames = stats.frames;
    run.fileCount = stats.files;
    return run;
}

void CheckSameSprites(const SpriteRun& serial, const SpriteRun& parallel)
{
    OBL_CHECK_EQ(parallel.ok, serial.ok);
    OBL_CHECK_EQ(parallel.error, serial.error);
    OBL_CHECK(parallel.files == serial.files);
    OBL_CHECK(parallel.reports == serial.reports);
    OBL_CHECK_EQ(parallel.sprites, serial.sprites);
    OBL_CHECK_EQ(parallel.animations, serial.animations);
    OBL_CHECK_EQ(parallel.frames, serial.frames);
    OBL_CHECK_EQ(parallel.fileCount, serial.fileCount);
}

// The sprites converted by `shards` instances that share nothing, then by one that takes their units over.
SpriteRun ConvertSharded(const SpriteArchiveBytes& archive, int shards)
{
    MemorySource source;
    source.Add("Data/Sprites/BLSprite.hdr", archive.header);
    source.Add("Data/Sprites/BLSprite.dat", archive.data);
    obl::json_schema::Registry schemas;
    SpriteRun run;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, run.error));
    Units units;
    for (int index = 0; index < shards; ++index) {
        const Shard shard{index, shards};
        UnitExchange exchange;
        exchange.shard = &shard;
        exchange.sink = [&units](std::string_view step, size_t unit, UnitOutput& output) {
            units[{std::string(step), unit}] = std::move(output);
        };
        MemorySink untouched;
        SpriteConversionStats stats;
        stats.exchange = &exchange;
        SpriteArchive opened;
        OBL_REQUIRE(SpriteArchive::Open(source, opened, run.error));
        OBL_REQUIRE(opened.Convert(untouched, schemas, "", stats, run.error));
        OBL_CHECK(untouched.files().empty()); // a shard writes nothing, not even the stamp
    }
    const UnitExchange exchange = Taking(units);
    MemorySink sink;
    SpriteConversionStats stats;
    stats.exchange = &exchange;
    stats.report = [&](int done, int total) { run.reports.emplace_back(done, total); };
    run.ok = ConvertSprites(source, sink, schemas, "", stats, run.error);
    run.files = sink.files();
    run.sprites = stats.sprites;
    run.animations = stats.animations;
    run.frames = stats.frames;
    run.fileCount = stats.files;
    return run;
}

std::vector<std::string> SpriteNames(size_t count)
{
    std::vector<std::string> names;
    for (size_t i = 0; i < count; ++i) names.push_back("Sprite_" + std::to_string(100 + i));
    return names;
}

OBL_TEST_CASE("convert.parallel_sprites_equal_serial")
{
    const auto archive = ManySprites(SpriteNames(24));
    const SpriteRun serial = ConvertWithJobs(archive, 1);
    OBL_REQUIRE(serial.ok);
    OBL_CHECK_EQ(serial.sprites, 24);
    OBL_CHECK_EQ(serial.reports.size(), size_t{24});
    for (int jobs : {2, 4, 0}) CheckSameSprites(serial, ConvertWithJobs(archive, jobs));
    // Shared between shards, the sprites, the stamp and the counts are those of one instance.
    OBL_CHECK(serial.files.contains(std::string("sprites/") + kSpriteStampName));
    for (int shards : {1, 2, 3, 5}) CheckSameSprites(serial, ConvertSharded(archive, shards));
}

OBL_TEST_CASE("convert.parallel_sprites_report_the_first_error_of_a_serial_run")
{
    // Two damaged sprites: the first in bank order is the error, whichever a worker reaches first, and the sprites before it
    // are written (with the damaged one's palette, which is encoded before its block is read).
    const auto damaged = ManySprites(SpriteNames(24), {9, 14});
    const SpriteRun serial = ConvertWithJobs(damaged, 1);
    OBL_CHECK(!serial.ok);
    OBL_CHECK(serial.error.find("sprite_109") != std::string::npos);
    OBL_CHECK_EQ(serial.sprites, 9);
    for (int jobs : {2, 4, 0}) CheckSameSprites(serial, ConvertWithJobs(damaged, jobs));
    for (int shards : {2, 3, 5}) CheckSameSprites(serial, ConvertSharded(damaged, shards));

    // A name that repeats in another case is refused after the sprites before it, as with one job.
    auto names = SpriteNames(12);
    names[8] = "SPRITE_103";
    const SpriteRun repeated = ConvertWithJobs(ManySprites(names), 1);
    OBL_CHECK(!repeated.ok);
    OBL_CHECK(repeated.error.find("occurs twice") != std::string::npos);
    OBL_CHECK_EQ(repeated.sprites, 8);
    CheckSameSprites(repeated, ConvertWithJobs(ManySprites(names), 4));
    CheckSameSprites(repeated, ConvertSharded(ManySprites(names), 3));
}

// --- maps --------------------------------------------------------------------------------------------------------------

using Files = std::map<std::string, std::vector<uint8_t>>;

std::vector<uint8_t> Text(const std::string& text)
{
    return {text.begin(), text.end()};
}

std::vector<uint8_t> Xor(const std::string& text)
{
    std::vector<uint8_t> bytes(text.begin(), text.end());
    for (auto& byte : bytes) byte ^= 0xff;
    return bytes;
}

// Several islands (one per scenario folder) that need a decode and four images each, and two scenarios that share the first.
Files MapInstallation(int islands)
{
    Files files;
    files["Data/Script/BuildingList.ble"] = Xor("\"COMFAM_BEACH\" \"Beach\"\n");
    for (int n = 1; n <= islands + 1; ++n) {
        const int island = std::min(n, islands); // the last scenario repeats the last island
        const std::string suffix = std::to_string(island);
        const std::string name = "Scenario" + std::to_string(n), folder = "Data/Scenario/" + name + "/";
        files["Data/Isle/Isle" + suffix + ".biz"] = obl::test::Biz(obl::test::RepeatedBlock(0x34, uint8_t(island), 0xcd, 0xab));
        files["Data/Isle/isle" + suffix + ".dat"] = obl::test::Grid([](size_t, size_t) { return obl::map_pack::kTerrainFlags[0]; });
        files["Data/Isle/height" + suffix + ".tga"] = obl::test::HeightTga(4 + island, 3);
        files["Data/Water/WaterAnim" + suffix + ".wat"] = obl::test::WaterFile(64);
        files[folder + name + ".pis"] = Text("'WORLD_MAPFILENAME' \"Data\\Isle\\Isle" + suffix +
                ".biz\"\r\n'WORLD_MAPGRID' \"Data/Isle/isle" + suffix + ".dat\"\r\n'WORLD_MAPHEIGHTMAP' \"Data/Isle/height" + suffix +
                ".tga\"\r\n'WORLD_HEIGHTMAPSCALE' 2.5\r\n'WORLD_WATERANIM' \"Data/Water/WaterAnim" + suffix +
                ".wat\"\r\n'WORLD_SCRIPT' \"" + folder + name + ".lgc\"\r\n");
        files[folder + name + ".ble"] = Xor("// " + name + "\r\n");
    }
    return files;
}

struct MapRun {
    bool ok = false;
    std::string error;
    std::map<std::string, std::vector<uint8_t>, std::less<>> files;
    std::vector<std::pair<int, int>> reports;
    int fileCount = 0;
};

MapRun ConvertMapsWithJobs(const Files& files, int jobs)
{
    MemorySource source;
    for (const auto& [path, bytes] : files) source.Add(path, bytes);
    obl::json_schema::Registry schemas;
    MapRun run;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, run.error));
    MemorySink sink;
    run.ok = ConvertMaps(
            source, sink, schemas, run.fileCount, run.error, [&](int done, int total) { run.reports.emplace_back(done, total); }, nullptr,
            jobs);
    run.files = sink.files();
    return run;
}

// The maps converted by `shards` instances that share nothing, then by one that takes their images over.
MapRun ConvertMapsSharded(const Files& files, int shards)
{
    MemorySource source;
    for (const auto& [path, bytes] : files) source.Add(path, bytes);
    obl::json_schema::Registry schemas;
    MapRun run;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, run.error));
    Units units;
    for (int index = 0; index < shards; ++index) {
        const Shard shard{index, shards};
        UnitExchange exchange;
        exchange.shard = &shard;
        exchange.sink = [&units](std::string_view step, size_t unit, UnitOutput& output) {
            units[{std::string(step), unit}] = std::move(output);
        };
        MemorySink untouched;
        int written = 0;
        // A damaged input may fail a cheap step of the plan before any image: that is the importer's to report, not the shard's.
        ConvertMaps(source, untouched, schemas, written, run.error, {}, nullptr, 1, &exchange);
        OBL_CHECK(untouched.files().empty());
    }
    const UnitExchange exchange = Taking(units);
    MemorySink sink;
    run.ok = ConvertMaps(
            source, sink, schemas, run.fileCount, run.error, [&](int done, int total) { run.reports.emplace_back(done, total); }, nullptr,
            1, &exchange);
    run.files = sink.files();
    return run;
}

void CheckSameMaps(const MapRun& serial, const MapRun& parallel)
{
    OBL_CHECK_EQ(parallel.ok, serial.ok);
    OBL_CHECK_EQ(parallel.error, serial.error);
    OBL_CHECK(parallel.files == serial.files);
    OBL_CHECK(parallel.reports == serial.reports);
    OBL_CHECK_EQ(parallel.fileCount, serial.fileCount);
}

OBL_TEST_CASE("convert.parallel_maps_equal_serial")
{
    const Files files = MapInstallation(4);
    const MapRun serial = ConvertMapsWithJobs(files, 1);
    OBL_REQUIRE(serial.ok);
    OBL_CHECK(serial.files.contains("maps/isle4/color.png"));
    OBL_CHECK(serial.files.contains("maps/water/4.png"));
    OBL_CHECK(serial.files.contains("maps/props.json"));
    OBL_CHECK_EQ(serial.reports.size(), size_t{6}); // before each of the five scenarios and after the last
    for (int jobs : {2, 4, 0}) CheckSameMaps(serial, ConvertMapsWithJobs(files, jobs));
    for (int shards : {1, 2, 3, 7}) CheckSameMaps(serial, ConvertMapsSharded(files, shards));
}

OBL_TEST_CASE("convert.parallel_maps_report_the_first_error_of_a_serial_run")
{
    // Island 3 has a cut BIZ and island 4 a cut terrain grid: the error is island 3's, island 1 and 2 are complete, and
    // island 3 has the files its serial run wrote before the failure (terrain and height).
    Files files = MapInstallation(4);
    files["Data/Isle/Isle3.biz"].resize(100);
    files["Data/Isle/isle4.dat"].resize(100);
    const MapRun serial = ConvertMapsWithJobs(files, 1);
    OBL_CHECK(!serial.ok);
    OBL_CHECK_EQ(serial.error, std::string("truncated BIZ tables"));
    OBL_CHECK(serial.files.contains("maps/isle2/depth.png"));
    OBL_CHECK(serial.files.contains("maps/isle3/terrain.png"));
    OBL_CHECK(!serial.files.contains("maps/isle3/color.png"));
    for (int jobs : {2, 4, 0}) CheckSameMaps(serial, ConvertMapsWithJobs(files, jobs));
    for (int shards : {2, 3, 7}) CheckSameMaps(serial, ConvertMapsSharded(files, shards));

    // A source that is missing and a scenario the cheap steps refuse come in their serial positions too.
    Files missing = MapInstallation(4);
    missing.erase("Data/Water/WaterAnim2.wat");
    const MapRun noWater = ConvertMapsWithJobs(missing, 1);
    OBL_CHECK(!noWater.ok);
    for (int jobs : {3, 0}) CheckSameMaps(noWater, ConvertMapsWithJobs(missing, jobs));
    for (int shards : {2, 5}) CheckSameMaps(noWater, ConvertMapsSharded(missing, shards));
    Files badName = MapInstallation(4);
    badName["Data/Scenario/Scenario4/Scenario4.pis"] = Text("'WORLD_MAPFILENAME' \"\"\r\n");
    const MapRun noKey = ConvertMapsWithJobs(badName, 1);
    OBL_CHECK(!noKey.ok);
    OBL_CHECK(noKey.files.contains("maps/isle3/depth.png"));
    for (int jobs : {3, 0}) CheckSameMaps(noKey, ConvertMapsWithJobs(badName, jobs));
    for (int shards : {2, 5}) CheckSameMaps(noKey, ConvertMapsSharded(badName, shards));
}

OBL_TEST_CASE("convert.parallel_maps_keep_the_conversion_stamp")
{
    // The stamp lists every written file and is written last, whatever the jobs; a run that finds it current starts no work.
    const Files files = MapInstallation(3);
    MemorySource source;
    for (const auto& [path, bytes] : files) source.Add(path, bytes);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    MemorySink sink;
    int written = 0;
    bool upToDate = true;
    OBL_REQUIRE(ConvertMaps(source, sink, schemas, written, error, {}, &upToDate, 4));
    OBL_CHECK(!upToDate);
    const auto stamp = sink.Read(std::string("maps/") + kMapStampName);
    OBL_REQUIRE(stamp.has_value());
    const std::string text(stamp->begin(), stamp->end());
    OBL_CHECK(text.ends_with("\nend\n"));
    size_t listed = 0;
    for (const auto& name : sink.Files())
        if (name != std::string("maps/") + kMapStampName) {
            OBL_CHECK(text.find("file " + name + "\n") != std::string::npos);
            ++listed;
        }
    OBL_CHECK_EQ(listed, static_cast<size_t>(written));

    const auto before = sink.files();
    int reports = 0, again = -1;
    OBL_REQUIRE(ConvertMaps(source, sink, schemas, again, error, [&](int, int) { ++reports; }, &upToDate, 4));
    OBL_CHECK(upToDate);
    OBL_CHECK_EQ(again, 0);
    OBL_CHECK_EQ(reports, 0);
    OBL_CHECK(sink.files() == before);

    // The stamp of a serial run is the same bytes.
    MemorySink serial;
    int serialFiles = 0;
    OBL_REQUIRE(ConvertMaps(source, serial, schemas, serialFiles, error, {}, nullptr, 1));
    OBL_CHECK(serial.files() == before);
}

} // namespace
