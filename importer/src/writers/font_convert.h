#pragma once
#include "importer/src/writers/sprite_convert.h"
namespace obl::convert {
bool ConvertFont(const std::string& name, std::span<const uint8_t> metrics, std::span<const uint8_t> kerning,
        std::span<const uint8_t> atlas, const json_schema::Registry& schemas, const ConvertedFileSink& sink, std::string& error);
bool ConvertFonts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);
} // namespace obl::convert
