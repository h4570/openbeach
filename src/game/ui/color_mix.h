#pragma once

#include "core/float_to_int.h"

#include <cstdint>

// Mix: the weighted mix of two 16-bit colours of the renderer's pixel format, `(format, first, second, weight)`.
// Per channel (blue, green, red) the value is
//     trunc(second_channel * (1.0 - weight) + first_channel * weight)
// where the channels are cut out with the format's masks (the blue mask only through its low byte, shifted nowhere; green and red are shifted right with a
// 16-bit shift, so a count of 16 or more gives zero) and `trunc` is the truncation to int; the byte that comes out is clamped to 0x1f (blue, red) or to
// `greenMax` (0x3f in 565, 0x1f in 555) and the channels are put back with the format's shifts. 565 cuts green out with the shift 6 although the mask
// 0x7e0 starts at bit 5, so a mixed green has five bits and its lowest bit is lost. The arithmetic is double precision in this order of
// operations, which fixes the result bits, including the `first == second` cases that truncate one below.
namespace obl::color_mix {

// The six globals ui::renderer::InitPixelFormatGlobals writes: (masks), (shifts, only the low byte is
// used) and (green's clamp).
struct PixelFormat {
    uint16_t blueMask = 0x1f;
    uint16_t greenMask = 0x7e0;
    uint16_t redMask = 0xf800;
    uint8_t greenShift = 5;
    uint8_t redShift = 11;
    uint8_t greenMax = 0x3f;

    static PixelFormat Rgb565() { return {0x1f, 0x7e0, 0xf800, 6, 11, 0x3f}; }
    static PixelFormat Rgb555() { return {0x1f, 0x3e0, 0x7c00, 5, 10, 0x1f}; }
};

namespace detail {

// The count of a 16-bit shift is taken modulo 32, and shifting by 16 or more empties the operand.
inline uint32_t Shift16(uint32_t value, uint32_t count)
{
    count &= 31u;
    return count >= 16 ? 0u : (value & 0xffffu) >> count;
}

// The channel's float-to-int result, all 32 bits (the red channel's upper half leaks into the result, see Mix).
inline uint32_t MixChannelRaw(uint32_t first, uint32_t second, double inverse, double weight)
{
    return static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(second) * inverse + static_cast<double>(first) * weight));
}

inline uint8_t MixChannel(uint32_t first, uint32_t second, double inverse, double weight)
{
    return static_cast<uint8_t>(MixChannelRaw(first, second, inverse, weight));
}

inline uint8_t Clamp(uint8_t value, uint8_t limit)
{
    return value > limit ? limit : value;
}

} // namespace detail

inline uint32_t Mix(const PixelFormat& format, uint32_t first, uint32_t second, float weight)
{
    const double inverse = 1.0 - static_cast<double>(weight);
    const double w = static_cast<double>(weight);
    // Blue: only the low bytes of the mask and of the colours take part.
    const uint8_t blue =
            detail::Clamp(detail::MixChannel((format.blueMask & first) & 0xffu, (format.blueMask & second) & 0xffu, inverse, w), 0x1f);
    const uint8_t green = detail::Clamp(detail::MixChannel(detail::Shift16(format.greenMask & first, format.greenShift) & 0xffu,
                                                detail::Shift16(format.greenMask & second, format.greenShift) & 0xffu, inverse, w),
            format.greenMax);
    // The game widens the clamped red byte with a byte widening, which leaves the upper half of the register as the last float-to-int conversion returned it: for weights outside [0, 1]
    // (a negative or huge channel) bits 16.. of the red value reach the result after the shift, and for NaN or infinities they are zero.
    const uint32_t redRaw = detail::MixChannelRaw(detail::Shift16(format.redMask & first, format.redShift) & 0xffu,
            detail::Shift16(format.redMask & second, format.redShift) & 0xffu, inverse, w);
    const uint32_t red = (redRaw & 0xffff0000u) | detail::Clamp(static_cast<uint8_t>(redRaw), 0x1f);
    return (red << (format.redShift & 31u)) + (static_cast<uint32_t>(green) << (format.greenShift & 31u)) + blue;
}

} // namespace obl::color_mix
