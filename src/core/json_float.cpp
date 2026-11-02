#include "core/json_float.h"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace obl::json_float {

nlohmann::ordered_json Number(float value)
{
    char text[32];
    const auto result = std::to_chars(text, text + sizeof(text) - 1, value);
    *result.ptr = 0;
    // The shortest float decimal read as a double prints back as the same digits.
    return std::strtod(text, nullptr);
}

nlohmann::ordered_json Bits(uint32_t bits)
{
    const float value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) {
        char text[16];
        std::snprintf(text, sizeof(text), "f32:%08x", bits);
        return text;
    }
    return Number(value);
}

std::optional<uint32_t> ParseBits(const nlohmann::json& value)
{
    if (value.is_number()) return std::bit_cast<uint32_t>(static_cast<float>(value.get<double>()));
    if (!value.is_string()) return std::nullopt;
    const std::string& text = value.get_ref<const std::string&>();
    if (text.size() != 12 || !text.starts_with("f32:")) return std::nullopt;
    uint32_t bits = 0;
    const auto result = std::from_chars(text.data() + 4, text.data() + text.size(), bits, 16);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return std::nullopt;
    return bits;
}

} // namespace obl::json_float
