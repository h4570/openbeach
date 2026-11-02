#pragma once

#include "importer/src/writers/sprite_convert.h"

namespace obl::convert {

bool DecodeFontAtlas(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& pixels, std::string& error);

// PaletteModifiers.bin must hold exactly twelve RGB tables of 64 entries.
bool ConvertPaletteModifiers(
        std::span<const uint8_t> bytes, const json_schema::Registry& schemas, const ConvertedFileSink& sink, std::string& error);

bool ConvertAssetImage(std::span<const uint8_t> bytes, bool haze, bool cursor, std::vector<uint8_t>& png, std::string& error);

// Installation only. Writes sprites/palette_modifiers.*, images/screens/*.png,
// images/cursor.{png,json}, images/haze/0..5.png; validates all JSON before writing.
// Installation PNGs preserve engine depth, pixel indices and palettes (unlike the RGBA preview helper above).
bool ConvertImages(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);

} // namespace obl::convert
