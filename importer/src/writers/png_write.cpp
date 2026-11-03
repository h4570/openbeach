#include "importer/src/writers/png_write.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// The deflate of every PNG the converter writes. It is stb_image_write's own algorithm (fixed Huffman codes, hash buckets
// of recent positions, one step of lazy matching) with the same decisions, so the compressed bytes are the ones stb gives and
// converted packs keep their hashes; only the data structures differ. stb keeps a stretchy buffer per hash bucket, 16384 of
// them allocated again for every image, and compares matches byte by byte. Here the buckets are one flat table and the
// compare takes eight bytes at a time. The quirks that decide the output are kept: the bucket scan runs from the oldest
// position and a later one wins a tie, a full bucket drops its older half, and skipped positions are never inserted.
namespace {

constexpr int kHashSize = 16384;
constexpr int kWindow = 32768;
constexpr int kMaxMatch = 258;

constexpr uint16_t kLengthBase[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 259};
constexpr uint8_t kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
        4097, 6145, 8193, 12289, 16385, 24577, 32768};
constexpr uint8_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

uint32_t Reverse(uint32_t code, int bits)
{
    uint32_t result = 0;
    while (bits--) {
        result = (result << 1) | (code & 1);
        code >>= 1;
    }
    return result;
}

struct Code {
    uint16_t bits; // already reversed for the LSB-first stream
    uint8_t length;
};

// The fixed Huffman code of RFC 1951 for the literal/length symbols 0..285.
struct FixedCodes {
    Code symbol[286];
    FixedCodes()
    {
        for (int n = 0; n < 286; ++n) {
            if (n <= 143)
                symbol[n] = {static_cast<uint16_t>(Reverse(0x30 + n, 8)), 8};
            else if (n <= 255)
                symbol[n] = {static_cast<uint16_t>(Reverse(0x190 + n - 144, 9)), 9};
            else if (n <= 279)
                symbol[n] = {static_cast<uint16_t>(Reverse(n - 256, 7)), 7};
            else
                symbol[n] = {static_cast<uint16_t>(Reverse(0xc0 + n - 280, 8)), 8};
        }
    }
};

uint32_t Hash(const uint8_t* data)
{
    uint32_t hash = data[0] + (data[1] << 8) + (data[2] << 16);
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash & (kHashSize - 1);
}

// Length of the common prefix of `a` and `b`, at most `limit` (never more than kMaxMatch).
int Common(const uint8_t* a, const uint8_t* b, int limit)
{
    limit = limit < kMaxMatch ? limit : kMaxMatch;
    int i = 0;
    for (; i + 8 <= limit; i += 8) {
        uint64_t x, y;
        std::memcpy(&x, a + i, 8);
        std::memcpy(&y, b + i, 8);
        if (x != y) {
            // Both hosts (x64, wasm) are little endian: the first differing byte is the lowest set bit.
            return i + (std::countr_zero(x ^ y) >> 3);
        }
    }
    while (i < limit && a[i] == b[i]) ++i;
    return i;
}

class BitWriter {
public:
    explicit BitWriter(uint8_t* out) : out_(out) {}
    void Add(uint32_t code, int bits)
    {
        buffer_ |= static_cast<uint64_t>(code) << count_;
        count_ += bits;
        if (count_ >= 32) {
            std::memcpy(out_ + size_, &buffer_, 4);
            size_ += 4;
            buffer_ >>= 32;
            count_ -= 32;
        }
    }
    void Byte(uint8_t value) { out_[size_++] = value; }
    // Pads with zero bits to a byte boundary.
    void Finish()
    {
        for (; count_ > 0; count_ -= 8) {
            out_[size_++] = static_cast<uint8_t>(buffer_);
            buffer_ >>= 8;
        }
        count_ = 0;
    }
    size_t Size() const { return size_; }

private:
    uint8_t* out_;
    size_t size_ = 0;
    uint64_t buffer_ = 0;
    int count_ = 0;
};

