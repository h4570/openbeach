#pragma once

#include <cstddef>
#include <cstdint>

// 16 bpp image helpers: the bilinear half-size resampler (Resample16, BuildHalfSizeChain) and the 16 bit palette
// (BuildPalette16; docs/architecture/data-formats/converter-images.md). Pure logic, no game memory, so it is unit-tested. The resampler
// keeps the game's evaluation order (float weights, products and sums in extended precision rounded to float, nearest
// integer rounding), so the results are bit-identical for any input the game can process.
namespace obl::image {

// Resamples `src` (srcW x srcH, row stride srcW) into `dst` (dstW x dstH) by bilinear interpolation of
// the colour channels, source position = destination index * (src - 1) / dst. 565 keeps 5 bit red and blue and the upper 5
// of 6 green bits (the lowest green bit comes out 0), 555 is plain 5-5-5. Needs srcW, srcH >= 2 as the game does (which
// reads one pixel right of and below the sample position). Does nothing for an empty destination or such a source.
void Resample16(const uint16_t* src, int srcW, int srcH, uint16_t* dst, int dstW, int dstH, bool is555);

// Halves the source repeatedly while the destination is at most half as wide as the current image,
// then resamples the last step into the destination (so an N:1 downscale averages through ~log2(N) 2:1 passes).
void BuildHalfSizeChain(const uint16_t* src, int srcW, int srcH, uint16_t* dst, int dstW, int dstH, bool is555);

// 256 RGB triplets (768 bytes) -> 256 RGB555 or RGB565 words.
void BuildPalette16(const uint8_t* palette, bool is555, uint16_t* out);

} // namespace obl::image
