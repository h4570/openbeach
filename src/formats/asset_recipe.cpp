#include "formats/asset_recipe.h"

#include "core/json_schema.h"
#include "core/png_read.h"
#include "core/sha256.h"
#include "formats/schemas.h"
#include "core/png_write.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obl::convert {
namespace {

constexpr const char* kCacheFile = "recipe-cache.json";
constexpr const char* kCacheFormat = "obl.recipe_cache";
constexpr int kCacheVersion = 1;

bool IsSpriteOutputPath(std::string_view path, std::string& key)
{
    if (!path.starts_with("sprites/") || !path.ends_with(".json")) return false;
    const std::string_view stem = path.substr(8, path.size() - 8 - 5);
    if (stem.empty() || stem.find('/') != std::string_view::npos) return false;
    for (char c : stem) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    key = std::string(stem);
    return true;
}

bool ValidateOutputPath(const AssetRecipe& recipe, std::string& error)
{
    const std::string_view out = recipe.output;
    if (out == "mod.json" || out.starts_with("recipes/") || out == kCacheFile) {
        error = "recipe output \"" + recipe.output + "\" may not target mod metadata or recipes/";
        return false;
    }
    if (out.ends_with(".png")) {
        if (recipe.hasSpriteOptions) {
            error = "\"sprite\" options are only allowed for sprites/<key>.json outputs";
            return false;
        }
        return true;
    }
    std::string key;
    if (IsSpriteOutputPath(out, key)) return true;
    error = "recipe output \"" + recipe.output + "\" must be a .png image or sprites/<key>.json";
    return false;
}

bool DecodeSourcePng(std::span<const uint8_t> bytes, RgbaImage& out, std::string& error)
{
    png_read::Image png;
    if (!png_read::Decode(bytes, png, error)) return false;
    if (png.width < 1 || png.height < 1 || png.width > kMaxRecipeImageSide || png.height > kMaxRecipeImageSide) {
        error = "source image dimensions exceed " + std::to_string(kMaxRecipeImageSide) + " pixels";
        return false;
    }
    if (png.bitDepth != 8) {
        error = "source PNG must be 8 bits per channel";
        return false;
    }
    out.width = png.width;
    out.height = png.height;
    const size_t pixelCount = static_cast<size_t>(png.width) * static_cast<size_t>(png.height);
    out.pixels.resize(pixelCount * 4);
    if (png.indexed) {
        const size_t paletteEntries = png.palette.size() / 3;
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t idx = png.bytes[i];
            if (idx >= paletteEntries) {
                error = "indexed source PNG references an out-of-range palette entry";
                return false;
            }
            out.pixels[i * 4 + 0] = png.palette[static_cast<size_t>(idx) * 3 + 0];
            out.pixels[i * 4 + 1] = png.palette[static_cast<size_t>(idx) * 3 + 1];
            out.pixels[i * 4 + 2] = png.palette[static_cast<size_t>(idx) * 3 + 2];
            out.pixels[i * 4 + 3] = 255;
        }
        return true;
    }
    if (png.channels == 1) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t g = png.bytes[i];
            out.pixels[i * 4 + 0] = g;
            out.pixels[i * 4 + 1] = g;
            out.pixels[i * 4 + 2] = g;
            out.pixels[i * 4 + 3] = 255;
        }
        return true;
    }
    if (png.channels == 2) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t g = png.bytes[i * 2 + 0];
            out.pixels[i * 4 + 0] = g;
            out.pixels[i * 4 + 1] = g;
            out.pixels[i * 4 + 2] = g;
            out.pixels[i * 4 + 3] = png.bytes[i * 2 + 1];
        }
        return true;
    }
    if (png.channels == 3) {
        for (size_t i = 0; i < pixelCount; ++i) {
            out.pixels[i * 4 + 0] = png.bytes[i * 3 + 0];
            out.pixels[i * 4 + 1] = png.bytes[i * 3 + 1];
            out.pixels[i * 4 + 2] = png.bytes[i * 3 + 2];
            out.pixels[i * 4 + 3] = 255;
        }
        return true;
    }
    if (png.channels == 4) {
        out.pixels = png.bytes;
        return true;
    }
    error = "unsupported source PNG channel count";
    return false;
}

bool ApplyCrop(const RgbaImage& src, const RecipeOp& op, RgbaImage& dst, std::string& error)
{
    if (op.x < 0 || op.y < 0 || op.width < 1 || op.height < 1 || op.x > src.width - op.width || op.y > src.height - op.height) {
        error = "crop rectangle (" + std::to_string(op.x) + ", " + std::to_string(op.y) + ", " + std::to_string(op.width) + "x" +
                std::to_string(op.height) + ") exceeds image bounds (" + std::to_string(src.width) + "x" + std::to_string(src.height) + ")";
        return false;
    }
    dst.width = op.width;
    dst.height = op.height;
    dst.pixels.resize(static_cast<size_t>(op.width) * static_cast<size_t>(op.height) * 4);
    const size_t rowBytes = static_cast<size_t>(op.width) * 4;
    for (int y = 0; y < op.height; ++y) {
        const size_t srcOff = (static_cast<size_t>(op.y + y) * static_cast<size_t>(src.width) + static_cast<size_t>(op.x)) * 4;
        const size_t dstOff = static_cast<size_t>(y) * rowBytes;
        std::memcpy(dst.pixels.data() + dstOff, src.pixels.data() + srcOff, rowBytes);
    }
    return true;
}

