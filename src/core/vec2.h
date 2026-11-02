#pragma once

#include <cstdint>
#include <cstring>

// 2D vector helpers (add, multiply, divide). Pure float math, unit-tested.
namespace obl::vec2 {

struct Vec2 {
    float x;
    float y;
};

inline Vec2 Add(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}

inline Vec2 Multiply(Vec2 v, float scalar)
{
    return {scalar * v.x, scalar * v.y};
}

// The game does not divide: it builds a reciprocal estimate by subtracting the divisor's bit pattern from
// 0x7f000000 and refines it with one Newton step r*(2 - d*r), then multiplies. The result is therefore only
// approximately v/d and must be reproduced bit for bit, not "fixed" into a real division.
inline float ReciprocalEstimate(float divisor)
{
    uint32_t bits;
    std::memcpy(&bits, &divisor, sizeof(bits));
    bits = 0x7f000000u - bits;
    float r;
    std::memcpy(&r, &bits, sizeof(r));
    // Wider registers keep the intermediates unrounded (only the final store narrows to float), so they are
    // computed in double; float arithmetic here would differ from the game by one ulp in ~1/3 of the cases.
    double refined = (2.0 - static_cast<double>(divisor) * r) * r;
    return static_cast<float>(refined);
}

inline Vec2 Divide(Vec2 v, float divisor)
{
    float r = ReciprocalEstimate(divisor);
    return {r * v.x, r * v.y};
}

} // namespace obl::vec2
