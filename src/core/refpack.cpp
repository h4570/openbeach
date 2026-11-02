#include "core/refpack.h"

namespace obl::refpack {

int64_t DecodedSize(const uint8_t* src)
{
    if (!src || (((src[0] << 8) | src[1]) & 0xFEFF) != 0x10FB) return -1;
    const uint8_t* p = src + ((src[0] & 1) ? 5 : 2);
    return (int64_t(p[0]) << 16) | (p[1] << 8) | p[2];
}

int64_t Decode(const uint8_t* src, uint8_t* dst, size_t dstCapacity)
{
    if (!src) return -1;
    const uint8_t* in = src + 2 + ((src[0] & 1) ? 3 : 0) + 3; // flags, optional compressed size, decoded size
    uint8_t* out = dst;
    uint8_t* const end = dst + dstCapacity;

    auto copyLiterals = [&](size_t n) {
        if (size_t(end - out) < n) return false;
        for (; n; --n) *out++ = *in++;
        return true;
    };

    for (;;) {
        uint32_t b0 = *in++;
        size_t literals, count, distance;
        if (b0 < 0x80) { // 2 bytes: 0-3 literals, copy 3-10, distance up to 1 KiB
            uint32_t b1 = *in++;
            literals = b0 & 3;
            count = ((b0 >> 2) & 7) + 3;
            distance = ((b0 & 0x60) << 3) + b1 + 1;
        } else if (b0 < 0xC0) { // 3 bytes: 0-3 literals, copy 4-67, distance up to 16 KiB
            uint32_t b1 = *in++, b2 = *in++;
            literals = b1 >> 6;
            count = (b0 & 0x3F) + 4;
            distance = ((b1 & 0x3F) << 8) + b2 + 1;
        } else if (b0 < 0xE0) { // 4 bytes: 0-3 literals, copy 5-1028, distance up to 128 KiB
            uint32_t b1 = *in++, b2 = *in++, b3 = *in++;
            literals = b0 & 3;
            count = ((b0 & 0x0C) << 6) + b3 + 5;
            distance = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
        } else if (b0 < 0xFC) { // 1 byte: 4-112 literals, no copy
            if (!copyLiterals(((b0 & 0x1F) + 1) * 4)) return -1;
            continue;
        } else { // stop: 0-3 trailing literals
            if (!copyLiterals(b0 & 3)) return -1;
            return out - dst;
        }
        if (!copyLiterals(literals) || size_t(out - dst) < distance || size_t(end - out) < count) return -1;
        // Byte-wise on purpose: with distance < count the copy re-reads bytes it just wrote (run-length fill).
        const uint8_t* from = out - distance;
        for (size_t i = 0; i < count; ++i) *out++ = *from++;
    }
}

} // namespace obl::refpack
