#include "game/ui/map_blocks.h"

#include "core/byte_reader.h"
#include "core/float_to_int.h"

namespace obl::map_blocks {

BlockSpan SpanOf(int blocksPerRow, int blockSize, int x, int y)
{
    const uint32_t pixels = static_cast<uint32_t>(blockSize) * static_cast<uint32_t>(blockSize);
    const uint32_t index = static_cast<uint32_t>(y) * static_cast<uint32_t>(blocksPerRow) + static_cast<uint32_t>(x);
    return {index * pixels * kBytesPerPixel, pixels * kBytesPerPixel};
}

int BlockIndex(int blocksPerRow, int x, int y)
{
    return static_cast<int>(static_cast<uint32_t>(y) * static_cast<uint32_t>(blocksPerRow) + static_cast<uint32_t>(x));
}

float MapPixelsPerScreenPixel(int zoomLevel)
{
    if (zoomLevel == 0) return 0.5f;
    if (zoomLevel == 2) return 2.0f;
    return 1.0f;
}

namespace {

int Div(int a, int b)
{
    return b == 0 ? 0 : a / b;
}

} // namespace

BlockRange ViewBlockRange(int cameraX, int cameraY, int blockSize, uint32_t viewWidth, uint32_t viewHeight, int zoomLevel)
{
    const double scale = MapPixelsPerScreenPixel(zoomLevel);
    const int width = FloatToIntWrap(static_cast<double>(viewWidth) * scale);
    const int height = FloatToIntWrap(static_cast<double>(viewHeight) * scale);

    const int farX = static_cast<int>(static_cast<uint32_t>(width) + static_cast<uint32_t>(cameraX));
    const int farY = static_cast<int>(static_cast<uint32_t>(height) + static_cast<uint32_t>(cameraY));
    return {Div(cameraX, blockSize), Div(cameraY, blockSize), Div(farX, blockSize), Div(farY, blockSize)};
}

bool IsOutside(const BlockRange& range, int x, int y)
{
    return x < range.x0 || x > range.x1 || y < range.y0 || y > range.y1;
}

void InterleavePlanes(const uint16_t* colour, const uint16_t* depth, size_t pixels, uint16_t* out)
{
    for (size_t i = 0; i < pixels; ++i) {
        out[2 * i] = colour[i];
        out[2 * i + 1] = depth[i];
    }
}

void InterleaveColumnRow(const uint8_t* column, int blocks, size_t pixelsPerBlock, uint16_t* out)
{
    for (int block = 0; block < blocks; ++block) {
        const std::span<const uint8_t> base(column + static_cast<size_t>(block) * kColumnBlockBytes, kColumnBlockBytes);
        uint16_t* dest = out + static_cast<size_t>(block) * pixelsPerBlock * 2;
        for (size_t i = 0; i < pixelsPerBlock; ++i) {
            dest[2 * i] = Read<uint16_t>(base, 2 * i);
            dest[2 * i + 1] = Read<uint16_t>(base, kColumnDepthOffset + 2 * i);
        }
    }
}

float TempMapProgress(uint32_t done, int total, float fade)
{
    return static_cast<float>(static_cast<double>(done) / static_cast<double>(total) * static_cast<double>(fade));
}

uint32_t Fnv1a(uint32_t hash, const void* data, size_t bytes)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

} // namespace obl::map_blocks
