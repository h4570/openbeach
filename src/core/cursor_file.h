#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Windows cursor files (`.cur`: an icon directory of type 2 whose entries carry the hot spot) decoded to straight ARGB, for
// SDL_CreateColorCursor on every system (docs/architecture/portable-host.md). The first entry is used. Supported images:
// BMP DIBs (BITMAPINFOHEADER, double height for the XOR image + AND mask) at 1, 4, 8, 24 or 32 bpp; PNG entries are rejected.
namespace obl::cursor_file {

struct Image {
    int width = 0;
    int height = 0;
    int hotX = 0;
    int hotY = 0;
    std::vector<uint32_t> argb; // width * height, top row first, 0xAARRGGBB
};

// A pixel the AND mask makes transparent is 0; an opaque one takes the XOR colour with alpha 255. The "inverted" pixel of a
// monochrome cursor (mask and colour both set) has no ARGB form and becomes opaque black. A 32 bpp image whose alpha
// channel is not all zero uses that alpha instead of the mask. False on a truncated or unsupported file.
bool Decode(const uint8_t* data, size_t size, Image& out);

// An icon file (`.ico`, directory type 1) for SDL_SetWindowIcon: the largest entry with the most bits per pixel, decoded as
// above; the hot spot is 0.
bool DecodeIcon(const uint8_t* data, size_t size, Image& out);

} // namespace obl::cursor_file
