#pragma once

#include <cstddef>
#include <cstdint>

namespace obl::map_blocks {

inline constexpr uint32_t kBytesPerPixel = 4;

struct BlockSpan {
    uint32_t offset;
    uint32_t size;
};
BlockSpan SpanOf(int blocksPerRow, int blockSize, int x, int y);

int BlockIndex(int blocksPerRow, int x, int y);

struct BlockRange {
    int x0, y0, x1, y1;
    bool operator==(const BlockRange&) const = default;
};

float MapPixelsPerScreenPixel(int zoomLevel);

BlockRange ViewBlockRange(int cameraX, int cameraY, int blockSize, uint32_t viewWidth, uint32_t viewHeight, int zoomLevel);

bool IsOutside(const BlockRange& range, int x, int y);

void InterleavePlanes(const uint16_t* colour, const uint16_t* depth, size_t pixels, uint16_t* out);

inline constexpr uint32_t kColumnBlockBytes = 0x10000;
inline constexpr uint32_t kColumnDepthOffset = 0x8000;

inline constexpr uint32_t kTempMapRowBlocks = 32;

void InterleaveColumnRow(const uint8_t* column, int blocks, size_t pixelsPerBlock, uint16_t* out);

float TempMapProgress(uint32_t done, int total, float fade);

inline constexpr uint32_t kFnvOffset = 0x811c9dc5u;
uint32_t Fnv1a(uint32_t hash, const void* data, size_t bytes);

} // namespace obl::map_blocks