void ApplyMirror(const RgbaImage& src, const RecipeOp& op, RgbaImage& dst)
{
    dst.width = src.width;
    dst.height = src.height;
    dst.pixels.resize(src.pixels.size());
    const int w = src.width;
    const int h = src.height;
    if (op.axis == MirrorAxis::Horizontal) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t s = (static_cast<size_t>(y) * w + static_cast<size_t>(w - 1 - x)) * 4;
                const size_t d = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4;
                std::memcpy(dst.pixels.data() + d, src.pixels.data() + s, 4);
            }
        }
    } else {
        const size_t rowBytes = static_cast<size_t>(w) * 4;
        for (int y = 0; y < h; ++y) {
            const size_t s = static_cast<size_t>(h - 1 - y) * rowBytes;
            const size_t d = static_cast<size_t>(y) * rowBytes;
            std::memcpy(dst.pixels.data() + d, src.pixels.data() + s, rowBytes);
        }
    }
}

void ApplyScale(const RgbaImage& src, const RecipeOp& op, RgbaImage& dst)
{
    dst.width = op.width;
    dst.height = op.height;
    dst.pixels.resize(static_cast<size_t>(op.width) * static_cast<size_t>(op.height) * 4);
    const int sw = src.width;
    const int sh = src.height;
    const int dw = op.width;
    const int dh = op.height;
    if (op.filter == ScaleFilter::Nearest) {
        for (int y = 0; y < dh; ++y) {
            const int sy = static_cast<int>((static_cast<uint64_t>(y) * static_cast<uint64_t>(sh)) / static_cast<uint64_t>(dh));
            for (int x = 0; x < dw; ++x) {
                const int sx = static_cast<int>((static_cast<uint64_t>(x) * static_cast<uint64_t>(sw)) / static_cast<uint64_t>(dw));
                const size_t s = (static_cast<size_t>(sy) * sw + static_cast<size_t>(sx)) * 4;
                const size_t d = (static_cast<size_t>(y) * dw + static_cast<size_t>(x)) * 4;
                std::memcpy(dst.pixels.data() + d, src.pixels.data() + s, 4);
            }
        }
        return;
    }

    // 16.16 fixed-point bilinear resampling with pixel-center alignment: exact integer arithmetic, no floats.
    constexpr int kShift = 16;
    constexpr int64_t kOne = int64_t{1} << kShift;
    constexpr int64_t kHalf = int64_t{1} << (kShift - 1);
    const int64_t maxFx = static_cast<int64_t>(sw - 1) * kOne;
    const int64_t maxFy = static_cast<int64_t>(sh - 1) * kOne;

    std::vector<int> x0s(static_cast<size_t>(dw));
    std::vector<int> x1s(static_cast<size_t>(dw));
    std::vector<uint64_t> wxs(static_cast<size_t>(dw));
    for (int x = 0; x < dw; ++x) {
        int64_t fx = ((static_cast<int64_t>(2 * x + 1) * sw * kHalf) / dw) - kHalf;
        fx = std::clamp<int64_t>(fx, 0, maxFx);
        const int x0 = static_cast<int>(fx >> kShift);
        x0s[static_cast<size_t>(x)] = x0;
        x1s[static_cast<size_t>(x)] = std::min(x0 + 1, sw - 1);
        wxs[static_cast<size_t>(x)] = static_cast<uint64_t>(fx & (kOne - 1));
    }

    for (int y = 0; y < dh; ++y) {
        int64_t fy = ((static_cast<int64_t>(2 * y + 1) * sh * kHalf) / dh) - kHalf;
        fy = std::clamp<int64_t>(fy, 0, maxFy);
        const int y0 = static_cast<int>(fy >> kShift);
        const int y1 = std::min(y0 + 1, sh - 1);
        const uint64_t wy = static_cast<uint64_t>(fy & (kOne - 1));
        const uint64_t invWy = static_cast<uint64_t>(kOne) - wy;
        const size_t row0 = static_cast<size_t>(y0) * sw;
        const size_t row1 = static_cast<size_t>(y1) * sw;
        for (int x = 0; x < dw; ++x) {
            const int x0 = x0s[static_cast<size_t>(x)];
            const int x1 = x1s[static_cast<size_t>(x)];
            const uint64_t wx = wxs[static_cast<size_t>(x)];
            const uint64_t invWx = static_cast<uint64_t>(kOne) - wx;
            const uint8_t* p00 = src.pixels.data() + (row0 + static_cast<size_t>(x0)) * 4;
            const uint8_t* p10 = src.pixels.data() + (row0 + static_cast<size_t>(x1)) * 4;
            const uint8_t* p01 = src.pixels.data() + (row1 + static_cast<size_t>(x0)) * 4;
            const uint8_t* p11 = src.pixels.data() + (row1 + static_cast<size_t>(x1)) * 4;
            uint8_t* out = dst.pixels.data() + (static_cast<size_t>(y) * dw + static_cast<size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) {
                const uint64_t top = static_cast<uint64_t>(p00[c]) * invWx + static_cast<uint64_t>(p10[c]) * wx;
                const uint64_t bot = static_cast<uint64_t>(p01[c]) * invWx + static_cast<uint64_t>(p11[c]) * wx;
                const uint64_t val = top * invWy + bot * wy;
                out[c] = static_cast<uint8_t>((val + (uint64_t{1} << 31)) >> 32);
            }
        }
    }
}

