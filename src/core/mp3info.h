#pragma once

#include <cstddef>
#include <cstdint>

// MP3 length scanner behind the playlist (`Mp3Info_*`; docs/architecture/platform.md). Pure logic:
// no game memory, so it is unit-tested.
namespace obl::mp3 {

struct Mp3Info {
    int32_t bitrate; // +0x00 kbit/s of the first frame, zeroed when a later frame differs (VBR)
    int32_t sampleRate; // +0x04 Hz
    int32_t notMono; // +0x08 1 unless the channel mode is mono
    int32_t lengthMs; // +0x0c computed by Finish
    int32_t frames; // +0x10
    int32_t version; // +0x14 1 = MPEG1, 2 = MPEG2/2.5
    int32_t bitrateSum; // +0x18 sum of frame bitrates, averaged by Finish
    int32_t layer; // +0x1c layer bits of the first frame (1 = Layer III), -1 before the first frame
    int32_t skip; // +0x20 bytes of an oversized frame that still lie in the next chunk
    uint8_t carry[4]; // +0x24 unfinished header bytes carried to the next chunk
    int32_t carryLen; // +0x28
    int32_t frameBytes; // +0x2c sum of frame lengths
    int32_t junkBytes; // +0x30 bytes skipped outside frames
    uint8_t valid; // +0x34
    uint8_t pad[3];
};

// Initializes the scanner state, carry[3] and the padding included.
void Init(Mp3Info& info);

// Feeds one chunk (the game uses 4 KB). Frame headers can straddle chunks.
void ParseFrames(Mp3Info& info, const uint8_t* data, uint32_t size);

// Second half of ScanFile: the verdict and the length once every chunk was parsed. Unlike a naive length division
// it handles a file without frames; such a file is invalid.
void Finish(Mp3Info& info);

// True when the stream parsed as MPEG layer 3.
bool IsValidLayer3(const Mp3Info& info);

// A file within directory read in 4 KB chunks. A file that cannot be opened leaves `info` untouched.
void ScanFile(Mp3Info& info, const char* directory, const char* file);

// True when the fields the game reads (everything but unused carry bytes/padding) are equal; `diff` (optional,
// at least 96 bytes) receives the first mismatching field.
bool Equal(const Mp3Info& a, const Mp3Info& b, char* diff = nullptr, size_t diffSize = 0);

} // namespace obl::mp3
