#pragma once

#include "game/ui/renderer_types.h"

#include <cstddef>
#include <cstdint>

// The sea's animated sparkle mask and the block draw that uses it (docs/architecture/presentation-render.md).
// Pure functions over `ui::WaterAnim` and numbers; water_anim_renderer.cpp connects them to the renderer.
namespace obl::water_anim {

constexpr int kFrames = 15;
constexpr double kFramesPerSecond = 7.5;

// Frame shown at `seconds` of game time: trunc(seconds * 7.5) % 15 (the multiply is exact in double). Negative or non-finite
// times give a negative result, which Update ignores.
int FrameAt(float seconds);

// The object is not zeroed, only the four fields below.
void Construct(ui::WaterAnim& anim);
// Construct on the renderer's memory for the animation; returns it.
ui::WaterAnim* ConstructAt(void* memory);

// The sea-effects switch of the renderer.
void SetEnabled(ui::WaterAnim& anim, uint8_t enabled);

const uint8_t* GetMask(const ui::WaterAnim& anim, int column, int row);

// Decodes the frame for `seconds` into the mask when it differs from the last one. Nothing happens while not loaded (not even
// the frame bookkeeping). Returns true when the mask was rewritten.
bool Update(ui::WaterAnim& anim, float seconds);

// Memory for the frame data: the game's heap in the game, anything in tests.
struct Allocator {
    void* (*allocate)(size_t size);
    void (*release)(void* memory);
};

// Reads `path` (u32 data size, 15 u32 offsets, 15 u32 run counts, the data), replaces the old data through `heap` and marks
// the object loaded. A missing file clears the loaded flag and returns false (the old data stays). `sizeOut` receives the
// data size. The last decoded frame is not reset, so a reload keeps showing the previous mask until the frame changes.
bool Load(ui::WaterAnim& anim, const char* path, const Allocator& heap, uint32_t* sizeOut = nullptr);

// Sheet geometry of the converted water animation (converter/maps.md): 15 frames of 128 px side by side, four 128 x 128 masks
// of a frame stacked top to bottom (mask order is GetMask's quadrant order); gray 255 is on, 0 off.
constexpr int kSheetWidth = 1920;
constexpr int kSheetHeight = 512;

// Same as Load, from the decoded sheet (kSheetWidth x kSheetHeight gray bytes): each frame is stored as signed runs of at most
// 127 bytes, the form Update decodes, so the masks Update produces are the sheet's. Never fails; a short `sheet` is the
// caller's error.
void LoadSheet(ui::WaterAnim& anim, const uint8_t* sheet, const Allocator& heap);

// ---- the water block draw ----

// The source rectangle {left, top, right, bottom} handed to the renderer's clip: `rect` itself, or the whole image (width,
// height) at {left, top} when the right or bottom edge is 0.
struct SourceRect {
    int left, top, right, bottom;
};
SourceRect StartRect(const uint32_t rect[4], uint32_t imageWidth, uint32_t imageHeight);

// The mask for a block: GetMask only for useMask == 1 exactly.
const uint8_t* BlockMask(const ui::WaterAnim* anim, int column, int row, int useMask);

} // namespace obl::water_anim

namespace obl::ui {

// The 128x128 16-bit image the sea draw reads; DrawSea re-points its pixels per sea block. Constructed at start-up, destroyed by
// the exit handler.
ui::RendererImage& WaterBlockImage();
void ConstructWaterBlockImage();
int __cdecl DestroyWaterBlockImage();

} // namespace obl::ui