void ApplyBlur(const RgbaImage& src, const RecipeOp& op, RgbaImage& dst)
{
    const int w = src.width;
    const int h = src.height;
    const int r = op.radius;
    const uint64_t window = static_cast<uint64_t>(2 * r + 1);
    const uint64_t half = static_cast<uint64_t>(r);

    RgbaImage cur = src;
    RgbaImage tmp;
    tmp.width = w;
    tmp.height = h;
    tmp.pixels.resize(src.pixels.size());

    for (int pass = 0; pass < op.passes; ++pass) {
        // Horizontal pass: cur -> tmp.
        for (int y = 0; y < h; ++y) {
            const uint8_t* rowIn = cur.pixels.data() + static_cast<size_t>(y) * w * 4;
            uint8_t* rowOut = tmp.pixels.data() + static_cast<size_t>(y) * w * 4;
            uint64_t sum[4] = {0, 0, 0, 0};
            for (int dx = -r; dx <= r; ++dx) {
                const int sx = std::clamp(dx, 0, w - 1);
                const uint8_t* p = rowIn + static_cast<size_t>(sx) * 4;
                for (int c = 0; c < 4; ++c) sum[c] += p[c];
            }
            for (int x = 0; x < w; ++x) {
                uint8_t* o = rowOut + static_cast<size_t>(x) * 4;
                for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>((sum[c] + half) / window);
                const int removeX = std::clamp(x - r, 0, w - 1);
                const int addX = std::clamp(x + r + 1, 0, w - 1);
                const uint8_t* pRem = rowIn + static_cast<size_t>(removeX) * 4;
                const uint8_t* pAdd = rowIn + static_cast<size_t>(addX) * 4;
                for (int c = 0; c < 4; ++c) sum[c] = sum[c] - pRem[c] + pAdd[c];
            }
        }
        // Vertical pass: tmp -> cur.
        for (int x = 0; x < w; ++x) {
            uint64_t sum[4] = {0, 0, 0, 0};
            for (int dy = -r; dy <= r; ++dy) {
                const int sy = std::clamp(dy, 0, h - 1);
                const uint8_t* p = tmp.pixels.data() + (static_cast<size_t>(sy) * w + static_cast<size_t>(x)) * 4;
                for (int c = 0; c < 4; ++c) sum[c] += p[c];
            }
            for (int y = 0; y < h; ++y) {
                uint8_t* o = cur.pixels.data() + (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4;
                for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>((sum[c] + half) / window);
                const int removeY = std::clamp(y - r, 0, h - 1);
                const int addY = std::clamp(y + r + 1, 0, h - 1);
                const uint8_t* pRem = tmp.pixels.data() + (static_cast<size_t>(removeY) * w + static_cast<size_t>(x)) * 4;
                const uint8_t* pAdd = tmp.pixels.data() + (static_cast<size_t>(addY) * w + static_cast<size_t>(x)) * 4;
                for (int c = 0; c < 4; ++c) sum[c] = sum[c] - pRem[c] + pAdd[c];
            }
        }
    }
    dst = std::move(cur);
}

int ReflectIndex(int i, int size)
{
    if (size <= 1) return 0;
    const int period = 2 * size;
    int m = i % period;
    if (m < 0) m += period;
    return m < size ? m : (period - 1 - m);
}

void ApplyPad(const RgbaImage& src, const RecipeOp& op, RgbaImage& dst)
{
    dst.width = op.width;
    dst.height = op.height;
    dst.pixels.resize(static_cast<size_t>(op.width) * static_cast<size_t>(op.height) * 4);
    // Floor division by 2 when centering so negative offsets behave consistently.
    const int diffX = op.width - src.width;
    const int diffY = op.height - src.height;
    const int offX = op.hasOffset ? op.x : (diffX >= 0 ? diffX / 2 : -((-diffX + 1) / 2));
    const int offY = op.hasOffset ? op.y : (diffY >= 0 ? diffY / 2 : -((-diffY + 1) / 2));

    for (int y = 0; y < op.height; ++y) {
        const int sy = y - offY;
        for (int x = 0; x < op.width; ++x) {
            const int sx = x - offX;
            uint8_t* out = dst.pixels.data() + (static_cast<size_t>(y) * op.width + static_cast<size_t>(x)) * 4;
            if (sx >= 0 && sx < src.width && sy >= 0 && sy < src.height) {
                std::memcpy(out, src.pixels.data() + (static_cast<size_t>(sy) * src.width + static_cast<size_t>(sx)) * 4, 4);
            } else if (op.padMode == PadMode::Color) {
                std::memcpy(out, op.color.data(), 4);
            } else if (op.padMode == PadMode::Edge) {
                const int cx = std::clamp(sx, 0, src.width - 1);
                const int cy = std::clamp(sy, 0, src.height - 1);
                std::memcpy(out, src.pixels.data() + (static_cast<size_t>(cy) * src.width + static_cast<size_t>(cx)) * 4, 4);
            } else {
                const int rx = ReflectIndex(sx, src.width);
                const int ry = ReflectIndex(sy, src.height);
                std::memcpy(out, src.pixels.data() + (static_cast<size_t>(ry) * src.width + static_cast<size_t>(rx)) * 4, 4);
            }
        }
    }
}

struct ColorCount {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint64_t count = 0;
    uint32_t Packed() const { return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b; }
};

struct ColorBox {
    size_t begin = 0;
    size_t end = 0;
    int rMin = 255, rMax = 0;
    int gMin = 255, gMax = 0;
    int bMin = 255, bMax = 0;
    uint64_t totalCount = 0;

    int LongestAxis() const
    {
        const int dr = rMax - rMin;
        const int dg = gMax - gMin;
        const int db = bMax - bMin;
        if (dr >= dg && dr >= db) return 0;
        if (dg >= db) return 1;
        return 2;
    }

    int LongestSpan() const { return std::max({rMax - rMin, gMax - gMin, bMax - bMin}); }
};

ColorBox MakeBox(const std::vector<ColorCount>& colors, size_t begin, size_t end)
{
    ColorBox box;
    box.begin = begin;
    box.end = end;
    for (size_t i = begin; i < end; ++i) {
        box.rMin = std::min<int>(box.rMin, colors[i].r);
        box.rMax = std::max<int>(box.rMax, colors[i].r);
        box.gMin = std::min<int>(box.gMin, colors[i].g);
        box.gMax = std::max<int>(box.gMax, colors[i].g);
        box.bMin = std::min<int>(box.bMin, colors[i].b);
        box.bMax = std::max<int>(box.bMax, colors[i].b);
        box.totalCount += colors[i].count;
    }
    return box;
}

// Deterministic integer median-cut quantization to `maxColors` RGB entries.
std::vector<uint8_t> BuildDeterministicPalette(const RgbaImage& image, size_t maxColors)
{
    std::map<uint32_t, uint64_t> histogram;
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        if (image.pixels[i * 4 + 3] < 128) continue;
        const uint32_t key = (static_cast<uint32_t>(image.pixels[i * 4 + 0]) << 16) |
                (static_cast<uint32_t>(image.pixels[i * 4 + 1]) << 8) | image.pixels[i * 4 + 2];
        histogram[key]++;
    }

    std::vector<uint8_t> palette(maxColors * 3, 0);
    if (histogram.empty()) return palette;

    std::vector<ColorCount> colors;
    colors.reserve(histogram.size());
    for (const auto& [key, count] : histogram) {
        colors.push_back({static_cast<uint8_t>((key >> 16) & 0xFF), static_cast<uint8_t>((key >> 8) & 0xFF),
                static_cast<uint8_t>(key & 0xFF), count});
    }

    if (colors.size() <= maxColors) {
        for (size_t i = 0; i < colors.size(); ++i) {
            palette[i * 3 + 0] = colors[i].r;
            palette[i * 3 + 1] = colors[i].g;
            palette[i * 3 + 2] = colors[i].b;
        }
        return palette;
    }

    std::vector<ColorBox> boxes;
    boxes.push_back(MakeBox(colors, 0, colors.size()));

    while (boxes.size() < maxColors) {
        // Pick the splittable box with largest span, breaking ties by totalCount then begin index.
        size_t bestIdx = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2) continue;
            if (bestIdx == std::numeric_limits<size_t>::max()) {
                bestIdx = i;
                continue;
            }
            const auto& a = boxes[i];
            const auto& b = boxes[bestIdx];
            if (a.LongestSpan() != b.LongestSpan() ? a.LongestSpan() > b.LongestSpan()
                                                   : (a.totalCount != b.totalCount ? a.totalCount > b.totalCount : a.begin < b.begin)) {
                bestIdx = i;
            }
        }
        if (bestIdx == std::numeric_limits<size_t>::max()) break;

        ColorBox box = boxes[bestIdx];
        const int axis = box.LongestAxis();
        std::sort(colors.begin() + static_cast<ptrdiff_t>(box.begin), colors.begin() + static_cast<ptrdiff_t>(box.end),
                [axis](const ColorCount& a, const ColorCount& b) {
                    const uint8_t ca = axis == 0 ? a.r : axis == 1 ? a.g : a.b;
                    const uint8_t cb = axis == 0 ? b.r : axis == 1 ? b.g : b.b;
                    return ca != cb ? ca < cb : a.Packed() < b.Packed();
                });

        const uint64_t halfCount = box.totalCount / 2;
        uint64_t acc = 0;
        size_t mid = box.begin + 1;
        for (size_t i = box.begin; i + 1 < box.end; ++i) {
            acc += colors[i].count;
            mid = i + 1;
            if (acc >= halfCount) break;
        }
        boxes[bestIdx] = MakeBox(colors, box.begin, mid);
        boxes.push_back(MakeBox(colors, mid, box.end));
    }

    // Sort boxes deterministically by average packed RGB so the palette order is stable.
    std::vector<std::array<uint8_t, 3>> entries;
    entries.reserve(boxes.size());
    for (const auto& box : boxes) {
        uint64_t rSum = 0, gSum = 0, bSum = 0, total = 0;
        for (size_t i = box.begin; i < box.end; ++i) {
            rSum += static_cast<uint64_t>(colors[i].r) * colors[i].count;
            gSum += static_cast<uint64_t>(colors[i].g) * colors[i].count;
            bSum += static_cast<uint64_t>(colors[i].b) * colors[i].count;
            total += colors[i].count;
        }
        const uint8_t r = static_cast<uint8_t>((rSum + total / 2) / total);
        const uint8_t g = static_cast<uint8_t>((gSum + total / 2) / total);
        const uint8_t b = static_cast<uint8_t>((bSum + total / 2) / total);
        entries.push_back({r, g, b});
    }
    std::sort(entries.begin(), entries.end());
    for (size_t i = 0; i < entries.size() && i < maxColors; ++i) {
        palette[i * 3 + 0] = entries[i][0];
        palette[i * 3 + 1] = entries[i][1];
        palette[i * 3 + 2] = entries[i][2];
    }
    return palette;
}

