#include "game/ui/sprite_preview.h"

#include "render_geometry.h"

namespace obl::sprite_preview {

float DepthPerPixel(int depthDifference, uint8_t extent)
{
    return static_cast<float>(static_cast<double>(depthDifference) / static_cast<double>(extent));
}

void ClipToUpperPart(Window* window, float progress, int height)
{
    const int bottom = render_geometry::FloatToIntWrap((1.0 - static_cast<double>(progress)) * static_cast<double>(height));
    if (window->bottom > bottom) window->bottom = bottom;
}

void ClipToLowerPart(Window* window, float progress, int height)
{
    const int top = static_cast<int>(static_cast<uint32_t>(height) -
            static_cast<uint32_t>(render_geometry::FloatToIntWrap(static_cast<double>(height) * static_cast<double>(progress))));
    if (window->top < top) {
        window->dstY =
                static_cast<int>(static_cast<uint32_t>(window->dstY) + static_cast<uint32_t>(top) - static_cast<uint32_t>(window->top));
        window->top = top;
    }
}

void ClipFlat(Window* window, float progress, int height)
{
    const int bottom = render_geometry::FloatToIntWrap((static_cast<double>(progress) - 1.0) * static_cast<double>(height));
    if (window->bottom > bottom) window->bottom = bottom;
}

} // namespace obl::sprite_preview
