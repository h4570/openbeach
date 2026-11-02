#include "unit_test.h"
#include "game/ui/sound_bank.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/sound_convert.h"
#include "temp_dir.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

// One 40-byte record of blsnd.hdr: little-endian offset and size, then the 32-byte NUL-padded name.
std::vector<uint8_t> Record(uint32_t offset, uint32_t size, const std::string& name)
{
    std::vector<uint8_t> record(40, 0);
    for (int i = 0; i < 4; ++i) {
        record[i] = uint8_t(offset >> (8 * i));
        record[4 + i] = uint8_t(size >> (8 * i));
    }
    std::memcpy(record.data() + 8, name.data(), name.size());
    return record;
}

std::vector<uint8_t> Concat(std::vector<uint8_t> a, const std::vector<uint8_t>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<uint8_t> Wave(size_t total)
{
    std::vector<uint8_t> bytes(total, 0);
    std::memcpy(bytes.data(), "RIFF", 4);
    const auto riff = uint32_t(total - 8);
    for (int i = 0; i < 4; ++i) bytes[4 + i] = uint8_t(riff >> (8 * i));
    std::memcpy(bytes.data() + 8, "WAVE", 4);
    return bytes;
}

} // namespace

OBL_TEST_CASE("convert.sample_index_parses_and_names_samples")
{
    // Names drop ".WAV" in any case and are lowercased; the record order is kept.
    const auto header = Concat(Concat(Record(0, 8, "ARRESTED001.WAV"), Record(8, 4, "Vomit_1.wav")), Record(12, 4, "bell"));
    std::vector<SoundSample> samples;
    std::string error;
    OBL_REQUIRE(ParseSampleIndex(header, 16, samples, error));
    OBL_REQUIRE(samples.size() == 3);
    OBL_CHECK(samples[0].name == "arrested001");
    OBL_CHECK(samples[1].name == "vomit_1");
    OBL_CHECK(samples[1].offset == 8);
    OBL_CHECK(samples[2].name == "bell");
    OBL_CHECK(samples[2].size == 4);
}

OBL_TEST_CASE("convert.sample_index_refuses_bad_records")
{
    std::vector<SoundSample> samples;
    std::string error;
    const auto one = [](uint32_t offset, uint32_t size, const std::string& name) { return Record(offset, size, name); };
    // Not whole records, a name with a space or a dot, a repeated name after lowercasing, an empty sample, and a sample past the end.
    OBL_CHECK(!ParseSampleIndex(std::vector<uint8_t>(39, 0), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(one(0, 4, "hit one"), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(one(0, 4, "hit.two"), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(Concat(one(0, 4, "Hit_01.WAV"), one(4, 4, "hit_01")), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(one(0, 0, "silence"), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(one(96, 8, "late"), 100, samples, error));
    OBL_CHECK(!ParseSampleIndex(std::vector<uint8_t>(), 100, samples, error));
}

OBL_TEST_CASE("convert.wave_file_size_must_match")
{
    OBL_CHECK(IsWaveFile(Wave(44)));
    auto wrongSize = Wave(44);
    wrongSize[4] = 40;
    OBL_CHECK(!IsWaveFile(wrongSize));
    auto wrongForm = Wave(44);
    wrongForm[8] = 'X';
    OBL_CHECK(!IsWaveFile(wrongForm));
    OBL_CHECK(!IsWaveFile(std::vector<uint8_t>(8, 0)));
}

// The installation's bank as the engine read it before the pack: every 40-byte record of blsnd.hdr names a slice of
// blsnd.spf, keyed by its upper-cased name (the old SoundBank::Load keys). Kept here, not in the converter, so the check
// does not grade the converter with its own parser.
std::map<std::string, std::vector<uint8_t>> ReadInstalledBank(const std::filesystem::path& header, const std::filesystem::path& samples)
{
    std::ifstream headerFile(header, std::ios::binary), samplesFile(samples, std::ios::binary);
    const std::vector<uint8_t> index((std::istreambuf_iterator<char>(headerFile)), {});
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(samplesFile)), {});
    std::map<std::string, std::vector<uint8_t>> bank;
    for (size_t at = 0; at + 40 <= index.size(); at += 40) {
        const uint8_t* record = index.data() + at;
        const auto u32 = [](const uint8_t* p) {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        };
        const auto offset = u32(record), size = u32(record + 4);
        if (uint64_t(offset) + size > data.size()) return {};
        std::string name(reinterpret_cast<const char*>(record + 8));
        for (char& c : name) c = char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        bank.emplace(name, std::vector<uint8_t>(data.begin() + offset, data.begin() + offset + size));
    }
    return bank;
}

OBL_TEST_CASE("data.convert_samples_of_installation")
{
    // The converted pack gives the engine the installation's bank: the same names, sizes and bytes through SoundBank.
    const auto header = FindInstalledFile(OBL_GAME_DIR, "Data/Sounds/blsnd.hdr");
    const auto samples = FindInstalledFile(OBL_GAME_DIR, "Data/Sounds/blsnd.spf");
    if (!header || !samples) OBL_SKIP("no installation sounds");
    const obl::test::TempPath tmp_out("obl-sound-test-installation");
    const auto& out = tmp_out.path;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertSamples(*obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), files, error));

    const auto installed = ReadInstalledBank(*header, *samples);
    OBL_REQUIRE(!installed.empty());
    obl::audio::SoundBank bank;
    OBL_REQUIRE(bank.Open(out / "sounds"));
    OBL_CHECK(files == int(installed.size()));
    OBL_CHECK(bank.Entries().size() == installed.size());
    for (const auto& [name, bytes] : installed) {
        // A name without the .WAV suffix would be found by no lookup of the engine; the key set must match exactly.
        OBL_REQUIRE(bank.Contains(name.c_str()));
        uint32_t size = 0;
        const uint8_t* engine = bank.Get(name.c_str(), size);
        OBL_REQUIRE(engine != nullptr);
        OBL_REQUIRE(size == bytes.size());
        OBL_CHECK(std::memcmp(engine, bytes.data(), size) == 0);
        bank.Release(name.c_str());
    }
}
