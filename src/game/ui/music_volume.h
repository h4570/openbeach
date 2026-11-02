#pragma once

namespace obl::audio {

inline float ClampVolume(float value)
{
    return value >= 0 ? (value <= 1 ? value : 1) : 0;
}

inline int MilesVolume(float value)
{
    int volume = static_cast<int>(ClampVolume(value) * 127.0f);
    return volume < 3 ? 0 : volume;
}

} // namespace obl::audio
