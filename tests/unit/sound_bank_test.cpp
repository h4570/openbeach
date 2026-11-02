#include "unit_test.h"
#include "game/ui/sound_bank.h"
#include "temp_dir.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
using Bytes = std::vector<uint8_t>;
Bytes Read(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
void Write(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void Put32(Bytes& bytes, uint32_t value)
{
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void Chunk(Bytes& bytes, const char* name, const Bytes& data)
{
    bytes.insert(bytes.end(), name, name + 4);
    Put32(bytes, static_cast<uint32_t>(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
    if (data.size() & 1) bytes.push_back(0);
}
Bytes Wav(bool ima)
{
    Bytes body{'W', 'A', 'V', 'E'};
    Bytes fmt = ima ? Bytes{17, 0, 1, 0, 0x11, 0x2b, 0, 0, 0, 0, 0, 0, 8, 0, 4, 0, 2, 0, 9, 0}
                    : Bytes{1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xac, 0, 0, 2, 0, 16, 0};
    Chunk(body, "fmt ", fmt);
    Chunk(body, "JUNK", {9});
    Chunk(body, "data", ima ? Bytes{0, 0, 0, 0, 0x11, 0x11, 0x11, 0x11} : Bytes{0, 0x80, 0, 0, 0xff, 0x7f});
    Bytes result{'R', 'I', 'F', 'F'};
    Put32(result, static_cast<uint32_t>(body.size()));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}
struct Fixture {
    std::filesystem::path directory = obl::test::UniqueTempPath("obl-sound-bank");
    Fixture() { std::filesystem::create_directories(directory); }
    ~Fixture() { std::filesystem::remove_all(directory); }
};
} // namespace

OBL_TEST_CASE("sound.wave_pcm_ima_and_invalid_riff")
{
    obl::audio::Wave wave;
    auto pcm = Wav(false);
    OBL_REQUIRE(obl::audio::DecodeWave(pcm, wave));
    OBL_CHECK_EQ(wave.rate, 22050);
    OBL_CHECK_EQ(wave.channels, 1);
    OBL_CHECK((wave.pcm == std::vector<int16_t>{-32768, 0, 32767}));
    OBL_REQUIRE(obl::audio::DecodeWave(Wav(true), wave));
    OBL_CHECK((wave.pcm == std::vector<int16_t>{0, 1, 2, 3, 4, 5, 6, 7, 8}));
    pcm.pop_back();
    OBL_CHECK(!obl::audio::DecodeWave(pcm, wave));
    OBL_CHECK(wave.pcm.empty());
    OBL_CHECK(!obl::audio::DecodeWave({}, wave));
    auto invalid = Wav(true);
    invalid[20] = 255; // unsupported WAV codec
    OBL_CHECK(!obl::audio::DecodeWave(invalid, wave));
}

OBL_TEST_CASE("sound.ima_stereo_partial_block_and_fact_inside_group")
{
    Bytes body{'W', 'A', 'V', 'E'};
    // A 24-byte block normally holds 17 frames. This final block has only nine frames.
    Chunk(body, "fmt ", {17, 0, 2, 0, 0x11, 0x2b, 0, 0, 0, 0, 0, 0, 24, 0, 4, 0, 2, 0, 17, 0});
    Chunk(body, "fact", {4, 0, 0, 0});
    Chunk(body, "data", {0, 0, 0, 0, 100, 0, 0, 0, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22});
    Bytes file{'R', 'I', 'F', 'F'};
    Put32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    obl::audio::Wave wave;
    OBL_REQUIRE(obl::audio::DecodeWave(file, wave));
    OBL_CHECK_EQ(wave.channels, 2);
    OBL_CHECK((wave.pcm == std::vector<int16_t>{0, 100, 1, 103, 2, 106, 3, 109}));
}

OBL_TEST_CASE("sound.bank_refcounts_case_cache_boundary_and_bad_directory")
{
    Fixture f;
    Write(f.directory / "Sample.WAV", {1, 2, 3});
    // Only .wav files are entries; the script and other files in packs/base/sounds are not.
    Write(f.directory / "sound_script.json", {'{', '}'});
    Write(f.directory / "notes.txt", {'x'});
    obl::audio::SoundBank bank;
    OBL_REQUIRE(bank.Open(f.directory));
    OBL_CHECK_EQ(bank.Entries().size(), 1u);
    OBL_CHECK(bank.Entries()[0].name == "SAMPLE.WAV");
    uint32_t size = 0;
    auto* first = bank.Get("sample.wav", size);
    OBL_REQUIRE(first);
    OBL_CHECK_EQ(size, 3u);
    OBL_CHECK(bank.Get("SaMpLe.WaV", size) == first);
    OBL_CHECK_EQ(bank.Entries()[0].references, 2);
    bank.Release("SAMPLE.WAV");
    bank.Update(100);
    OBL_CHECK_EQ(bank.CachedBytes(), 3u);
    bank.Release("sample.wav");
    bank.Update(5);
    OBL_CHECK_EQ(bank.CachedBytes(), 3u);
    OBL_REQUIRE(bank.Get("sample.wav", size) == first);
    bank.Update(100);
    OBL_CHECK_EQ(bank.CachedBytes(), 3u);
    bank.Release("sample.wav");
    bank.Update(5);
    OBL_CHECK_EQ(bank.CachedBytes(), 3u);
    bank.Update(0.001f);
    OBL_CHECK_EQ(bank.CachedBytes(), 0u);
    OBL_REQUIRE(bank.Get("sample.wav", size));
    OBL_CHECK_EQ(bank.CachedBytes(), 3u);
    size = 99;
    OBL_CHECK(!bank.Get("missing", size));
    OBL_CHECK_EQ(size, 0u);
    // A missing directory, a directory without .wav files, and an empty .wav file each refuse the bank.
    OBL_CHECK(!bank.Open(f.directory / "missing"));
    OBL_CHECK(bank.Entries().empty());
    Write(f.directory / "empty.wav", {});
    OBL_CHECK(!bank.Open(f.directory));
    OBL_CHECK(bank.Entries().empty());
    std::filesystem::remove(f.directory / "empty.wav");
    std::filesystem::remove(f.directory / "Sample.WAV");
    OBL_CHECK(!bank.Open(f.directory));
    OBL_CHECK(bank.Entries().empty());
}

OBL_TEST_CASE("sound.bank_layers_shadow_by_name_and_skip_missing_directories")
{
    // The samples come from the mod layer first, then packs/base (pack_path.h): one name, one file.
    Fixture f;
    const auto mod = f.directory / "mod";
    const auto pack = f.directory / "packs";
    std::filesystem::create_directories(mod);
    std::filesystem::create_directories(pack);
    Write(pack / "Shot.wav", {1, 2, 3});
    Write(pack / "Bell.WAV", {4});
    Write(mod / "shot.wav", {7, 7});
    Write(mod / "Extra.wav", {5, 5, 5, 5});
    const std::vector<std::filesystem::path> layers{mod, f.directory / "missing", pack};
    obl::audio::SoundBank bank;
    OBL_REQUIRE(bank.Open(layers));
    OBL_CHECK_EQ(bank.Entries().size(), 3u);
    uint32_t size = 0;
    OBL_REQUIRE(bank.Get("SHOT.WAV", size));
    OBL_CHECK_EQ(size, 2u);
    // Entries are sorted by name: BELL.WAV, EXTRA.WAV, SHOT.WAV; the shadowing file is the mod layer's.
    OBL_CHECK(bank.Entries()[2].path == mod / "shot.wav");
    OBL_REQUIRE(bank.Get("bell.wav", size));
    OBL_CHECK_EQ(size, 1u);
    OBL_CHECK(bank.Contains("extra.wav"));
    // No layer holds a .wav file: the bank is refused, a missing layer alone does not refuse it.
    OBL_CHECK(!bank.Open(std::vector<std::filesystem::path>{f.directory / "missing"}));
    OBL_CHECK(bank.Entries().empty());
}

OBL_TEST_CASE("data.sound_bank_extractor_and_pcm_oracles")
{
    auto run = std::filesystem::path(OBL_GAME_DIR);
    auto samples = run / "packs/base/sounds";
    if (!std::filesystem::exists(samples / "sound_script.json")) OBL_SKIP("no converted pack");
    if (!std::filesystem::exists(run / "extracted/sounds/WAVES_LOOP.pcm"))
        OBL_SKIP("run python tools/test_sound_data.py to prepare independent WAV/IMA oracles");
    obl::audio::SoundBank bank;
    OBL_REQUIRE(bank.Open(samples));
    unsigned pcmCount = 0, imaCount = 0;
    for (const auto& e : bank.Entries()) {
        uint32_t size = 0;
        const auto* raw = bank.Get(e.name.c_str(), size);
        OBL_REQUIRE(raw);
        auto wav = run / "extracted/sounds" / e.name;
        auto extracted = Read(wav);
        OBL_REQUIRE(extracted.size() == size);
        OBL_CHECK(!std::memcmp(raw, extracted.data(), size));
        obl::audio::Wave wave;
        OBL_REQUIRE(obl::audio::DecodeWave({raw, size}, wave));
        Bytes expected;
        if (extracted[20] == 17) {
            ++imaCount;
            wav.replace_extension(".pcm");
            expected = Read(wav);
        } else {
            ++pcmCount;
            // Independently locate the RIFF data chunk, rather than assuming a fixed WAV header.
            for (size_t p = 12; p + 8 <= extracted.size();) {
                uint32_t n = 0;
                std::memcpy(&n, extracted.data() + p + 4, 4);
                OBL_REQUIRE(p + 8ull + n <= extracted.size());
                if (!std::memcmp(extracted.data() + p, "data", 4)) {
                    expected.assign(extracted.begin() + p + 8, extracted.begin() + p + 8 + n);
                    break;
                }
                p += 8ull + n + (n & 1);
            }
        }
        OBL_CHECK_EQ(expected.size(), wave.pcm.size() * sizeof(int16_t));
        if (expected.size() == wave.pcm.size() * 2 && std::memcmp(expected.data(), wave.pcm.data(), expected.size())) {
            size_t first = 0, last = 0, count = 0;
            int maxDelta = 0;
            for (size_t i = 0; i < wave.pcm.size(); ++i) {
                int16_t value;
                std::memcpy(&value, expected.data() + i * 2, 2);
                if (value != wave.pcm[i]) {
                    if (!count) first = i;
                    last = i;
                    ++count;
                    maxDelta = std::max(maxDelta, std::abs(int(value) - wave.pcm[i]));
                }
            }
            obl::unit::ReportFailure(__FILE__, __LINE__,
                    e.name + " PCM differs: first=" + std::to_string(first) + " last=" + std::to_string(last) +
                            " count=" + std::to_string(count) + " max-delta=" + std::to_string(maxDelta));
        }
        bank.Release(e.name.c_str());
    }
    OBL_CHECK_EQ(pcmCount, 296u);
    OBL_CHECK_EQ(imaCount, 6u);
    bank.Update(5.01f);
    OBL_CHECK_EQ(bank.CachedBytes(), 0u);
}
