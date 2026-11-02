#include "game/ui/sprite_anim.h"
#include "game/ui/blit.h"
#include "game/ui/sprite_instance_bake.h"

#include "game/game/game_accessors.h"
#include "game/ui/map_view.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_snapshot.h"
#include "game/ui/sprite_bake.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite_preview.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "game/ui/map_view_math.h"

namespace obl::ui {
namespace {

using sprite_bake::PixelRect;
using ui::MapView;
using ui::map_snapshot::GameEnv;
using ui::map_snapshot::Rng;
using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using Sprite = ui::Sprite;

constexpr int kTypeBuilding = 5;

test::CallCounter g_overlayScene("ui.sprite-overlay-scene");
test::CallCounter g_groundScene("ui.sprite-ground-scene");
// Call counters (`call-report ui.sprite-bake`), per function.

// ---- the Envs -----------------------------------------------------------------------------------------------------------------

uint8_t IsoMask(int index)
{
    return sprite_bake::IsoTileMask(index);
}

// The game's block Env plus the tile mask BakeBuildingRectColor gates its stores with.
class GameBakeEnv : public GameEnv {
public:
    using GameEnv::GameEnv;
    uint8_t Mask(int index) const { return IsoMask(index); }
};

// ---- the bodies -----------------------------------------------------------------------------------------------------------

Anim* CurrentAnim(const Instance* instance)
{
    return instance->m_sprite->m_anims[instance->m_animIndex];
}

const uint8_t* FrameOf(const Instance* instance, Anim* anim)
{
    return anim->GetFrame(instance->m_frameIndex, instance->m_direction);
}

PixelRect AsRect(const int* rect)
{
    return {rect[0], rect[1], rect[2], rect[3]};
}

// BakeBuildingDepth: animation 0 is the colour frame, animation 1 the depth frame (fetched in that order); a sprite that is not a building does nothing.
template<class Env>
void RunDepth(
        Env& env, Instance* instance, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, float step, uint32_t depth)
{
    const Sprite* sprite = instance->m_sprite;
    if (sprite->m_type != kTypeBuilding) return;
    const sprite_bake::DepthFrames frames{FrameOf(instance, sprite->m_anims[0]), FrameOf(instance, sprite->m_anims[1]),
            sprite->m_palettes[0], CurrentAnim(instance)->m_width};
    sprite_bake::BakeDepth(env, rect, blockX, blockY, offsetX, offsetY, step, depth, frames);
}

// The frame, palette and blend masks of the two colour writers: the current animation's frame (no type check), palette 0.
sprite_bake::ColourFrame ColourFrameOf(const Instance* instance)
{
    Anim* anim = CurrentAnim(instance);
    sprite_bake::ColourFrame frame{FrameOf(instance, anim), instance->m_sprite->m_palettes[0], anim->m_width, anim->m_hotspotY, 0, 0};
    frame.halfMask = ui::pixel_primitives::GetMaskHalf();
    frame.quarterMask = ui::pixel_primitives::GetMaskQuarter();
    return frame;
}

template<class Env>
void RunColour(Env& env, Instance* instance, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth)
{
    sprite_bake::BakeColour(env, rect, blockX, blockY, offsetX, offsetY, depth, ColourFrameOf(instance));
}

template<class Env>
void RunRectColour(Env& env, Instance* instance, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth,
        int maskX, int maskY)
{
    sprite_bake::BakeRectColour(env, rect, blockX, blockY, offsetX, offsetY, depth, maskX, maskY, ColourFrameOf(instance));
}

// What BakeBuilding / BakeBuildingRect hand to their writer, with the effects they have on the way.
enum class Writer { None, Depth, Colour, RectColour, MaskedColour, GhostDepth, GhostColour, GroundTile };
struct Plan {
    bool active = false;
    sprite_bake::WriterPosition position{};
    float step = 0.0f; // BakeBuilding only
    uint32_t depth = 0;
    int maskX = 0, maskY = 0; // BakeBuildingRect only
    Writer writer = Writer::None; // 12e wrappers choose a writer according to their clip
};

// The sprite frame's map position: where the world point lands on the screen plus the frame offset and the animation's hotspot (the first two steps both
// bakers share) - the screen point comes from the game's own WorldToScreen and GetDrawOffsetX.
void FrameOrigin(Instance* instance, const float* world, int* x, int* y)
{
    MapView* view = GetMapView();
    int screen[2];
    view->ProjectWorldPoint(screen, world);
    *x = screen[0] + instance->GetDrawOffsetX();
    *y = screen[1] + CurrentAnim(instance)->m_hotspotY;
}

// BakeBuilding up to the writer call. A building that is not on its first animation is put back on it (animation and frame 0), the footprint is measured,
// both building animations count as used now, and the clip rectangle - rewritten in place - becomes frame coordinates.
void PlanBuilding(Instance* instance, int* rect, const float* world, int tilesDir0, int tilesDir6, Plan* plan)
{
    const Sprite* sprite = instance->m_sprite;
    if (sprite->m_type != kTypeBuilding) return;
    plan->active = true;
    MapView* view = GetMapView();
    if (instance->m_animIndex != 0) {
        instance->m_animIndex = 0;
        instance->m_frameIndex = 0;
    }
    int x, y;
    FrameOrigin(instance, world, &x, &y);
    const FootprintDepth footprint = MeasureBuildingFootprint(world, tilesDir6, tilesDir0);
    const float time = game::GetWorld()->m_gameTime;
    sprite->m_anims[0]->m_lastUsedTime = time;
    sprite->m_anims[1]->m_lastUsedTime = time;
    const uint8_t extent = sprite->m_buildingExtra[instance->m_direction];
    x += view->m_cameraX;
    y += view->m_cameraY;
    plan->position = sprite_bake::ToWriterPosition(AsRect(rect), x, y, view->m_blockSize);
    rect[0] = plan->position.rect.x0;
    rect[1] = plan->position.rect.y0;
    rect[2] = plan->position.rect.x1;
    rect[3] = plan->position.rect.y1;
    plan->step = sprite_preview::DepthPerPixel(static_cast<int>(footprint.origin) - static_cast<int>(footprint.far), extent);
    plan->depth = footprint.far;
}

// BakeBuildingRect up to the writer call: no type check and no animation change, the depth is the point's own (+8, odd) rather than a footprint ramp.
void PlanRect(Instance* instance, int* rect, const float* world, int maskX, int maskY, Plan* plan)
{
    plan->active = true;
    MapView* view = GetMapView();
    int x, y;
    FrameOrigin(instance, world, &x, &y);
    const uint32_t depth = view->GetDepth(world);
    x += view->m_cameraX;
    y += view->m_cameraY;
    plan->position = sprite_bake::ToWriterPosition(AsRect(rect), x, y, view->m_blockSize);
    rect[0] = plan->position.rect.x0;
    rect[1] = plan->position.rect.y0;
    rect[2] = plan->position.rect.x1;
    rect[3] = plan->position.rect.y1;
    plan->depth = sprite_bake::RectDepth(depth);
    plan->maskX = maskX;
    plan->maskY = maskY;
}

// The writers (below the planners, beside the test drivers).
void Depth(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, float step, uint32_t depth);
void Colour(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth);
void RectColour(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, int maskX, int maskY);
void MaskedColour(Instance* instance, int bx, int by, int ox, int oy, uint32_t depth);
void GhostDepth(Instance* instance, int bx, int by, int ox, int oy, float step, uint32_t depth);
void GhostColour(Instance* instance, int* rect, int bx, int by, int ox, int oy, float step, uint32_t depth);
void GroundBlock(Instance* instance, int bx, int by, int ox, int oy, float step, uint32_t depth);

void CallDepth(Instance* instance, int* rect, const Plan& plan)
{
    Depth(instance, rect, plan.position.blockX, plan.position.blockY, plan.position.offsetX, plan.position.offsetY, plan.step, plan.depth);
}

void CallRectColour(Instance* instance, int* rect, const Plan& plan)
{
    RectColour(instance, rect, plan.position.blockX, plan.position.blockY, plan.position.offsetX, plan.position.offsetY, plan.depth,
            plan.maskX, plan.maskY);
}

template<class Env> void RunMaskedColour(Env& env, Instance* instance, int bx, int by, int ox, int oy, uint32_t depth)
{
    const auto frame = ColourFrameOf(instance);
    sprite_bake::BakeMaskedColour(env, CurrentAnim(instance)->m_height, bx, by, ox, oy, depth, frame);
}

template<class Env>
void RunGhostWriter(Env& env, Instance* instance, const PixelRect& rect, int bx, int by, int ox, int oy, float step, uint32_t depth)
{
    const Sprite* sprite = instance->m_sprite;
    if (sprite->m_type != kTypeBuilding) return;
    const sprite_bake::DepthFrames frames{FrameOf(instance, sprite->m_anims[0]), FrameOf(instance, sprite->m_anims[1]),
            sprite->m_palettes[0], CurrentAnim(instance)->m_width};
    sprite_bake::BakeGhost(env, rect, bx, by, ox, oy, step, depth, frames);
}

// One ground tile block: animation 0's frame (current frame and direction), palette 0 and the size of the current animation.
template<class Env> void RunGroundBlock(Env& env, Instance* instance, int bx, int by, int ox, int oy, uint32_t depth)
{
    const Sprite* sprite = instance->m_sprite;
    const uint8_t* colour = FrameOf(instance, sprite->m_anims[0]);
    sprite_bake::GroundFrame frame{colour, sprite->m_palettes[0], CurrentAnim(instance)->m_width, CurrentAnim(instance)->m_height, 0, 0};
    frame.halfMask = ui::pixel_primitives::GetMaskHalf();
    frame.quarterMask = ui::pixel_primitives::GetMaskQuarter();
    sprite_bake::BakeGroundTile(env, bx, by, ox, oy, depth, frame);
}

// SpriteInstance::DrawGroundTile up to the writer call: the point's screen position moved by (-0x1f, -0x20) plus the camera is the frame origin, the footprint is
// measured like the building preview (tilesDir0 steps along neighbour direction 0, tilesDir6 along 6; its far corner is the depth), animation 0 counts as
void PlanGround(Instance* instance, const float* world, int tilesDir0, int tilesDir6, Plan* plan)
{
    MapView* view = GetMapView();
    int screen[2];
    view->ProjectWorldPoint(screen, world);
    const int x = screen[0] - 0x1f + view->m_cameraX;
    const int y = screen[1] - 0x20 + view->m_cameraY;
    const FootprintDepth footprint = MeasureBuildingFootprint(world, tilesDir6, tilesDir0);
    instance->m_sprite->m_anims[0]->m_lastUsedTime = game::GetWorld()->m_gameTime;
    const int bs = view->m_blockSize;
    plan->active = true;
    plan->position = {{0, 0, 0, 0}, x / bs, y / bs, x % bs, y % bs};
    plan->depth = footprint.far;
    plan->step = static_cast<float>(
            static_cast<double>(static_cast<int>(footprint.origin) - static_cast<int>(footprint.far)) * static_cast<double>(0.004f));
    plan->writer = Writer::GroundTile;
}

void CallGround(Instance* instance, const Plan& plan)
{
    const auto& p = plan.position;
    GroundBlock(instance, p.blockX, p.blockY, p.offsetX, p.offsetY, plan.step, plan.depth);
}

void PlanOverlay(Instance* instance, const float* world, bool ghost, int tilesDir0, int tilesDir6, Plan* plan)
{
    const Sprite* sprite = instance->m_sprite;
    if (ghost && sprite->m_type != kTypeBuilding) return;
    MapView* view = GetMapView();
    int screen[2];
    view->ProjectWorldPoint(screen, world);
    if (ghost && instance->m_animIndex != 0) {
        instance->m_animIndex = 0;
        instance->m_frameIndex = 0;
    }
    const int x = screen[0] + instance->GetDrawOffsetX() + view->m_cameraX;
    const int y = screen[1] + CurrentAnim(instance)->m_hotspotY + view->m_cameraY;
    const auto clip = sprite_bake::ClipFrame(x, y, CurrentAnim(instance)->m_width, CurrentAnim(instance)->m_height, !ghost);
    plan->active = true;
    const int bs = view->m_blockSize;
    plan->position = {clip.rect, clip.x / bs, clip.y / bs, clip.x % bs, clip.y % bs};
    if (ghost) {
        const FootprintDepth footprint = MeasureBuildingFootprint(world, tilesDir6, tilesDir0);
        const float time = game::GetWorld()->m_gameTime;
        sprite->m_anims[0]->m_lastUsedTime = time;
        sprite->m_anims[1]->m_lastUsedTime = time;
        plan->step = sprite_preview::DepthPerPixel(
                static_cast<int>(footprint.origin) - static_cast<int>(footprint.far), sprite->m_buildingExtra[instance->m_direction]);
        plan->depth = footprint.far;
        plan->writer = clip.clipped ? Writer::GhostColour : Writer::GhostDepth;
    } else {
        const uint32_t depth = view->GetDepth(world);
        plan->depth = depth | 1u;
        plan->writer = clip.clipped ? Writer::Colour : Writer::MaskedColour;
    }
}

void CallOverlay(Instance* instance, const Plan& plan)
{
    const auto& p = plan.position;
    int rect[4] = {p.rect.x0, p.rect.y0, p.rect.x1, p.rect.y1};
    if (plan.writer == Writer::Colour)
        Colour(instance, rect, p.blockX, p.blockY, p.offsetX, p.offsetY, plan.depth);
    else if (plan.writer == Writer::MaskedColour)
        MaskedColour(instance, p.blockX, p.blockY, p.offsetX, p.offsetY, plan.depth);
    else if (plan.writer == Writer::GhostDepth)
        GhostDepth(instance, p.blockX, p.blockY, p.offsetX, p.offsetY, plan.step, plan.depth);
    else if (plan.writer == Writer::GhostColour)
        GhostColour(instance, rect, p.blockX, p.blockY, p.offsetX, p.offsetY, plan.step, plan.depth);
}

// ---- writers --------------------------------------------------------------------------------------------------------------------

template<class Run> void Write(Run&& run)
{
    GameBakeEnv env(*GetMapView());
    run(env);
}

void Depth(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, float step, uint32_t depth)
{
    Write([&](auto& env) { RunDepth(env, instance, AsRect(rect), blockX, blockY, offsetX, offsetY, step, depth); });
}

void Colour(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth)
{
    Write([&](auto& env) { RunColour(env, instance, AsRect(rect), blockX, blockY, offsetX, offsetY, depth); });
}

void RectColour(Instance* instance, int* rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, int maskX, int maskY)
{
    Write([&](auto& env) { RunRectColour(env, instance, AsRect(rect), blockX, blockY, offsetX, offsetY, depth, maskX, maskY); });
}

void MaskedColour(Instance* instance, int bx, int by, int ox, int oy, uint32_t depth)
{
    Write([&](auto& env) { RunMaskedColour(env, instance, bx, by, ox, oy, depth); });
}

void GhostDepth(Instance* instance, int bx, int by, int ox, int oy, float step, uint32_t depth)
{
    const bool building = instance->m_sprite->m_type == kTypeBuilding;
    const int rect[4] = {0, 0, building ? CurrentAnim(instance)->m_width : 0, building ? CurrentAnim(instance)->m_height : 0};
    Write([&](auto& env) { RunGhostWriter(env, instance, AsRect(rect), bx, by, ox, oy, step, depth); });
}

void GhostColour(Instance* instance, int* rect, int bx, int by, int ox, int oy, float step, uint32_t depth)
{
    Write([&](auto& env) {
        if (instance->m_sprite->m_type == kTypeBuilding) RunGhostWriter(env, instance, AsRect(rect), bx, by, ox, oy, step, depth);
    });
}

void GroundBlock(Instance* instance, int bx, int by, int ox, int oy, float, uint32_t depth)
{
    Write([&](auto& env) { RunGroundBlock(env, instance, bx, by, ox, oy, depth); });
}

void Overlay(Instance* instance, const float* world, bool ghost, int tilesDir0, int tilesDir6)
{
    Plan plan;
    PlanOverlay(instance, world, ghost, tilesDir0, tilesDir6, &plan);
    if (plan.active) CallOverlay(instance, plan);
}

// ---- drivers ------------------------------------------------------------------------------------------------------------------

// A synthetic building: two animations (colour and depth) of two frames and four directions with the same random run layout, a random palette and random
// per-direction extents, at a heap address so the pointers inside it stay valid.
struct Synthetic {
    static constexpr int kFrames = 2, kDirections = 4, kUnique = kFrames * kDirections;
    std::vector<uint8_t> data[2];
    uint32_t offsets[2][kUnique];
    uint16_t indices[kUnique];
    Anim anims[2];
    Anim* table[2];
    uint16_t palette[256];
    uint16_t* paletteTable[1];
    Sprite sprite;
    Instance instance;
    int width = 0, height = 0;