uint8_t NearestPaletteIndex(uint8_t r, uint8_t g, uint8_t b, std::span<const uint8_t> palette, size_t count)
{
    uint32_t bestDist = std::numeric_limits<uint32_t>::max();
    size_t bestIdx = 0;
    for (size_t i = 0; i < count; ++i) {
        const int dr = static_cast<int>(r) - static_cast<int>(palette[i * 3 + 0]);
        const int dg = static_cast<int>(g) - static_cast<int>(palette[i * 3 + 1]);
        const int db = static_cast<int>(b) - static_cast<int>(palette[i * 3 + 2]);
        const uint32_t dist = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
            if (dist == 0) break;
        }
    }
    return static_cast<uint8_t>(bestIdx);
}

struct CacheEntry {
    std::string recipeSha256;
    std::string source;
    std::string sourceSha256;
    std::vector<std::string> outputs;
};

std::map<std::string, CacheEntry> LoadCache(const PackSink& cache)
{
    std::map<std::string, CacheEntry> entries;
    const auto bytes = cache.Read(kCacheFile);
    if (!bytes) return entries;
    const auto doc = nlohmann::json::parse(bytes->begin(), bytes->end(), nullptr, false);
    if (!doc.is_object() || doc.value("format", "") != kCacheFormat || doc.value("version", 0) != kCacheVersion) return entries;
    const auto it = doc.find("entries");
    if (it == doc.end() || !it->is_object()) return entries;
    for (const auto& [recipePath, val] : it->items()) {
        if (!val.is_object()) continue;
        CacheEntry e;
        e.recipeSha256 = val.value("recipe_sha256", "");
        e.source = val.value("source", "");
        e.sourceSha256 = val.value("source_sha256", "");
        if (const auto outs = val.find("outputs"); outs != val.end() && outs->is_array()) {
            for (const auto& o : *outs)
                if (o.is_string()) e.outputs.push_back(o.get<std::string>());
        }
        if (!e.recipeSha256.empty() && !e.sourceSha256.empty() && !e.outputs.empty()) entries[recipePath] = std::move(e);
    }
    return entries;
}

