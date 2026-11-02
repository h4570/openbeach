#include "game/ui/sprite_animation.h"

#include "core/det_math.h"

#include "render_geometry.h"

#include <array>
#include <cmath>
#include <cstring>

namespace obl::sprite_animation {
namespace {

constexpr double kTwoPi = static_cast<double>(6.2831855f);

uint32_t BitsOf(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float FloatOf(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

Step Advance(float startTime, float time, int type, int fps, int frame, int frameCount)
{
    Step step{startTime, frame, false, false};
    if (startTime > time) step.startTime = time;
    const double elapsed = static_cast<double>(time) - static_cast<double>(step.startTime);
    const int ticks = render_geometry::FloatToIntWrap(static_cast<double>(fps) * elapsed);
    switch (type) {
    case kCountdown: {
        const int left = static_cast<int>(static_cast<uint32_t>(frameCount) - static_cast<uint32_t>(ticks));
        step.frame = left > 0 ? left - 1 : 0;
        break;
    }
    case kPlayOnce: step.frame = ticks >= frameCount ? frameCount - 1 : ticks; break;
    case kExternalFrame: step.badFrame = frame >= frameCount && frame >= 0; break;
    default:
        step.frame = frameCount > 0 ? ticks % frameCount : 0;
        step.wrapped = step.frame < frame;
        break;
    }
    return step;
}

bool StampsAnimation(float time)
{
    return time > 0.0f;
}

double Duration(int frameCount, int fps)
{
    return static_cast<double>(frameCount) / static_cast<double>(fps);
}

int DirectionFromVector(float x, float y, int directionCount)
{
    if (directionCount <= 0) return 0;
    const float t = static_cast<float>(static_cast<double>(y) * -1.0);
    double angle = 0.0;
    if (t < 1.0f && !(t <= -1.0f)) angle = det_math::Acos(static_cast<double>(t));
    if (!(x >= 0.0f)) // `x < 0`, also taken for NaN
        angle = kTwoPi - angle;
    int index = render_geometry::FloatToIntWrap(angle / (kTwoPi / static_cast<double>(2 * directionCount)));
    if (index % 2 != 0) ++index;
    int direction = index / 2 % directionCount;
    if (direction == 0) {
        if (y > 0.0f) direction = 4;
    } else if (direction == 4) {
        if (!(y >= 0.0f)) direction = 0;
    }
    return direction < directionCount ? direction : directionCount - 1;
}

const uint32_t* SqrtMantissaTable()
{
    constexpr uint32_t kMantissaMask = (1u << 23) - 1;
    static const std::array<uint32_t, kSqrtMantissaTableSize> table = [] {
        std::array<uint32_t, kSqrtMantissaTableSize> t{};
        for (uint32_t i = 0; i < 0x8000; ++i) {
            // load of the float with exponent 0 / 1 and the 15 mantissa bits i << 8, square root (53 bit), store as float.
            const float one = FloatOf((i | 0x3f8000u) << 8);
            const float two = FloatOf((i | 0x400000u) << 8);
            t[0x8000 + i] = BitsOf(static_cast<float>(std::sqrt(static_cast<double>(one)))) & kMantissaMask;
            t[i] = BitsOf(static_cast<float>(std::sqrt(static_cast<double>(two)))) & kMantissaMask;
        }
        return t;
    }();
    return table.data();
}

float FastSqrt(float value)
{
    const uint32_t bits = BitsOf(value);
    if (bits == 0) return 0.0f;
    const uint32_t exponent = ((bits + 0xc0800000u) >> 1) + 0x3f800000u;
    return FloatOf(SqrtMantissaTable()[(bits >> 8) & 0xffff] | (exponent & 0x7f800000u));
}

Vector2 FaceVector(float targetX, float targetY, float fromX, float fromY)
{
    // dy and dx are rounded to float for the multiplications, but dy itself multiplies its float copy at full
    // precision (store keeps the register).
    const double dx = static_cast<double>(targetX) - static_cast<double>(fromX);
    const double dy = static_cast<double>(targetY) - static_cast<double>(fromY);
    const float dxf = static_cast<float>(dx), dyf = static_cast<float>(dy);
    const float squared = static_cast<float>(dy * static_cast<double>(dyf) + static_cast<double>(dxf) * static_cast<double>(dxf));

    const float root = FastSqrt(squared);
    // 1/sqrt: integer-subtraction guess, then x * (2 - root * x).
    const float guess = FloatOf(0x7f000000u - BitsOf(root));
    const float inverse = static_cast<float>((2.0 - static_cast<double>(root) * static_cast<double>(guess)) * static_cast<double>(guess));
    return {static_cast<float>(static_cast<double>(inverse) * static_cast<double>(dxf)),
            static_cast<float>(static_cast<double>(inverse) * static_cast<double>(dyf))};
}

} // namespace obl::sprite_animation