    Synthetic(Rng& rng, int w, int h) : width(w), height(h)
    {
        for (int unique = 0; unique < kUnique; ++unique) {
            offsets[0][unique] = static_cast<uint32_t>(data[0].size());
            offsets[1][unique] = static_cast<uint32_t>(data[1].size());
            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w;) {
                    // Sequenced, so every compiler draws the same stream (the operands of % are not); the scripts' map hashes
                    // were recorded with the chance drawn first.
                    const uint32_t range = rng.Chance(8) ? 127 : 17;
                    const int n = std::min(w - col, 1 + static_cast<int>(rng.Next() % range));
                    const bool literal = (rng.Next() & 3) != 0;
                    for (auto& stream : data) stream.push_back(static_cast<uint8_t>(literal ? n : -n));
                    if (literal)
                        for (int k = 0; k < n; ++k)
                            for (auto& stream : data) stream.push_back(static_cast<uint8_t>(rng.Next()));
                    col += n;
                }
            }
        }
        for (int k = 0; k < kUnique; ++k) indices[k] = static_cast<uint16_t>(k);
        static const char* const names[2] = {"image", "depth"};
        for (int a = 0; a < 2; ++a) {
            std::memset(&anims[a], 0, sizeof(Anim));
            anims[a].m_name = const_cast<char*>(names[a]);
            anims[a].m_loaded = anims[a].m_allLoaded = 1;
            anims[a].m_frames = kFrames;
            anims[a].m_directions = kDirections;
            anims[a].m_width = static_cast<uint16_t>(w);
            anims[a].m_height = static_cast<uint16_t>(h);
            anims[a].m_hotspotX = static_cast<int16_t>(rng.Range(-16, 16));
            anims[a].m_hotspotY = static_cast<int16_t>(rng.Range(-16, 16));
            anims[a].m_uniqueFrames = kUnique;
            anims[a].m_frameIndices = indices;
            anims[a].m_frameOffsets = offsets[a];
            anims[a].m_allData = data[a].data();
            anims[a].m_dataSize = static_cast<uint32_t>(data[a].size());
            anims[a].m_lastUsedTime = -1.0f;
            table[a] = &anims[a];
        }
        for (auto& v : palette) v = static_cast<uint16_t>(rng.Next());
        paletteTable[0] = palette;
        std::memset(&sprite, 0, sizeof(sprite));
        sprite.m_animCount = 2;
        sprite.m_anims = table;
        sprite.m_paletteCount = 1;
        sprite.m_palettes = paletteTable;
        sprite.m_type = kTypeBuilding;
        for (auto& extent : sprite.m_buildingExtra) extent = static_cast<uint8_t>(rng.Chance(5) ? 0 : 1 + rng.Next() % 255);
        std::memset(&instance, 0, sizeof(instance));
        instance.m_sprite = &sprite;
        instance.m_animIndex = rng.Chance(40) ? 1 : 0;
        instance.m_frameIndex = static_cast<int>(rng.Next() % kFrames);
        instance.m_direction = static_cast<int>(rng.Next() % kDirections);
        instance.m_animType = 2;
    }
};