bool SaveCache(PackSink& cache, const std::map<std::string, CacheEntry>& entries, std::string& error)
{
    nlohmann::ordered_json doc;
    doc["format"] = kCacheFormat;
    doc["version"] = kCacheVersion;
    nlohmann::ordered_json map = nlohmann::ordered_json::object();
    for (const auto& [recipePath, e] : entries) {
        nlohmann::ordered_json item;
        item["recipe_sha256"] = e.recipeSha256;
        item["source"] = e.source;
        item["source_sha256"] = e.sourceSha256;
        item["outputs"] = e.outputs;
        map[recipePath] = std::move(item);
    }
    doc["entries"] = std::move(map);
    return cache.WriteText(kCacheFile, doc.dump(2) + "\n", error);
}

bool IsRecipeFile(std::string_view path)
{
    return path.starts_with("recipes/") && path.ends_with(".json") && path.find('/', 8) == std::string_view::npos;
}

} // namespace

bool ParseAssetRecipe(std::string_view text, const json_schema::Registry& schemas, AssetRecipe& recipe, std::string& error)
{
    const auto doc = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "recipe is not a valid JSON object";
        return false;
    }
    const auto errors = schemas.Validate("obl.asset_recipe", doc);
    if (!errors.empty()) {
        error = "obl.asset_recipe schema:";
        for (size_t i = 0; i < errors.size() && i < 10; ++i) error += "\n  " + errors[i];
        return false;
    }

    AssetRecipe parsed;
    parsed.source = doc.at("source").get<std::string>();
    parsed.output = doc.at("output").get<std::string>();
    if (const auto sp = doc.find("sprite"); sp != doc.end()) {
        parsed.hasSpriteOptions = true;
        parsed.sprite.kind = sp->value("kind", std::string("ui"));
        parsed.sprite.animation = sp->value("animation", std::string("idle"));
        if (const auto hs = sp->find("hotspot"); hs != sp->end()) {
            parsed.sprite.hotspotX = (*hs)[0].get<int>();
            parsed.sprite.hotspotY = (*hs)[1].get<int>();
        }
        if (const auto pc = sp->find("palette_colors"); pc != sp->end()) {
            parsed.sprite.paletteColors = pc->get<int>();
            parsed.sprite.hasPaletteColors = true;
        }
    }
    if (!ValidateOutputPath(parsed, error)) return false;

    for (const auto& item : doc.at("operations")) {
        RecipeOp op;
        const std::string kind = item.at("op").get<std::string>();
        if (kind == "crop") {
            op.kind = RecipeOpKind::Crop;
            op.x = item.at("x").get<int>();
            op.y = item.at("y").get<int>();
            op.width = item.at("width").get<int>();
            op.height = item.at("height").get<int>();
        } else if (kind == "mirror") {
            op.kind = RecipeOpKind::Mirror;
            op.axis = item.at("axis").get<std::string>() == "vertical" ? MirrorAxis::Vertical : MirrorAxis::Horizontal;
        } else if (kind == "scale") {
            op.kind = RecipeOpKind::Scale;
            op.width = item.at("width").get<int>();
            op.height = item.at("height").get<int>();
            op.filter = item.value("filter", std::string("bilinear")) == "nearest" ? ScaleFilter::Nearest : ScaleFilter::Bilinear;
        } else if (kind == "blur") {
            op.kind = RecipeOpKind::Blur;
            op.radius = item.at("radius").get<int>();
            op.passes = item.value("passes", 1);
        } else if (kind == "pad") {
            op.kind = RecipeOpKind::Pad;
            op.width = item.at("width").get<int>();
            op.height = item.at("height").get<int>();
            if (item.contains("x") || item.contains("y")) {
                op.hasOffset = true;
                op.x = item.value("x", 0);
                op.y = item.value("y", 0);
            }
            const std::string mode = item.value("mode", std::string("color"));
            if (mode == "edge")
                op.padMode = PadMode::Edge;
            else if (mode == "mirror")
                op.padMode = PadMode::Mirror;
            else
                op.padMode = PadMode::Color;
            if (const auto col = item.find("color"); col != item.end()) {
                for (size_t c = 0; c < 4; ++c) op.color[c] = (*col)[c].get<uint8_t>();
            }
        }
        parsed.operations.push_back(op);
    }

    recipe = std::move(parsed);
    return true;
}

