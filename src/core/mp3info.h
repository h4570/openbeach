#pragma once

#include <cstddef>
#include <cstdint>

// MP3 length scanner behind the playlist (docs/architecture/platform.md). Pure logic:
// no game memory, so it is unit-tested.
namespace obl::mp3 {

struct Mp3Info {
    int32_t bitrate; // kbit/s of the first frame, zeroed when a later frame differs (VBR)
    int32_t sampleRate; // Hz
    int32_t notMono; // 1 unless the channel mode is mono
    int32_t lengthMs; // computed by Finish
    int32_t frames;
    int32_t version; // 1 = MPEG1, 2 = MPEG2/2.5
    int32_t bitrateSum; // sum of frame bitrates, averaged by Finish
    int32_t layer; // layer bits of the first frame (1 = Layer III), -1 before the first frame
    int32_t skip; // bytes of an oversized frame that still lie in the next chunk
    uint8_t carry[4]; // unfinished header bytes carried to the next chunk
    int32_t carryLen;
    int32_t frameBytes; // sum of frame lengths
    int32_t junkBytes; // bytes skipped outside frames
    uint8_t valid;
};

// Initializes the scanner state.
void Init(Mp3Info& info);

// Feeds one chunk. Frame headers can straddle chunks.
void ParseFrames(Mp3Info& info, const uint8_t* data, uint32_t size);

// Second half of ScanFile: the verdict and the length once every chunk was parsed. A file without frames is invalid.
void Finish(Mp3Info& info);

// True when the stream parsed as MPEG layer 3.
bool IsValidLayer3(const Mp3Info& info);

// A file within directory read in 4 KB chunks. A file that cannot be opened leaves `info` untouched.
void ScanFile(Mp3Info& info, const char* directory, const char* file);

// True when the fields are equal (unused carry bytes aside); `diff` (optional,
// at least 96 bytes) receives the first mismatching field.
bool Equal(const Mp3Info& a, const Mp3Info& b, char* diff = nullptr, size_t diffSize = 0);

} // namespace obl::mp3