bool DriverReady(MapView* view)
{
    return view && view->m_blocksMapped && view->m_blocks && view->m_blockSize == sprite_bake::kBlockEdge && view->m_blocksPerRow >= 4 &&
            view->m_blocksPerColumn >= 4;
}

void PlaceOverlay(Synthetic& synthetic, const float* world, bool ghost, int targetX, int targetY)
{
    Instance probe = synthetic.instance;
    if (ghost && probe.m_sprite->m_type == kTypeBuilding && probe.m_animIndex != 0) {
        probe.m_animIndex = 0;
        probe.m_frameIndex = 0;
    }
    for (auto& anim : synthetic.anims) anim.m_hotspotX = anim.m_hotspotY = 0;
    int x, y;
    FrameOrigin(&probe, world, &x, &y);
    const int baseOffset = (&probe)->GetDrawOffsetX();
    for (auto& anim : synthetic.anims) anim.m_hotspotX = 1;
    const int sign = (&probe)->GetDrawOffsetX() - baseOffset;
    for (auto& anim : synthetic.anims) {
        anim.m_hotspotX = static_cast<int16_t>((targetX - x - GetMapView()->m_cameraX) * sign);
        anim.m_hotspotY = static_cast<int16_t>(targetY - y - GetMapView()->m_cameraY);
    }
}

