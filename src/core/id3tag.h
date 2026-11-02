#pragma once

#include <cstddef>
#include <cstdint>

// ID3v1 tag reader behind the playlist (PlaylistTrack::ReadId3Tag; docs/architecture/platform.md). Pure logic: no game memory,
// so it is unit-tested.
namespace obl::id3 {

constexpr size_t kTagSize = 128;
constexpr size_t kFieldSize = 30;

// The two texts the game keeps for a track. Both are cut at the first NUL of their 30-byte field, as the game's
// `strncpy` into a zeroed buffer does.
struct Id3v1Text {
    char title[kFieldSize + 1]; // tag +3
    char artist[kFieldSize + 1]; // tag +33
};

// The 128-byte block must start with "TAG". `out` is untouched otherwise.
bool ParseTag(const uint8_t* block, Id3v1Text& out);

// `directory` and `file` are concatenated without a separator (the game's "%s%s", directories end with one). Reads the
// last 128 bytes. Returns false when the file cannot be opened, is shorter than a tag (the result then follows
// uninitialized stack memory) or has no "TAG" signature.
bool ReadTag(const char* directory, const char* file, Id3v1Text& out);

} // namespace obl::id3