// Same contract as stbi_zlib_compress: a malloc'ed buffer the caller frees; null when out of memory.
unsigned char* ZlibCompress(unsigned char* data, int length, int* outLength, int quality)
{
    static const FixedCodes codes;
    if (quality < 5) quality = 5;
    const int capacity = 2 * quality;
    const size_t bound = (static_cast<size_t>(length) * 9 + 7) / 8 + 64;
    uint8_t* out = static_cast<uint8_t*>(std::malloc(bound));
    // Positions per bucket, oldest first; `used` counts them.
    std::vector<int32_t> table(static_cast<size_t>(kHashSize) * capacity);
    std::vector<uint8_t> used(kHashSize, 0);
    if (!out) return nullptr;

    BitWriter writer(out);
    writer.Byte(0x78); // DEFLATE 32K window
    writer.Byte(0x5e); // FLEVEL = 1
    writer.Add(1, 1); // BFINAL
    writer.Add(1, 2); // BTYPE = fixed Huffman

    const auto literal = [&](int byte) { writer.Add(codes.symbol[byte].bits, codes.symbol[byte].length); };
    int i = 0;
    while (i < length - 3) {
        int h = static_cast<int>(Hash(data + i)), best = 3;
        int bestAt = -1;
        const int32_t* list = &table[static_cast<size_t>(h) * capacity];
        for (int j = 0; j < used[h]; ++j) {
            // A match of at least `best` bytes agrees on the byte before that length too: most candidates fail here, cheaply.
            if (list[j] > i - kWindow && data[list[j] + best - 1] == data[i + best - 1]) {
                const int d = Common(data + list[j], data + i, length - i);
                if (d >= best) {
                    best = d;
                    bestAt = list[j];
                }
            }
        }
        // A full bucket drops its older half.
        int32_t* bucket = &table[static_cast<size_t>(h) * capacity];
        if (used[h] == capacity) {
            std::memmove(bucket, bucket + quality, sizeof(int32_t) * quality);
            used[h] = static_cast<uint8_t>(quality);
        }
        bucket[used[h]++] = i;

        if (bestAt >= 0) {
            // Lazy matching: a better match one byte on turns this one into a literal.
            h = static_cast<int>(Hash(data + i + 1));
            list = &table[static_cast<size_t>(h) * capacity];
            for (int j = 0; j < used[h]; ++j) {
                // Only a match longer than `best` counts, so the byte at that length must agree (and exist, up to the cap).
                if (best < kMaxMatch && best < length - i - 1 && list[j] > i - (kWindow - 1) &&
                        data[list[j] + best] == data[i + 1 + best] && Common(data + list[j], data + i + 1, length - i - 1) > best) {
                    bestAt = -1;
                    break;
                }
            }
        }

        if (bestAt >= 0) {
            const int distance = i - bestAt;
            int j = 0;
            while (best > kLengthBase[j + 1] - 1) ++j;
            const Code symbol = codes.symbol[j + 257];
            writer.Add(symbol.bits, symbol.length);
            if (kLengthExtra[j]) writer.Add(static_cast<uint32_t>(best - kLengthBase[j]), kLengthExtra[j]);
            for (j = 0; distance > kDistBase[j + 1] - 1; ++j) {}
            writer.Add(Reverse(static_cast<uint32_t>(j), 5), 5);
            if (kDistExtra[j]) writer.Add(static_cast<uint32_t>(distance - kDistBase[j]), kDistExtra[j]);
            i += best;
        } else {
            literal(data[i]);
            ++i;
        }
    }
    for (; i < length; ++i) literal(data[i]);
    writer.Add(codes.symbol[256].bits, codes.symbol[256].length); // end of block
    writer.Finish();

    size_t size = writer.Size();
    // Stored blocks instead when compression made it larger.
    if (size > static_cast<size_t>(length) + 2 + ((static_cast<size_t>(length) + 32766) / 32767) * 5) {
        const size_t stored = static_cast<size_t>(length) + 2 + ((static_cast<size_t>(length) + 32766) / 32767) * 5 + 4;
        uint8_t* bigger = static_cast<uint8_t*>(std::realloc(out, stored > bound ? stored : bound));
        if (!bigger) {
            std::free(out);
            return nullptr;
        }
        out = bigger;
        size = 2;
        for (int j = 0; j < length;) {
            int block = length - j;
            if (block > 32767) block = 32767;
            out[size++] = length - j == block;
            out[size++] = static_cast<uint8_t>(block);
            out[size++] = static_cast<uint8_t>(block >> 8);
            out[size++] = static_cast<uint8_t>(~block);
            out[size++] = static_cast<uint8_t>(~block >> 8);
            std::memcpy(out + size, data + j, static_cast<size_t>(block));
            size += static_cast<size_t>(block);
            j += block;
        }
    }
    // Adler-32 of the input.
    uint32_t s1 = 1, s2 = 0;
    for (int j = 0; j < length;) {
        const int block = length - j < 5552 ? length - j : 5552;
        for (int k = 0; k < block; ++k) {
            s1 += data[j + k];
            s2 += s1;
        }
        s1 %= 65521;
        s2 %= 65521;
        j += block;
    }
    out[size++] = static_cast<uint8_t>(s2 >> 8);
    out[size++] = static_cast<uint8_t>(s2);
    out[size++] = static_cast<uint8_t>(s1 >> 8);
    out[size++] = static_cast<uint8_t>(s1);
    *outLength = static_cast<int>(size);
    return out;
}

} // namespace

