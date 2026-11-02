#pragma once

#include <cstddef>
#include <cstdint>

// EA RefPack (QFS) decoder (docs/architecture/data-formats/converter/scenarios.md).
// Pure logic: no game memory, so it is unit-tested.
namespace obl::refpack {

// Decoded size from the header (`10 FB` or `11 FB` + 24-bit big-endian size, 3 more bytes of compressed
// size when bit 0x100 is set). Returns -1 when `src` does not start with a RefPack header. The decoder
// decoder never reads this field and does not check the signature; callers use it to size the output.
int64_t DecodedSize(const uint8_t* src);

// Decodes `src` into `dst` as the game does (it stops at the 0xFC..0xFF stop command, input length is not
// known) but never writes past `dstCapacity`. Returns the number of bytes produced, or -1 when the output
// would overflow. Unlike a naive decoder, a null `src` is rejected with -1 instead of being a silent no-op.
int64_t Decode(const uint8_t* src, uint8_t* dst, size_t dstCapacity);

} // namespace obl::refpack
