#include "importer/src/writers/sound_convert.h"
#include "importer/src/readers/save/save_stream.h"
#include "importer/src/source/source.h"
#include "core/ascii_case.h"
#include <cstring>
#include <exception>
#include <fstream>
#include <set>
#include <string_view>
namespace obl::convert {
namespace {

constexpr size_t kRecordSize = 40;
constexpr size_t kNameSize = 32;
constexpr uint64_t kMaxIndexBytes = 1u << 20;
constexpr uint64_t kMaxSampleBytes = 64u << 20;

uint32_t U32(const uint8_t* p)
{
    return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// The file name of a sample: ASCII, ".wav" dropped (any case), lowercase, and a content key (lowercase letters, digits and '_').
bool SampleName(const uint8_t* field, std::string& name)
{
    const void* nul = std::memchr(field, 0, kNameSize);
    if (!nul) return false;
    name.assign(reinterpret_cast<const char*>(field), static_cast<const char*>(nul) - reinterpret_cast<const char*>(field));
    for (char& c : name) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
        c = ascii::Lower(c);
    }
    constexpr std::string_view suffix = ".wav";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        name.resize(name.size() - suffix.size());
    if (name.empty()) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

} // namespace

bool ParseSampleIndex(std::span<const uint8_t> header, uint64_t samplesSize, std::vector<SoundSample>& samples, std::string& error)
{
    samples.clear();
    if (header.empty() || header.size() % kRecordSize != 0) {
        error = "sample index is empty or not whole records";
        return false;
    }
    std::set<std::string> names;
    for (size_t at = 0; at < header.size(); at += kRecordSize) {
        const uint8_t* record = header.data() + at;
        SoundSample sample;
        sample.offset = U32(record);
        sample.size = U32(record + 4);
        if (!SampleName(record + 8, sample.name)) {
            error = "sample name is not a content key at record " + std::to_string(at / kRecordSize);
            return false;
        }
        if (sample.size == 0) {
            error = "sample " + sample.name + " is empty";
            return false;
        }
        if (uint64_t(sample.offset) + sample.size > samplesSize) {
            error = "sample " + sample.name + " runs past the samples file";
            return false;
        }
        if (!names.insert(sample.name).second) {
            error = "sample name " + sample.name + " repeats";
            return false;
        }
        samples.push_back(std::move(sample));
    }
    return true;
}

bool IsWaveFile(std::span<const uint8_t> bytes)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;
    return uint64_t(U32(bytes.data() + 4)) + 8 == bytes.size();
}

bool ConvertSamples(const ByteSource& source, PackSink& out, int& files, std::string& error)
{
    files = 0;
    try {
        const auto header = FindFile(source, "Data/Sounds/blsnd.hdr"), samples = FindFile(source, "Data/Sounds/blsnd.spf");
        if (!header || !samples) {
            error = "missing installation Data/Sounds/blsnd.hdr or blsnd.spf";
            return false;
        }
        std::vector<uint8_t> index;
        if (!ReadBoundedFile(source, *header, kMaxIndexBytes, index, error)) return false;
        const auto samplesSize = source.Size(*samples);
        if (!samplesSize) {
            error = "cannot read " + source.Describe(*samples);
            return false;
        }
        std::vector<SoundSample> entries;
        if (!ParseSampleIndex(index, *samplesSize, entries, error)) {
            error = "blsnd.hdr: " + error;
            return false;
        }
        for (const auto& entry : entries) {
            if (entry.size > kMaxSampleBytes) {
                error = "sample " + entry.name + " is too large";
                return false;
            }
            std::vector<uint8_t> bytes(entry.size);
            if (!source.Read(*samples, entry.offset, bytes)) {
                error = "cannot read sample " + entry.name;
                return false;
            }
            if (!IsWaveFile(bytes)) {
                error = "sample " + entry.name + " is not a RIFF/WAVE file of its size";
                return false;
            }
            if (!out.Write("sounds/" + entry.name + ".wav", bytes, error)) return false;
            ++files;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