#define STBIW_ZLIB_COMPRESS ZlibCompress

// Private copy of the writer: obl_common compiles its own (renderer_jpeg.cpp) and the Linux unit tests link both
// libraries into one executable.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505) // the BMP/TGA/JPEG writers stay unused
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace obl::convert {

namespace {
void Big32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift : {24, 16, 8, 0}) out.push_back(static_cast<uint8_t>(value >> shift));
}
void Chunk(std::vector<uint8_t>& out, const char* type, std::span<const uint8_t> bytes)
{
    Big32(out, static_cast<uint32_t>(bytes.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    Big32(out, stbiw__crc32(out.data() + start, static_cast<int>(bytes.size() + 4)));
}
std::vector<uint8_t> PlanePng(
        int width, int height, int bits, int type, std::span<const uint8_t> rows, std::span<const uint8_t> palette = {})
{
    int size = 0;
    unsigned char* compressed = stbi_zlib_compress(const_cast<unsigned char*>(rows.data()), static_cast<int>(rows.size()), &size, 8);
    if (!compressed) return {};
    std::vector<uint8_t> out{137, 80, 78, 71, 13, 10, 26, 10}, header;
    Big32(header, width);
    Big32(header, height);
    header.insert(header.end(), {static_cast<uint8_t>(bits), static_cast<uint8_t>(type), 0, 0, 0});
    Chunk(out, "IHDR", header);
    if (!palette.empty()) Chunk(out, "PLTE", palette);
    Chunk(out, "IDAT", std::span(compressed, size));
    STBIW_FREE(compressed);
    Chunk(out, "IEND", {});
    return out;
}
} // namespace

std::vector<uint8_t> EncodeIndexedPng(int width, int height, std::span<const uint8_t> pixels, std::span<const uint8_t> palette)
{
    if (width <= 0 || height <= 0 || pixels.size() != size_t(width) * height || palette.empty() || palette.size() % 3 ||
            palette.size() > 768)
        return {};
    std::vector<uint8_t> rows;
    for (int y = 0; y < height; ++y) {
        rows.push_back(0);
        for (int x = 0; x < width; ++x) {
            const auto index = pixels[size_t(y) * width + x];
            if (index >= palette.size() / 3) return {};
            rows.push_back(index);
        }
    }
    return PlanePng(width, height, 8, 3, rows, palette);
}

std::vector<uint8_t> EncodeGray16Png(int width, int height, std::span<const uint16_t> pixels)
{
    if (width <= 0 || height <= 0 || pixels.size() != size_t(width) * height) return {};
    std::vector<uint8_t> rows;
    rows.reserve((size_t(width) * 2 + 1) * height);
    for (int y = 0; y < height; ++y) {
        rows.push_back(0);
        for (int x = 0; x < width; ++x) {
            const auto value = pixels[size_t(y) * width + x];
            rows.push_back(static_cast<uint8_t>(value >> 8));
            rows.push_back(static_cast<uint8_t>(value));
        }
    }
    return PlanePng(width, height, 16, 0, rows);
}

std::vector<uint8_t> EncodePng(int width, int height, int channels, std::span<const uint8_t> pixels)
{
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
            pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels))
        return out;
    auto append = [](void* context, void* data, int size) {
        auto* bytes = static_cast<const uint8_t*>(data);
        static_cast<std::vector<uint8_t>*>(context)->insert(static_cast<std::vector<uint8_t>*>(context)->end(), bytes, bytes + size);
    };
    if (!stbi_write_png_to_func(append, &out, width, height, channels, pixels.data(), width * channels)) out.clear();
    return out;
}

} // namespace obl::convert