bool ApplyRecipeOperations(const RgbaImage& input, std::span<const RecipeOp> operations, RgbaImage& output, std::string& error)
{
    if (input.width < 1 || input.height < 1 || input.width > kMaxRecipeImageSide || input.height > kMaxRecipeImageSide ||
            input.pixels.size() != static_cast<size_t>(input.width) * static_cast<size_t>(input.height) * 4) {
        error = "invalid input RGBA image";
        return false;
    }
    RgbaImage current = input;
    for (size_t i = 0; i < operations.size(); ++i) {
        const RecipeOp& op = operations[i];
        RgbaImage next;
        switch (op.kind) {
        case RecipeOpKind::Crop:
            if (!ApplyCrop(current, op, next, error)) {
                error = "operation " + std::to_string(i) + ": " + error;
                return false;
            }
            break;
        case RecipeOpKind::Mirror: ApplyMirror(current, op, next); break;
        case RecipeOpKind::Scale:
            if (op.width < 1 || op.height < 1 || op.width > kMaxRecipeImageSide || op.height > kMaxRecipeImageSide) {
                error = "operation " + std::to_string(i) + ": scale size out of range";
                return false;
            }
            ApplyScale(current, op, next);
            break;
        case RecipeOpKind::Blur:
            if (op.radius < 1 || op.radius > 64 || op.passes < 1 || op.passes > 5) {
                error = "operation " + std::to_string(i) + ": blur parameters out of range";
                return false;
            }
            ApplyBlur(current, op, next);
            break;
        case RecipeOpKind::Pad:
            if (op.width < 1 || op.height < 1 || op.width > kMaxRecipeImageSide || op.height > kMaxRecipeImageSide) {
                error = "operation " + std::to_string(i) + ": pad size out of range";
                return false;
            }
            ApplyPad(current, op, next);
            break;
        }
        current = std::move(next);
    }
    output = std::move(current);
    return true;
}

