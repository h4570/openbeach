#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"

#include "game/game/game_accessors.h"
#include "game/ui/renderer_types.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::ui {
namespace {

using ui::Renderer;
using ui::RendererSurface;

// Call counters (`call-report ui.line-draw`).
test::CallCounter g_calls("ui.line-draw");

RendererSurface* SurfaceOf(const Renderer* renderer)
{
    return ui::SurfaceAt(*renderer, renderer->m_activeSurface);
}

void DrawRendererLine(Renderer* renderer, int x0, int y0, int x1, int y1, uint32_t color)
{
    line_draw::Line line;
    if (!line_draw::Prepare(renderer->m_view[2], renderer->m_view[3], x0, y0, x1, y1, &line)) return;
    Renderer* global = game::GetRenderer(); // the loops clip against g_Renderer's size, the lock origin is g_Renderer's too
    if (!renderer->m_directMode) {
        line_draw::Draw32(line, color, renderer->m_workBuffer, renderer->m_width, static_cast<uint32_t>(global->m_width),
                static_cast<uint32_t>(global->m_height));
        return;
    }
    auto* bits = obl::ui::renderer::GetBackBufferBits(renderer);
    if (!bits) return;
    const RendererSurface* origin = SurfaceOf(global);
    line.x -= origin->m_originX;
    line.y -= origin->m_originY;
    const RendererSurface* surface = SurfaceOf(renderer);
    line_draw::Draw16(line, color, bits, static_cast<int>(static_cast<unsigned>(surface->m_pitch) >> 1),
            static_cast<uint32_t>(global->m_width), static_cast<uint32_t>(global->m_height));
    renderer->m_surfaceLocked = 0;
}

} // namespace

namespace pixel_primitives {
void DrawLine(void* renderer, int x0, int y0, int x1, int y1, uint32_t color)
{
    g_calls.Count();
    DrawRendererLine(static_cast<Renderer*>(renderer), x0, y0, x1, y1, color);
}
} // namespace pixel_primitives

} // namespace obl::ui
