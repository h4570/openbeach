#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <string>

// TGA and TIFF image loaders with the 32 to 24 bpp and 4 to 8 bpp conversions (
// docs/architecture/data-formats/converter-images.md). Pure logic: bytes of a file in, pixel buffer + palette out, no game memory, so it is
// unit-tested. The result holds width, height, bpp, pixels and the 768-byte RGB palette; the caller builds the image object from it.
//
// A naive decoder checks nothing: a truncated or inconsistent file makes it read or write out of bounds (or
// divide by zero). Where that would happen, these functions return false instead; for every valid file the
// game reads they produce the identical buffers.
namespace obl::image {

struct Image {
    int32_t width = 0;
    int32_t height = 0;
    int32_t bpp = 0; // 8, 16, 24 or 32 (Tiff: 8 * SamplesPerPixel)
    std::vector<uint8_t> pixels; // width * height * bpp/8 bytes, top row first
    std::vector<uint8_t> palette; // 768 bytes (R, G, B per entry) or empty when the image has no palette
    bool hasPalette() const { return !palette.empty(); }
};

// Reads a converted PNG into the same pixel/index and palette buffers as the engine decoders.
bool LoadPng(const std::filesystem::path& path, Image& out, std::string& error);

// Resolves only the migrated installation image paths; empty for other image families.
std::filesystem::path PackPath(std::string path);

// A whole TGA file: uncompressed (type 1/2) and RLE (type 9/10) images with 8 bpp + 24 bit colour map,
// 24 bpp (BGR → RGB) or 32 bpp (BGRA → RGBA), bottom-left origin only. Types the game does not use (0, 3, 11)
// fail; other unknown types yield an all-zero image as before.
bool DecodeTga(const uint8_t* data, size_t size, Image& out);

// A whole TIFF file: little-endian TIFF with strips or tiles, uncompressed (1) or PackBits (32773),
// an optional 16-bit colour map (→ 8-bit palette), 32 bpp RGBA → 24 bpp RGB and 4-bit samples → 8 bpp (first 8 palette
// entries only, as the game does). Other compression values leave the pixels zero.
bool DecodeTiff(const uint8_t* data, size_t size, Image& out);

// Colour map value 0..65535 → palette byte (truncated, not rounded).
uint8_t TiffColorToByte(uint16_t value);

// True when both images have the same size, depth, pixels and palette; `diff` (optional, >= 96 bytes) receives the
// first mismatch.
bool Equal(const Image& a, const Image& b, char* diff = nullptr, size_t diffSize = 0);

// 64-bit FNV-1a of size, depth, pixels and palette (the `image-load` command, to compare paths across runs).
uint64_t Hash(const Image& image);

} // namespace obl::image
