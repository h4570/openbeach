#pragma once

#include <cstdint>
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace obl::convert {

// One sample of the index: its name as the converter writes it (lowercase, without ".wav") and its place in the .spf file.
struct SoundSample {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// Parses `blsnd.hdr`: 40-byte records of a little-endian offset, size and a 32-byte NUL-terminated name. Refused: a file that
// is not whole records, a name that is empty, not ASCII or not a content key once ".wav" is dropped and lowercased, a name that
// repeats after lowercasing, a zero size, and a sample that runs past `samplesSize` (the .spf file's size).
bool ParseSampleIndex(std::span<const uint8_t> header, uint64_t samplesSize, std::vector<SoundSample>& samples, std::string& error);

// A RIFF/WAVE file whose RIFF size matches its length (the byte copy of a sample must be a whole file).
bool IsWaveFile(std::span<const uint8_t> bytes);

// Converts every sample of the installation's `Data/Sounds/blsnd.hdr` (with `blsnd.spf`) into `<out>/sounds/<name>.wav`, a
// byte copy each, read one slice at a time. Both files must exist; a missing one is an error.
bool ConvertSamples(const ByteSource& source, PackSink& out, int& files, std::string& error);

} // namespace obl::convert
