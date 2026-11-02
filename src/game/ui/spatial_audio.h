#pragma once
#include <algorithm>
#include <cmath>

namespace obl::audio {
struct SpatialPosition {
    float x = 0, y = 0, z = 0;
};
struct SpatialMix {
    float left = 1, right = 1, distance = 0;
};

// Linear distance rolloff follows the game's software fallback (250..750).
// The fixed listener has +X to its right; the game never supplies orientation.
inline SpatialMix SpatialGains(SpatialPosition source, SpatialPosition listener, float nearDistance, float farDistance)
{
    const float x = source.x - listener.x, y = source.y - listener.y, z = source.z - listener.z;
    const float distance = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(distance)) return {0, 0, 0};
    nearDistance = std::max(0.0f, nearDistance);
    farDistance = std::max(nearDistance, farDistance);
    const float attenuation =
            distance <= nearDistance ? 1.0f : (distance >= farDistance ? 0.0f : (farDistance - distance) / (farDistance - nearDistance));
    const float pan = distance > 0 ? std::clamp(x / distance, -1.0f, 1.0f) : 0;
    return {attenuation * std::sqrt(1 - std::max(0.0f, pan)), attenuation * std::sqrt(1 + std::min(0.0f, pan)), distance};
}
} // namespace obl::audio
