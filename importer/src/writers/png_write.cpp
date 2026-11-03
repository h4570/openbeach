#include "importer/src/writers/png_write.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
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
    // Positions per bucket, oldest first; `used` counts them, so the table itself is never cleared. Kept between calls: a
    // sprite import encodes thousands of small images, and a fresh megabyte for each would cost more than the encoding.
    thread_local std::vector<int32_t> table;
    thread_local std::vector<uint8_t> used;
    if (table.size() != static_cast<size_t>(kHashSize) * capacity) table.resize(static_cast<size_t>(kHashSize) * capacity);
    used.assign(kHashSize, 0);
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

// CRC-32 of PNG chunks, eight bytes per step: the value of stb's byte-wise loop, which is a large share of the time for a
// compressed map. Row k of the table is row 0 advanced over k more zero bytes.
struct CrcTables {
    uint32_t row[8][256];
    CrcTables()
    {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            row[0][n] = c;
        }
        for (uint32_t n = 0; n < 256; ++n)
            for (int k = 1; k < 8; ++k) row[k][n] = (row[k - 1][n] >> 8) ^ row[0][row[k - 1][n] & 0xff];
    }
};

uint32_t Crc32(const uint8_t* data, size_t size)
{
    static const CrcTables tables;
    uint32_t crc = ~0u;
    for (; size >= 8; size -= 8, data += 8) {
        uint32_t low, high;
        std::memcpy(&low, data, 4); // little endian on every host (x64, wasm), as in Common() above
        std::memcpy(&high, data + 4, 4);
        low ^= crc;
        crc = tables.row[7][low & 0xff] ^ tables.row[6][(low >> 8) & 0xff] ^ tables.row[5][(low >> 16) & 0xff] ^ tables.row[4][low >> 24] ^
                tables.row[3][high & 0xff] ^ tables.row[2][(high >> 8) & 0xff] ^ tables.row[1][(high >> 16) & 0xff] ^
                tables.row[0][high >> 24];
    }
    for (; size > 0; --size, ++data) crc = tables.row[0][(crc ^ *data) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void Chunk(std::vector<uint8_t>& out, const char* type, std::span<const uint8_t> bytes)
{
    Big32(out, static_cast<uint32_t>(bytes.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    Big32(out, Crc32(out.data() + start, bytes.size() + 4));
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

namespace {

// The filters of PNG, by the number written in front of a row. The predictor of a byte is built from `left` (the same
// channel of the pixel before), `above` and `aboveLeft`; the filtered byte is the byte minus it, modulo 256.
constexpr int kFilterCount = 5;

// stbiw__paeth gives `left` on a tie with either, then `above`; written without branches so that the loops vectorize.
inline int Paeth(int left, int above, int aboveLeft)
{
    const int distLeft = std::abs(above - aboveLeft), distAbove = std::abs(left - aboveLeft),
              distAboveLeft = std::abs(left + above - 2 * aboveLeft);
    const bool useLeft = distLeft <= distAbove && distLeft <= distAboveLeft;
    const bool useAbove = distAbove <= distAboveLeft;
    return useLeft ? left : (useAbove ? above : aboveLeft);
}

template<int Filter> inline uint8_t Predict(int left, int above, int aboveLeft)
{
    if constexpr (Filter == 0) return 0;
    if constexpr (Filter == 1) return static_cast<uint8_t>(left);
    if constexpr (Filter == 2) return static_cast<uint8_t>(above);
    if constexpr (Filter == 3) return static_cast<uint8_t>((left + above) >> 1);
    if constexpr (Filter == 4) return static_cast<uint8_t>(Paeth(left, above, aboveLeft));
}

// stb's size estimate of a filtered row: the sum of the filtered bytes taken as signed (-128 counts 128).
template<int Filter> int Estimate(const uint8_t* row, const uint8_t* above, int channels, int size)
{
    int sum = 0;
    // The first pixel has no neighbour on the left: stb's first `channels` bytes use 0 for it (and for aboveLeft).
    const int head = size < channels ? size : channels;
    for (int i = 0; i < head; ++i) sum += std::abs(static_cast<int8_t>(static_cast<uint8_t>(row[i] - Predict<Filter>(0, above[i], 0))));
    for (int i = head; i < size; ++i)
        sum += std::abs(
                static_cast<int8_t>(static_cast<uint8_t>(row[i] - Predict<Filter>(row[i - channels], above[i], above[i - channels]))));
    return sum;
}

template<int Filter> void Apply(const uint8_t* row, const uint8_t* above, int channels, int size, uint8_t* out)
{
    const int head = size < channels ? size : channels;
    for (int i = 0; i < head; ++i) out[i] = static_cast<uint8_t>(row[i] - Predict<Filter>(0, above[i], 0));
    for (int i = head; i < size; ++i)
        out[i] = static_cast<uint8_t>(row[i] - Predict<Filter>(row[i - channels], above[i], above[i - channels]));
}

// Rows of filtered bytes with their filter byte in front, as stbi_write_png_to_mem chooses them: the filter with the smallest
// estimate, the lowest number on a tie. The first row has no row above, which stb handles with other filter functions
// (up is none, average halves the left byte, Paeth is Sub); a row of zeros above gives exactly those bytes. The estimates
// run first and the winner is then written straight into the deflate input, so no row is filtered more than twice.
std::unique_ptr<uint8_t[]> FilterRows(int width, int height, int channels, const uint8_t* pixels)
{
    const size_t size = static_cast<size_t>(width) * channels;
    const size_t stride = size + 1;
    auto filtered = std::make_unique_for_overwrite<uint8_t[]>(stride * height);
    if (!filtered) return nullptr;
    const std::vector<uint8_t> zeros(size);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = pixels + size * y;
        const uint8_t* above = y ? row - size : zeros.data();
        const int n = static_cast<int>(size);
        const int estimates[kFilterCount] = {Estimate<0>(row, above, channels, n), Estimate<1>(row, above, channels, n),
                Estimate<2>(row, above, channels, n), Estimate<3>(row, above, channels, n), Estimate<4>(row, above, channels, n)};
        int best = 0;
        for (int filter = 1; filter < kFilterCount; ++filter)
            if (estimates[filter] < estimates[best]) best = filter;
        uint8_t* out = filtered.get() + stride * y;
        *out++ = static_cast<uint8_t>(best);
        switch (best) {
        case 0: std::memcpy(out, row, size); break;
        case 1: Apply<1>(row, above, channels, n, out); break;
        case 2: Apply<2>(row, above, channels, n, out); break;
        case 3: Apply<3>(row, above, channels, n, out); break;
        default: Apply<4>(row, above, channels, n, out); break;
        }
    }
    return filtered;
}
} // namespace

std::vector<uint8_t> EncodePng(int width, int height, int channels, std::span<const uint8_t> pixels)
{
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
            pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels))
        return {};
    const auto filtered = FilterRows(width, height, channels, pixels.data());
    if (!filtered) return {};
    constexpr int kColorType[] = {0, 0, 4, 2, 6}; // gray, gray + alpha, RGB, RGBA
    return PlanePng(width, height, 8, kColorType[channels], std::span(filtered.get(), (size_t(width) * channels + 1) * height));
}

} // namespace obl::convert
