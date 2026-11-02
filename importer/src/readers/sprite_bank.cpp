#include "importer/src/readers/sprite_bank.h"

#include "formats/sprite_pack.h"

#include <algorithm>
#include <cstring>

namespace obl::convert {

namespace {

class Cursor {
public:
    explicit Cursor(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    bool Has(size_t count) const { return count <= bytes_.size() - pos_; }
    size_t Position() const { return pos_; }
    size_t Remaining() const { return bytes_.size() - pos_; }

    bool Bytes(void* out, size_t count)
    {
        if (!Has(count)) return false;
        if (count) std::memcpy(out, bytes_.data() + pos_, count);
        pos_ += count;
        return true;
    }
    bool U8(uint8_t& v) { return Bytes(&v, 1); }
    bool U16(uint16_t& v)
    {
        uint8_t b[2];
        if (!Bytes(b, 2)) return false;
        v = static_cast<uint16_t>(b[0] | (b[1] << 8));
        return true;
    }
    bool U32(uint32_t& v)
    {
        uint8_t b[4];
        if (!Bytes(b, 4)) return false;
        v = b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }
    // A length byte, then that many bytes (the stored NUL included); the name ends at the first NUL as the game reads it.
    bool Name(std::string& out)
    {
        uint8_t length;
        std::string bytes;
        if (!U8(length)) return false;
        bytes.resize(length);
        if (!Bytes(bytes.data(), length)) return false;
        out = bytes.substr(0, bytes.find('\0'));
        return true;
    }

private:
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0;
};

bool ParseAnim(Cursor& in, SpriteAnimRecord& a)
{
    uint16_t hx, hy;
    if (!in.Name(a.name) || !in.U16(a.frames) || !in.U8(a.directions) || !in.U16(a.width) || !in.U16(a.height) || !in.U16(hx) ||
            !in.U16(hy) || !in.U32(a.offset) || !in.U32(a.size) || !in.U16(a.uniqueFrames))
        return false;
    a.hotspotX = static_cast<int16_t>(hx);
    a.hotspotY = static_cast<int16_t>(hy);
    return true;
}

} // namespace

bool ParseSpriteBank(std::span<const uint8_t> header, SpriteBank& bank, std::string& error)
{
    Cursor in(header);
    char stamp[20];
    uint32_t count = 0;
    if (!in.Bytes(stamp, sizeof(stamp)) || !in.U32(bank.version) || !in.U32(count)) {
        error = "sprite header: truncated file header";
        return false;
    }
    bank.stamp.assign(stamp, strnlen(stamp, sizeof(stamp)));
    // Each record takes at least 13 bytes, so a count beyond that is damage, not a large bank.
    if (count > in.Remaining() / 13) {
        error = "sprite header: implausible sprite count " + std::to_string(count);
        return false;
    }
    bank.sprites.assign(count, {});
    for (uint32_t i = 0; i < count; ++i) {
        SpriteRecord& s = bank.sprites[i];
        const std::string where = "sprite header: record " + std::to_string(i);
        uint32_t palettes = 0, anims = 0;
        if (!in.Name(s.name) || !in.U32(s.type)) {
            error = where + ": truncated";
            return false;
        }
        // Type 11 is known to the game's loader but absent from every data revision; refused until data has it.
        if (s.type != 1 && s.type != 2 && s.type != 3 && s.type != 5 && s.type != 6) {
            error = where + " (" + s.name + "): unsupported sprite type " + std::to_string(s.type);
            return false;
        }
        if ((s.type == 5 && !in.Bytes(s.extra.data(), s.extra.size())) || !in.U32(palettes) || palettes > in.Remaining() / 768) {
            error = where + " (" + s.name + "): truncated palettes";
            return false;
        }
        s.palettes.resize(palettes);
        for (auto& palette : s.palettes) in.Bytes(palette.data(), palette.size());
        if (!in.U32(anims) || anims > in.Remaining() / 22) {
            error = where + " (" + s.name + "): truncated animations";
            return false;
        }
        s.anims.resize(anims);
        for (auto& a : s.anims)
            if (!ParseAnim(in, a)) {
                error = where + " (" + s.name + "): truncated animation";
                return false;
            }
    }
    if (in.Remaining()) {
        error = "sprite header: " + std::to_string(in.Remaining()) + " bytes after the last record";
        return false;
    }
    return true;
}

bool DecodeSpriteAnim(const SpriteAnimRecord& anim, std::span<const uint8_t> block, bool mask, std::vector<uint16_t>& frameMap,
        int& runLength, const SpriteFrameSink& sink, std::string& error)
{
    runLength = 0;
    int longestRun = 0;
    std::vector<uint8_t> encoded;
    const std::string where = "animation " + anim.name + ": ";
    if (block.size() != anim.BlockSize()) {
        error = where + "block size " + std::to_string(block.size()) + ", expected " + std::to_string(anim.BlockSize());
        return false;
    }
    if (anim.frames == 0 || anim.directions == 0 || anim.uniqueFrames == 0 || anim.width == 0 || anim.height == 0) {
        error = where + "empty frame count, direction count or size";
        return false;
    }
    Cursor in(block);
    const size_t logical = static_cast<size_t>(anim.frames) * anim.directions;
    frameMap.resize(logical);
    for (auto& index : frameMap) {
        in.U16(index);
        if (index >= anim.uniqueFrames) {
            error = where + "frame index " + std::to_string(index) + " outside " + std::to_string(anim.uniqueFrames) + " unique frames";
            return false;
        }
    }
    std::vector<uint32_t> offsets(anim.uniqueFrames);
    for (auto& offset : offsets) in.U32(offset);
    const std::span<const uint8_t> data = block.subspan(in.Position());

    const size_t w = anim.width, h = anim.height;
    std::vector<uint8_t> frame(w * h * 2);
    for (size_t u = 0; u < offsets.size(); ++u) {
        size_t pos = offsets[u];
        const size_t start = pos;
        int frameRunLength = 0; // the length of the runs followed by one of the same kind: the encoder's limit
        std::fill(frame.begin(), frame.end(), uint8_t{0});
        uint8_t* pixels = frame.data();
        for (size_t y = 0; y < h; ++y) {
            size_t x = 0;
            int previous = 0;
            while (x < w) {
                if (pos >= data.size()) {
                    error = where + "frame " + std::to_string(u) + " runs past the pixel data";
                    return false;
                }
                const int run = static_cast<int8_t>(data[pos++]);
                const size_t length = static_cast<size_t>(run < 0 ? -run : run);
                if (length == 0 || x + length > w) {
                    error = where + "frame " + std::to_string(u) + " row " + std::to_string(y) + ": a run of " + std::to_string(run) +
                            " at x " + std::to_string(x) + " does not fit the width " + std::to_string(w);
                    return false;
                }
                longestRun = std::max(longestRun, static_cast<int>(length));
                if (previous != 0 && (previous > 0) == (run > 0)) {
                    const int limit = previous < 0 ? -previous : previous;
                    if (frameRunLength && frameRunLength != limit) {
                        error = where + "frame " + std::to_string(u) + " splits runs at " + std::to_string(frameRunLength) + " and at " +
                                std::to_string(limit) + " pixels";
                        return false;
                    }
                    frameRunLength = limit;
                }
                previous = run;
                if (run > 0) {
                    if (!mask && length > data.size() - pos) {
                        error = where + "frame " + std::to_string(u) + " runs past the pixel data";
                        return false;
                    }
                    uint8_t* p = pixels + (y * w + x) * 2;
                    for (size_t i = 0; i < length; ++i, p += 2) {
                        p[0] = mask ? 0 : data[pos++];
                        p[1] = 255;
                    }
                }
                x += length;
            }
        }
        // The engine re-encodes the frames from the sheets (sprite_pack::EncodeFrame); refuse a frame it would not rebuild byte for byte.
        encoded.clear();
        sprite_pack::EncodeFrame(
                frame, anim.width, anim.height, mask, frameRunLength ? frameRunLength : sprite_pack::kDefaultRunLength, encoded);
        if (encoded.size() != pos - start ||
                !std::equal(encoded.begin(), encoded.end(), data.begin() + static_cast<std::ptrdiff_t>(start))) {
            error = where + "frame " + std::to_string(u) + " is not in the run form the engine rebuilds";
            return false;
        }
        if (frameRunLength) {
            if (runLength && runLength != frameRunLength) {
                error = where + "frames split runs at " + std::to_string(runLength) + " and at " + std::to_string(frameRunLength) +
                        " pixels";
                return false;
            }
            runLength = frameRunLength;
        }
        if (!sink(static_cast<int>(u), frame, error)) return false;
    }
    if (!runLength) runLength = sprite_pack::kDefaultRunLength;
    if (longestRun > runLength) {
        error = where + "a run of " + std::to_string(longestRun) + " pixels where runs split at " + std::to_string(runLength);
        return false;
    }
    return true;
}

} // namespace obl::convert
