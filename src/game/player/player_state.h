#pragma once

#include "core/char16.h"

#include <cstdint>
#include <string>
#include <utility>

namespace obl::player {

inline constexpr int kNameCapacity = 64; // the loader read at most this many UTF-16 units, the terminator included

struct Player {
    std::basic_string<Char16> m_name;
    float m_cash = 0.0f; // ConstructWorld sets the start cash (25000)

    Player() = default;
    explicit Player(std::basic_string<Char16> name) : m_name(std::move(name)) {}
};

} // namespace obl::player