std::vector<uint8_t> EncodeRecipePng(const RgbaImage& image)
{
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    bool opaque = true;
    for (size_t i = 0; i < pixelCount; ++i) {
        if (image.pixels[i * 4 + 3] != 255) {
            opaque = false;
            break;
        }
    }
    if (!opaque) return EncodePng(image.width, image.height, 4, image.pixels);
    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = image.pixels[i * 4 + 0];
        rgb[i * 3 + 1] = image.pixels[i * 4 + 1];
        rgb[i * 3 + 2] = image.pixels[i * 4 + 2];
    }
    return EncodePng(image.width, image.height, 3, rgb);
}

bool EncodeRecipeSprite(const RgbaImage& image, std::string_view outputJsonPath, const RecipeSpriteOptions& options,
        const json_schema::Registry& schemas, std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, std::string& error)
{
    std::string key;
    if (!IsSpriteOutputPath(outputJsonPath, key)) {
        error = "sprite output must be sprites/<key>.json";
        return false;
    }
    const int paletteColors = options.hasPaletteColors ? options.paletteColors : (options.kind == "character" ? 64 : 256);
    if (paletteColors != 64 && paletteColors != 256) {
        error = "palette_colors must be 64 or 256";
        return false;
    }
    const std::vector<uint8_t> palette = BuildDeterministicPalette(image, static_cast<size_t>(paletteColors));
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    std::vector<uint8_t> sheet(pixelCount * 2);
    for (size_t i = 0; i < pixelCount; ++i) {
        if (image.pixels[i * 4 + 3] < 128) {
            sheet[i * 2 + 0] = 0;
            sheet[i * 2 + 1] = 0;
        } else {
            sheet[i * 2 + 0] = NearestPaletteIndex(
                    image.pixels[i * 4 + 0], image.pixels[i * 4 + 1], image.pixels[i * 4 + 2], palette, static_cast<size_t>(paletteColors));
            sheet[i * 2 + 1] = 255;
        }
    }

    const std::string palRel = key + ".palettes.png";
    const std::string sheetRel = key + "/" + options.animation + ".png";
    nlohmann::ordered_json doc;
    doc["format"] = "obl.sprite";
    doc["version"] = 1;
    doc["name"] = key;
    doc["kind"] = options.kind;
    doc["pixels"] = "indexed";
    doc["palettes"] = palRel;
    doc["palette_colors"] = paletteColors;
    nlohmann::ordered_json anim;
    anim["name"] = options.animation;
    anim["directions"] = 1;
    anim["frames"] = 1;
    anim["size"] = {image.width, image.height};
    anim["hotspot"] = {options.hotspotX, options.hotspotY};
    anim["sheets"] = {sheetRel};
    anim["columns"] = 1;
    anim["unique_frames"] = 1;
    doc["animations"] = nlohmann::ordered_json::array({std::move(anim)});

    if (!formats::ValidateDocument(schemas, "obl.sprite", doc, error)) return false;

    const std::string jsonText = doc.dump(2) + "\n";
    files.clear();
    files.emplace_back(std::string(outputJsonPath), std::vector<uint8_t>(jsonText.begin(), jsonText.end()));
    files.emplace_back("sprites/" + palRel, EncodePng(paletteColors, 1, 3, palette));
    files.emplace_back("sprites/" + sheetRel, EncodePng(image.width, image.height, 2, sheet));
    return true;
}

