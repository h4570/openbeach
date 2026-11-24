#pragma once

#include <cstdint>
#include <string_view>

// The names of the physical keys (core/key_code.h) as scripts see them: the enumerator's name, "F5", "LeftShift", "Keypad7".
namespace obl::mods {

// The key's name; empty for a code without one.
std::string_view KeyName(uint8_t code);

} // namespace obl::mods
