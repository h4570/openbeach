#pragma once

#include "core/ascii_case.h"
#include <cstdint>

namespace obl::sprite_animation {
inline constexpr int kCountdown = 1; // frames - elapsed frames, played once from the last frame down to 0
inline constexpr int kPlayOnce = 2; // elapsed frames, stops on the last frame
inline constexpr int kExternalFrame = 3; // the frame is set from outside; the clock does not touch it
// every other value loops: elapsed frames modulo the frame count

struct Step {
    float startTime;
    int frame;
    bool wrapped; // looping animation whose new frame is below the old one: UpdateAnim plays the sprite sound
    bool badFrame;
};
Step Advance(float startTime, float time, int type, int fps, int frame, int frameCount);
// Only positive times update the last-use stamp.
bool StampsAnimation(float time);
double Duration(int frameCount, int fps);

// Name of the animation SetAnimByName falls back to.
inline constexpr char kFallbackAnimName[] = "walk_normal";

// SetAnimByName's lookup: the first animation whose name matches `name`, else the first called "walk_normal",
// else animation 0. `nameAt(i)` returns the name of animation `i`.
template<class NameAt> int FindAnim(int animCount, NameAt&& nameAt, const char* name)
{
    for (int i = 0; i < animCount; ++i)
        if (ascii::EqualsNoCase(nameAt(i), name)) return i;
    for (int i = 0; i < animCount; ++i)
        if (ascii::EqualsNoCase(nameAt(i), kFallbackAnimName)) return i;
    return 0;
}

// SetDirectionFromVector: the facing (0 .. directionCount-1) of a unit vector, angle = acos(-y), mirrored for
// x < 0 and quantised to `directionCount` sectors (rounded to the nearest). A vector whose sector falls on the
// first or opposite axis direction (0 or 4) is resolved by the sign of y; missing directions clamp to the last one.
int DirectionFromVector(float x, float y, int directionCount);

struct Vector2 {
    float x, y;
};

// Normalize the vector from the source point to the target using the animation math.
Vector2 FaceVector(float targetX, float targetY, float fromX, float fromY);

const uint32_t* SqrtMantissaTable();
inline constexpr int kSqrtMantissaTableSize = 0x10000;

float FastSqrt(float value);

} // namespace obl::sprite_animation
