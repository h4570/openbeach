#pragma once

// The converted toning tables (`sprites/palette_modifiers.json` and `.png`): twelve tables of 64 colours that the engine
// mixes into a guest's palette (game/ui/palette_modifiers.h). The importer writes them, the game reads them here.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace obl::palette_modifiers {

inline constexpr int kTableCount = 12;
inline constexpr int kTableEntries = 64;
inline constexpr size_t kFileBytes = kTableCount * kTableEntries * 3;

// `rgb` (kTableCount * kTableEntries RGB8 triples) as the engine's 16-bit colours: RGB565, or RGB555 when `rgb565` is false.
void ConvertTables(const uint8_t* rgb, bool rgb565, uint16_t* tables);

// Reads the tables of the pack layers under `pack` into `tables` (kTableCount * kTableEntries entries).
bool LoadPack(const std::filesystem::path& pack, bool rgb565, uint16_t* tables, std::string& error);

} // namespace obl::palette_modifiers
