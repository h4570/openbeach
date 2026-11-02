#pragma once

#include <cstddef>
#include <cstdint>

// Whole-file and little-endian scalar reads: the file size, the whole file (optionally inverted), a read into a
// caller buffer and the 8/16/32 bit scalars. Pure logic, no game memory; the memory the caller later frees comes from an injected allocator, the stream of the scalar reads from an
// injected `fread`. Unit-tested.
namespace obl::file_io {

// The byte every buffer from ReadAll/ReadAllXorFF ends with: `.` (0x2e), not NUL. The text scanners
// (src/game/map/path_tile_script.h, the GUI/object/world script parsers) rely on a stop byte past the end.
inline constexpr uint8_t kTerminator = '.';

// GetFileSize: size in bytes (low 32 bits like an fpos_t read), 0 when the file cannot be opened or is empty.
uint32_t FileSize(const char* path);

// Allocator for results the game frees later (the block must come from the heap that frees it).
using Allocate = void* (*)(size_t size);

// Size + 1 bytes from `allocate`, the file content (every byte inverted when `invert`),
// then kTerminator. Null for a missing or empty file. Reading a null stream is avoided when the file vanishes between
// the size query and the read (a crash); this returns null instead.
void* ReadAll(const char* path, Allocate allocate, bool invert);

// Reads up to `count` bytes of the file into `destination`; a shorter file leaves the rest untouched and
// still counts as success. False only when the file cannot be opened.
bool ReadInto(const char* path, void* destination, size_t count);

// fread with the CRT signature: reads `count` elements of `size` bytes into `destination` and returns how many were
// complete. A short element must still have its bytes written (the CRT does so).
using FRead = size_t (*)(void* destination, size_t size, size_t count, void* stream);

// One little-endian value read through `read` (`size` = 1, 2 or 4). The reader fills the
// argument slot, which holds the stream pointer, so bytes a short read does not reach keep the low bits of that pointer:
// `streamValue` is the pointer's value (its low 32 bits). The result is zero-extended to the width.
uint32_t ReadScalar(unsigned size, void* stream, uint32_t streamValue, FRead read);

} // namespace obl::file_io
