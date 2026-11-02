#include "game/objects/object_manager_state.h"
#include "game/ui/renderer_ops.h"
#include "game/objects/building_api.h"
#include "game/map/path_queries.h"
#include "game/ui/map_view.h"

#include "game/ui/map_rebuild.h"
#include "game/ui/map_view_access.h"
#include "game/ui/sprite_instance_bake.h"

#include <cstdint>
#include "game/ui/map_view_blocks.h"
#include "game/objects/object_building.h"

namespace obl::ui {
namespace {

using object::Building;
using screen::Rect;
using ui::MapView;

class RebuildEnv {
public:
    explicit RebuildEnv(MapView& view) : m_view(view) {}

    void LoadBlockPair(int x, int y) { (m_view).LoadBlockPair(static_cast<uint32_t>(x), static_cast<uint32_t>(y)); }
    void RebuildPathTiles(Rect& rect) { game::GetPathTiles()->RebuildRect(rect); }
    uint32_t NumBuildings() { return game::GetObjectManager()->m_buildings.size(); }
    object::Building* Building(uint32_t i) { return static_cast<object::Building*>(game::GetObjectManager()->m_buildings[i]); }
    const Rect* BuildingRect(object::Building& building) { return static_cast<const Rect*>(building.UpdateScreenRect()); }
    bool Intersect(const Rect& a, const Rect& b, Rect& out) { return renderer_ops::IntersectRects(a, b, out); }
    bool HasDemolishJob(object::Building& b) { return object::building_api::HasDemolishJob(&b); }
    bool HasBuildJob(object::Building& b) { return object::building_api::HasBuildJob(&b); }
    void BakeBuilding(object::Building& building, const Rect& clip, const float position[3], int gridW, int gridH)
    {
        static_cast<ui::SpriteInstance*>(building.m_sprite)->BakeBuilding(clip, position, gridW, gridH);
    }

private:
    MapView& m_view;
};

} // namespace

void MapView::QueueBlockRebuild(int blockX, int blockY, int width, int height)
{
    RebuildEnv env(*this);
    map_rebuild::QueueBlockRebuild(env, *this, blockX, blockY, width, height);
}

int MapView::GetRebuildQueueSize()
{
    return map_rebuild::QueueSize(*this);
}

void MapView::ProcessRebuildQueue(float time)
{
    RebuildEnv env(*this);
    map_rebuild::ProcessRebuildQueue(env, *this, time);
}

} // namespace obl::ui
