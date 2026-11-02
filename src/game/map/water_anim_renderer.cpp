#include "game/map/water_anim.h"

#include "core/call_counter.h"
#include "core/log.h"
#include "game/game/diagnostics.h"
#include "game/map/grid_lifecycle.h"
#include "game/ui/blit.h"
#include "game/ui/image.h"
#include "formats/map_pack.h"
#include "game/ui/renderer_api.h"

#include <cstdint>
#include <string>
#include <vector>

// The renderer's side of the sea animation (docs/architecture/presentation-render.md): loading the converted sheet,
// the per-frame update, the sea-effects switch and the draw of one 128 x 128 water block with the animation's mask.
namespace obl::ui {
namespace {

using ui::Renderer;
using ui::RendererImage; // the static 128x128 water image; the sea draw re-points m_pixels at each block
using ui::WaterAnim;

// `tests/water_anim.txt` reads these: the goldens are optional, the counts show the sea was updated and drawn.
test::CallCounter g_updateCalls("map.water-update");
test::CallCounter g_drawCalls("map.water-draw");

const water_anim::Allocator kHeap{[](size_t size) { return ::operator new(size); }, [](void* block) { ::operator delete(block); }};

// The engine names the animation by its file; the sheet of the same animation comes from the pack. A sheet that does not
// load is a failed load, which the world construction reports with the path.
uint32_t LoadSheet(WaterAnim* anim, const char* path)
{
    std::vector<uint8_t> sheet;
    std::string error;
    if (!map_pack::ReadWaterSheet(map::PackDirectory(), path, sheet, error)) {
        anim->m_loaded = 0;
        game::FatalError("Couldn't load the water animation %s. %s", path, error.c_str());
        return 0;
    }
    water_anim::LoadSheet(*anim, sheet.data(), kHeap);
    return 1;
}

// What the blitter gets after clipping.
struct BlockPlan {
    bool draws = false;
    int x = 0, y = 0, left = 0, top = 0, right = 0, bottom = 0;
    bool half = false;
    const uint8_t* mask = nullptr;
    const uint32_t* source = nullptr;
    uint32_t sourcePitch = 0;
    int destPitch = 0;
};

BlockPlan PlanBlock(Renderer* target, int column, int row, const int* pos, const uint32_t* rect, const RendererImage* image, int useMask)
{
    BlockPlan plan;
    plan.x = pos[0];
    plan.y = pos[1];
    const water_anim::SourceRect start = water_anim::StartRect(rect, uint32_t(image->m_width), uint32_t(image->m_height));
    plan.left = start.left;
    plan.top = start.top;
    plan.right = start.right;
    plan.bottom = start.bottom;
    plan.draws = ui::renderer::ClipRect(target, &plan.x, &plan.y, &plan.left, &plan.top, &plan.right, &plan.bottom) != 0;
    if (!plan.draws) return plan;
    plan.mask = water_anim::BlockMask(target->m_waterAnim, column, row, useMask);
    plan.half = target->m_zoomLevel == 2;
    plan.source = reinterpret_cast<const uint32_t*>(image->m_pixels); // the mapped block: 32-bit samples
    plan.sourcePitch = uint32_t(image->m_width);
    plan.destPitch = target->m_width;
    return plan;
}

void ExecuteBlock(const BlockPlan& plan, uint32_t* dest)
{
    if (!plan.draws) return;
    const uint32_t width = static_cast<uint32_t>(plan.right - plan.left);
    const uint32_t height = static_cast<uint32_t>(plan.bottom - plan.top);
    if (plan.half) {
        ui::pixel_primitives::WaterHalfRes(plan.source, plan.sourcePitch, dest, plan.destPitch, static_cast<uint32_t>(plan.left),
                static_cast<uint32_t>(plan.top), plan.x, plan.y, width, height, plan.mask);
    } else {
        ui::pixel_primitives::WaterFullRes(plan.source, plan.sourcePitch, dest, plan.destPitch, static_cast<uint32_t>(plan.left),
                static_cast<uint32_t>(plan.top), plan.x, plan.y, static_cast<uint16_t>(width), static_cast<uint16_t>(height), plan.mask);
    }
}

} // namespace

uint32_t ui::renderer::LoadWaterAnim(void* target, const char* path)
{
    return LoadSheet(static_cast<Renderer*>(target)->m_waterAnim, path);
}

void ui::renderer::UpdateWaterAnim(void* target, float seconds)
{
    g_updateCalls.Count();
    water_anim::Update(*static_cast<Renderer*>(target)->m_waterAnim, seconds);
}

void ui::renderer::SetSeaEffects(void* target, uint32_t enabled)
{
    water_anim::SetEnabled(*static_cast<Renderer*>(target)->m_waterAnim, static_cast<uint8_t>(enabled));
}

void ui::renderer::DrawWaterBlock(void* target, int column, int row, int* pos, uint32_t* rect, void* image, int, int, int useMask)
{
    g_drawCalls.Count();
    auto* view = static_cast<Renderer*>(target);
    ExecuteBlock(PlanBlock(view, column, row, pos, rect, static_cast<RendererImage*>(image), useMask), view->m_workBuffer);
}

} // namespace obl::ui

namespace obl::water_anim {

ui::WaterAnim* ConstructAt(void* memory)
{
    auto* anim = static_cast<ui::WaterAnim*>(memory);
    Construct(*anim);
    return anim;
}

} // namespace obl::water_anim

namespace obl::ui {
namespace {

ui::RendererImage g_waterBlockImage{};

} // namespace

ui::RendererImage& WaterBlockImage()
{
    return g_waterBlockImage;
}

void ConstructWaterBlockImage()
{
    ui::images::Construct16(&g_waterBlockImage, 0x80, 0x80);
}

int __cdecl DestroyWaterBlockImage()
{
    OBL_LOG("startup: exit handler destroys the water block image");
    ui::images::Destruct(&g_waterBlockImage);
    return 0;
}

} // namespace obl::ui