bool BuildModRecipes(const PackSink& pack, const PackSink& mod, PackSink& cache, const json_schema::Registry& schemas,
        ModRecipesResult& result, std::string& error)
{
    std::vector<std::string> recipeFiles;
    for (const std::string& file : mod.Files()) {
        if (IsRecipeFile(file)) recipeFiles.push_back(file);
    }
    std::sort(recipeFiles.begin(), recipeFiles.end());

    const auto oldCache = LoadCache(cache);
    if (recipeFiles.empty() && oldCache.empty()) return true;

    std::map<std::string, CacheEntry> newCache;
    std::set<std::string> claimedOutputs;
    std::set<std::string> liveOutputs;

    for (const std::string& recipePath : recipeFiles) {
        const auto recipeBytes = mod.Read(recipePath);
        if (!recipeBytes) {
            error = "cannot read " + mod.Describe(recipePath);
            return false;
        }
        const std::string recipeText(recipeBytes->begin(), recipeBytes->end());
        AssetRecipe recipe;
        std::string parseError;
        if (!ParseAssetRecipe(recipeText, schemas, recipe, parseError)) {
            error = mod.Describe(recipePath) + ": " + parseError;
            return false;
        }
        if (!claimedOutputs.insert(recipe.output).second) {
            error = mod.Describe(recipePath) + ": duplicate recipe output \"" + recipe.output + "\"";
            return false;
        }

        const auto sourceBytes = pack.Read(recipe.source);
        if (!sourceBytes) {
            result.disabled.push_back({recipePath, recipe.source, recipe.output,
                    "recipe \"" + recipePath + "\": source \"" + recipe.source + "\" not found in pack; output \"" + recipe.output +
                            "\" disabled"});
            continue;
        }

        const std::string recipeSha = sha256::Hex(*recipeBytes);
        const std::string sourceSha = sha256::Hex(*sourceBytes);

        // Reuse cached outputs when neither the recipe nor the source changed and every cached file is still present.
        if (const auto it = oldCache.find(recipePath);
                it != oldCache.end() && it->second.recipeSha256 == recipeSha && it->second.sourceSha256 == sourceSha) {
            bool allPresent = true;
            for (const std::string& outFile : it->second.outputs) {
                if (!cache.Read(outFile)) {
                    allPresent = false;
                    break;
                }
            }
            if (allPresent) {
                for (const std::string& outFile : it->second.outputs) liveOutputs.insert(outFile);
                newCache[recipePath] = it->second;
                result.cached++;
                continue;
            }
        }

        RgbaImage sourceImage;
        std::string imgError;
        if (!DecodeSourcePng(*sourceBytes, sourceImage, imgError)) {
            error = mod.Describe(recipePath) + ": source \"" + recipe.source + "\": " + imgError;
            return false;
        }
        RgbaImage derived;
        if (!ApplyRecipeOperations(sourceImage, recipe.operations, derived, imgError)) {
            error = mod.Describe(recipePath) + ": " + imgError;
            return false;
        }

        std::vector<std::pair<std::string, std::vector<uint8_t>>> outputs;
        if (recipe.output.ends_with(".png")) {
            outputs.emplace_back(recipe.output, EncodeRecipePng(derived));
        } else {
            if (!EncodeRecipeSprite(derived, recipe.output, recipe.sprite, schemas, outputs, imgError)) {
                error = mod.Describe(recipePath) + ": " + imgError;
                return false;
            }
        }

        CacheEntry entry;
        entry.recipeSha256 = recipeSha;
        entry.source = recipe.source;
        entry.sourceSha256 = sourceSha;
        for (const auto& [outPath, outBytes] : outputs) {
            if (!cache.Write(outPath, outBytes, error)) return false;
            entry.outputs.push_back(outPath);
            liveOutputs.insert(outPath);
        }
        newCache[recipePath] = std::move(entry);
        result.built++;
    }

    // Remove stale outputs left behind by removed, renamed, or newly disabled recipes.
    for (const auto& [recipePath, entry] : oldCache) {
        for (const std::string& outPath : entry.outputs) {
            if (!liveOutputs.count(outPath)) cache.Remove(outPath);
        }
    }

    if (newCache.empty()) {
        cache.Remove(kCacheFile);
        return true;
    }
    return SaveCache(cache, newCache, error);
}

bool BuildInstalledModRecipes(const std::filesystem::path& packDir, const std::filesystem::path& modsDir,
        const std::filesystem::path& cacheRoot, const json_schema::Registry& schemas, ModRecipesResult& result, std::string& error,
        std::string_view onlyModId)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(modsDir, ec)) return true;

    std::vector<std::string> modIds;
    if (!onlyModId.empty()) {
        if (!std::filesystem::is_directory(modsDir / std::string(onlyModId), ec)) {
            error = "mod directory not found: " + (modsDir / std::string(onlyModId)).generic_string();
            return false;
        }
        modIds.emplace_back(onlyModId);
    } else {
        for (std::filesystem::directory_iterator it(modsDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) modIds.push_back(it->path().filename().string());
        }
        std::sort(modIds.begin(), modIds.end());
    }

    const auto pack = OpenDirectorySink(packDir);
    for (const std::string& id : modIds) {
        const std::filesystem::path modDir = modsDir / id;
        const std::filesystem::path cacheDir = cacheRoot / id;
        const bool hasRecipes = std::filesystem::is_directory(modDir / "recipes", ec);
        const bool hasCache = std::filesystem::is_regular_file(cacheDir / kCacheFile, ec);
        if (!hasRecipes && !hasCache) continue;
        const auto mod = OpenDirectorySink(modDir);
        const auto cache = OpenDirectorySink(cacheDir);
        if (!BuildModRecipes(*pack, *mod, *cache, schemas, result, error)) return false;
    }
    return true;
}

} // namespace obl::convert
