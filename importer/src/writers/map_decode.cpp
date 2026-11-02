#include "importer/src/writers/map_convert.h"

#include "core/image_decode.h"
#include "formats/map_pack.h"

#include <algorithm>
#include <cstdlib>

namespace obl::convert {
namespace {
uint32_t U32(std::span<const uint8_t> b, size_t at)
{
    return b[at] | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}
bool Fail(std::string& error, const char* message)
{
    error = message;
    return false;
}
} // namespace

bool DecodeMapTerrain(std::span<const uint8_t> bytes, std::vector<uint8_t>& indices, std::string& error)
{
    if (bytes.size() != 128 * 128 * 4) return Fail(error, "map grid must contain exactly 128 x 128 words");
    indices.resize(128 * 128);
    for (size_t x = 0; x < 128; ++x)
        for (size_t y = 0; y < 128; ++y) {
            const auto value = U32(bytes, (x * 128 + y) * 4);
            const auto found = std::find(std::begin(map_pack::kTerrainFlags), std::end(map_pack::kTerrainFlags), value);
            if (found == std::end(map_pack::kTerrainFlags)) {
                error = "unrecognized terrain flag " + std::to_string(value) + " at " + std::to_string(x) + "," + std::to_string(y);
                return false;
            }
            indices[y * 128 + x] = static_cast<uint8_t>(found - std::begin(map_pack::kTerrainFlags));
        }
    return true;
}

bool DecodeMapHeight(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& samples, std::string& error)
{
    // These are sample indices, not palette colors: the map view's terrain height query reads Image::pixels directly.
    if (bytes.size() < 18 || bytes[1] != 1 || (bytes[2] != 1 && bytes[2] != 9) || bytes[16] != 8 || bytes[7] != 24 || bytes[3] ||
            bytes[4] || (bytes[17] & 0x30))
        return Fail(error, "unsupported height TGA layout");
    image::Image image;
    if (!image::DecodeTga(bytes.data(), bytes.size(), image) || image.width < 2 || image.height < 2 || image.width > 4096 ||
            image.height > 4096)
        return Fail(error, "invalid height TGA");
    width = image.width;
    height = image.height;
    samples = std::move(image.pixels);
    return true;
}

bool DecodeMapBlock(std::span<const uint8_t> bytes, std::vector<uint16_t>& words, std::string& error)
{
    // RefPack's engine decoder has no input bound. This reader bounds every command before consuming it.
    if (bytes.size() < 5 || bytes[1] != 0xfb || (bytes[0] & 0xfe) != 0x10) return Fail(error, "invalid map RefPack header");
    size_t at = (bytes[0] & 1) ? 8 : 5;
    if (bytes.size() < at) return Fail(error, "truncated map RefPack header");
    const size_t sizeAt = at - 3;
    if (((uint32_t(bytes[sizeAt]) << 16) | (bytes[sizeAt + 1] << 8) | bytes[sizeAt + 2]) != 65536)
        return Fail(error, "map RefPack block must decode to 65536 bytes");
    std::vector<uint8_t> decoded;
    decoded.reserve(65536);
    auto literals = [&](size_t count) {
        if (count > bytes.size() - at || count > 65536 - decoded.size()) return false;
        decoded.insert(decoded.end(), bytes.begin() + at, bytes.begin() + at + count);
        at += count;
        return true;
    };
    while (at < bytes.size()) {
        const uint32_t a = bytes[at++];
        size_t count = 0, distance = 0, literalCount = 0;
        if (a < 0x80) {
            if (bytes.size() - at < 1) return Fail(error, "truncated RefPack command");
            literalCount = a & 3;
            count = ((a >> 2) & 7) + 3;
            distance = ((a & 0x60) << 3) + bytes[at++] + 1;
        } else if (a < 0xc0) {
            if (bytes.size() - at < 2) return Fail(error, "truncated RefPack command");
            const auto b = bytes[at++], c = bytes[at++];
            literalCount = b >> 6;
            count = (a & 0x3f) + 4;
            distance = ((b & 0x3f) << 8) + c + 1;
        } else if (a < 0xe0) {
            if (bytes.size() - at < 3) return Fail(error, "truncated RefPack command");
            const auto b = bytes[at++], c = bytes[at++], d = bytes[at++];
            literalCount = a & 3;
            count = ((a & 0xc) << 6) + d + 5;
            distance = ((a & 0x10) << 12) + (b << 8) + c + 1;
        } else if (a < 0xfc) {
            if (!literals(((a & 0x1f) + 1) * 4)) return Fail(error, "invalid RefPack literals");
            continue;
        } else {
            if (!literals(a & 3) || decoded.size() != 65536 || at != bytes.size()) return Fail(error, "invalid RefPack end");
            words.resize(32768);
            for (size_t i = 0; i < words.size(); ++i) words[i] = uint16_t(decoded[i * 2] | (decoded[i * 2 + 1] << 8));
            return true;
        }
        if (!literals(literalCount) || distance > decoded.size() || count > 65536 - decoded.size())
            return Fail(error, "invalid RefPack backreference");
        for (size_t i = 0; i < count; ++i) decoded.push_back(decoded[decoded.size() - distance]);
    }
    return Fail(error, "missing RefPack end");
}

bool DecodeMapWater(std::span<const uint8_t> bytes, std::vector<uint8_t>& sheet, std::string& error)
{
    if (bytes.size() < 124 || U32(bytes, 0) != bytes.size() - 124) return Fail(error, "invalid water animation size");
    sheet.assign(1920 * 512, 0);
    for (size_t frame = 0; frame < 15; ++frame) {
        const auto offset = U32(bytes, 4 + frame * 4), runs = U32(bytes, 64 + frame * 4);
        if (offset > bytes.size() - 124 || runs > bytes.size() - 124 - offset) return Fail(error, "invalid water frame range");
        size_t pixel = 0;
        for (size_t i = 0; i < runs; ++i) {
            const int run = static_cast<int8_t>(bytes[124 + offset + i]);
            const size_t count = run < 0 ? -run : run;
            if (count > 65536 - pixel) return Fail(error, "water frame overflows masks");
            for (size_t j = 0; j < count; ++j, ++pixel) {
                const size_t mask = pixel / 16384, within = pixel % 16384;
                sheet[(mask * 128 + within / 128) * 1920 + frame * 128 + within % 128] = run > 0 ? 255 : 0;
            }
        }
        if (pixel != 65536) return Fail(error, "water frame does not fill four masks");
    }
    return true;
}

bool ParseMapPropAliases(std::string_view script, std::vector<MapPropAlias>& aliases, std::string& error)
{
    aliases.clear();
    // Same token boundaries and final-name rule as WorldScript::Read/Create. Commands remain in their scripts.
    size_t pos = 0;
    auto read = [&]() -> std::string {
        while (pos < script.size()) {
            if (script[pos] == ' ' || script[pos] == '\t' || script[pos] == '\r' || script[pos] == '\n') {
                ++pos;
                continue;
            }
            if (script[pos] != '/') break;
            while (pos < script.size() && script[pos] != '\n') ++pos;
        }
        if (pos == script.size()) return {};
        const size_t start = pos++;
        if (script[start] != ';')
            while (pos < script.size() && script[pos] != ';' && script[pos] != ' ' && script[pos] != '\t' && script[pos] != '\r' &&
                    script[pos] != '\n')
                ++pos;
        return std::string(script.substr(start, pos - start));
    };
    for (auto token = read(); !token.empty(); token = read()) {
        if (token != "Create") continue;
        if (read() != "Scenery") continue;
        const auto definition = read();
        if (definition.empty() || definition == ";") return Fail(error, "missing scenery definition");
        const auto x = read();
        if (LowerAscii(x) != "random") {
            if (read().empty()) return Fail(error, "missing scenery coordinate");
            if (std::atoi(x.c_str()) >= 0 && read().empty()) return Fail(error, "missing scenery direction");
        }
        std::string name;
        for (token = read(); token != ";"; token = read()) {
            if (token.empty()) return Fail(error, "unterminated Create Scenery");
            if (token == "Number") {
                if (read().empty()) return Fail(error, "missing scenery count");
            } else
                name = token;
        }
        if (name.empty()) return Fail(error, "empty scenery name");
        aliases.push_back({name, definition});
    }
    return true;
}
} // namespace obl::convert