// A persistent map fixture for the map-hash check: both wrappers paint deterministic streams, clipped and whole, and the result stays, so
// the scripts can assert the hash of the background file.
void DriveOverlayScene(int count)
{
    MapView* view = GetMapView();
    if (!DriverReady(view)) return;
    Rng rng{71};
    const auto synthetic = std::make_unique<Synthetic>(rng, 160, 24);
    float world[3] = {1600.0f, 2400.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        for (int branch = 0; branch < 2; ++branch) {
            synthetic->instance.m_animIndex = 0;
            synthetic->instance.m_frameIndex = 0;
            PlaceOverlay(*synthetic, world, false, branch == 0 ? 250 : -10, branch == 0 ? 250 : -6);
            (&synthetic->instance)->BakeMasked(world);
            (&synthetic->instance)->BakeGhost(world, 3, 2);
        }
        g_overlayScene.Count();
    }
}

// A persistent map fixture for the map-hash check of the ground tile pair: the wrapper on real world points and the writer at block-crossing
// origins, over the real background (nothing is restored, so the scripts can assert the hash of the background file).
void DriveGroundScene(int count)
{
    MapView* view = GetMapView();
    if (!DriverReady(view) || !game::GetGrid() || !game::GetWorld()) return;
    Rng rng{77};
    const auto synthetic = std::make_unique<Synthetic>(rng, 100, 24);
    Instance* instance = &synthetic->instance;
    for (int i = 0; i < count; ++i) {
        instance->m_animIndex = i % 2;
        instance->m_frameIndex = i % Synthetic::kFrames;
        instance->m_direction = i % Synthetic::kDirections;
        const float world[3] = {1000.0f + 211.0f * static_cast<float>(i % 7), 2400.0f + 337.0f * static_cast<float>(i % 5), 0.0f};
        instance->DrawGroundTile(world, 1 + i % 3, 1 + i % 4);
        const int bx = 1 + i % (view->m_blocksPerRow - 2);
        GroundBlock(instance, bx, 3 + i % 4, 60 + 7 * (i % 9), 100 + i % 28, 0.0f, 0x2000u + 0x300u * static_cast<uint32_t>(i));
        g_groundScene.Count();
    }
}

} // namespace

