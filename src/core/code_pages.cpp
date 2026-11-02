#include "core/code_pages.h"

#include <unordered_map>

namespace obl::code_pages {
namespace {

// Bytes 0x80..0xFF of each code page. 65533 (U+FFFD) marks the bytes the code page leaves undefined.
constexpr unsigned kWestern[] = {8364, 65533, 8218, 402, 8222, 8230, 8224, 8225, 710, 8240, 352, 8249, 338, 65533, 381, 65533, 65533, 8216,
        8217, 8220, 8221, 8226, 8211, 8212, 732, 8482, 353, 8250, 339, 65533, 382, 376, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169,
        170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195,
        196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221,
        222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
        248, 249, 250, 251, 252, 253, 254, 255};
constexpr unsigned kPolish[] = {8364, 65533, 8218, 65533, 8222, 8230, 8224, 8225, 65533, 8240, 352, 8249, 346, 356, 381, 377, 65533, 8216,
        8217, 8220, 8221, 8226, 8211, 8212, 65533, 8482, 353, 8250, 347, 357, 382, 378, 160, 711, 728, 321, 164, 260, 166, 167, 168, 169,
        350, 171, 172, 173, 174, 379, 176, 177, 731, 322, 180, 181, 182, 183, 184, 261, 351, 187, 317, 733, 318, 380, 340, 193, 194, 258,
        196, 313, 262, 199, 268, 201, 280, 203, 282, 205, 206, 270, 272, 323, 327, 211, 212, 336, 214, 215, 344, 366, 218, 368, 220, 221,
        354, 223, 341, 225, 226, 259, 228, 314, 263, 231, 269, 233, 281, 235, 283, 237, 238, 271, 273, 324, 328, 243, 244, 337, 246, 247,
        345, 367, 250, 369, 252, 253, 355, 729};

} // namespace

unsigned CodePoint(bool polish, unsigned byte)
{
    if (byte < 0x80) return byte;
    return (polish ? kPolish : kWestern)[byte - 0x80];
}

namespace {

// The inverse table of one code page: an undefined byte maps back to its own C1 code point.
std::unordered_map<char16_t, uint8_t> BuildInverse(bool polish)
{
    std::unordered_map<char16_t, uint8_t> table;
    for (unsigned byte = 0; byte < 256; ++byte) {
        const unsigned code = CodePoint(polish, byte);
        table.emplace(static_cast<char16_t>(code == kUndefinedCodePoint ? byte : code), static_cast<uint8_t>(byte));
    }
    return table;
}

const std::unordered_map<char16_t, uint8_t>& Inverse(bool polish)
{
    static const auto western = BuildInverse(false);
    static const auto polishTable = BuildInverse(true);
    return polish ? polishTable : western;
}

} // namespace

bool ByteOf(bool polish, char16_t unit, uint8_t& byte)
{
    const auto& table = Inverse(polish);
    const auto found = table.find(unit);
    if (found == table.end()) return false;
    byte = found->second;
    return true;
}

bool Encode(bool polish, std::u16string_view text, std::string& bytes)
{
    bytes.clear();
    bytes.reserve(text.size());
    for (char16_t unit : text) {
        uint8_t byte = 0;
        if (!ByteOf(polish, unit, byte)) return false;
        bytes.push_back(static_cast<char>(byte));
    }
    return true;
}

} // namespace obl::code_pages
