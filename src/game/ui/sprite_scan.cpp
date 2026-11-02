#include "game/ui/sprite_scan.h"

namespace obl::sprite_scan {

bool FindFirstOpaque(const uint8_t* frame, int width, int height, Opaque* result)
{
    const uint8_t* p = frame;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width;) {
            const int control = static_cast<int8_t>(*p++);
            if (control < 0) {
                column -= control;
                continue;
            }
            p += control;
            column += control;
            if (*p != 0) {
                result->x = column;
                result->y = row;
                return true;
            }
        }
    }
    return false;
}

bool HitTest(const uint8_t* frame, int width, int x, int y, bool maskOnly)
{
    const uint8_t* p = frame;
    for (; y > 0; --y) {
        for (int column = 0; column < width;) {
            const int control = static_cast<int8_t>(*p++);
            if (control < 0) {
                column -= control;
            } else {
                column += control;
                if (!maskOnly) p += control;
            }
        }
    }
    if (x <= 0) return false;
    for (int column = 0;;) {
        const int control = static_cast<int8_t>(*p++);
        if (control >= 0) {
            column += control;
            if (!maskOnly) p += control;
            if (x <= column) return true;
        } else {
            column -= control;
            if (x <= column) return false;
        }
    }
}

} // namespace obl::sprite_scan
