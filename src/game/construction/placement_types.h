#pragma once

#include <cstdint>

namespace obl::construction {

// SpawnHoarding payload for the place command. Only the low two layer bytes are read.
struct HoardingPlacementRecord {
    char m_name[32];
    char m_sprite[32];
    uint32_t m_x;
    uint32_t m_y;
    uint8_t m_usesTilePosition;
    uint8_t m_direction;
    int m_owner;
};

} // namespace obl::construction