void SpriteInstance::BakeBuilding(screen::Rect clip, const float world[3], int tilesDir0, int tilesDir6)
{
    int rect[4] = {clip.left, clip.top, clip.right, clip.bottom}; // the writer rewrites it into frame coordinates
    Plan plan;
    PlanBuilding(this, rect, world, tilesDir0, tilesDir6, &plan);
    if (plan.active) CallDepth(this, rect, plan);
}

void SpriteInstance::BakeBuildingRect(screen::Rect clip, const float world[3], int maskX, int maskY)
{
    int rect[4] = {clip.left, clip.top, clip.right, clip.bottom}; // the writer rewrites it into frame coordinates
    Plan plan;
    PlanRect(this, rect, world, maskX, maskY, &plan);
    CallRectColour(this, rect, plan);
}

void SpriteInstance::BakeMasked(const float world[3])
{
    Overlay(this, world, false, 0, 0);
}

void SpriteInstance::BakeGhost(const float world[3], int tilesDir0, int tilesDir6)
{
    Overlay(this, world, true, tilesDir0, tilesDir6);
}

void SpriteInstance::DrawGroundTile(const float world[3], int tilesDir0, int tilesDir6)
{
    Plan plan;
    PlanGround(this, world, tilesDir0, tilesDir6, &plan);
    CallGround(this, plan);
}

bool RegisterSpriteBakeDrivers()
{
    g_overlayScene.SetDriver(&DriveOverlayScene);
    g_groundScene.SetDriver(&DriveGroundScene);
    return true;
}

} // namespace obl::ui
