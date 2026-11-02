#include "game/ui/map_view_restore.h"

#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "game/map/path_queries.h"
#include "game/ui/map_restore.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_snapshot.h"
#include "core/call_counter.h"
#include "game/objects/object_scenery.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using map_restore::PixelRect;
using ui::MapView;
using ui::map_snapshot::GameEnv;

} // namespace

void MapView::RestoreBackgroundRect(const PixelRect* rect)
{
    GameEnv env(*this);
    map_restore::RestoreRect(env, *rect);
}

void MapView::RestoreTileBackground(int worldX, int worldY, const int8_t* mask)
{
    const map_restore::TileSplit split = map_restore::SplitTilePosition(worldX, worldY, m_blockSize);

    RestoreTileBackgroundRect(static_cast<uint32_t>(split.blockX), static_cast<uint32_t>(split.blockY), split.offsetX, split.offsetY, mask);
}

void MapView::RestoreTileBackgroundRect(uint32_t blockX, uint32_t blockY, int offsetX, int offsetY, const int8_t* mask)
{
    GameEnv env(*this);
    map_restore::RestoreTileRect(env, blockX, blockY, offsetX, offsetY, mask);
}

bool ExecuteMapRestoreCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "restore-rect" && name != "path-tile" && name != "remove-scenery") return false;
    MapView* view = GetMapView();
    if (!view || !view->m_blocksMapped || !game::GetGrid()) {
        OBL_LOG("autotest: FAILED %s requires settled gameplay (await gameplay)", name.c_str());
        return true;
    }
    const auto number = [&](size_t i, long& out, int base) {
        char* end = nullptr;
        out = i < args.size() ? std::strtol(args[i].c_str(), &end, base) : 0;
        return i < args.size() && end && *end == '\0' && !args[i].empty();
    };
    long a = 0, b = 0, c = 0, d = 0;
    if (name == "restore-rect" && args.size() == 4 && number(0, a, 10) && number(1, b, 10) && number(2, c, 10) && number(3, d, 10)) {
        PixelRect rect{static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c), static_cast<int32_t>(d)};
        view->RestoreBackgroundRect(&rect);
        OBL_LOG("autotest: restore-rect x0=%ld y0=%ld x1=%ld y1=%ld", a, b, c, d);
    } else if (name == "path-tile" && args.size() == 3 && number(0, a, 10) && number(1, b, 10) && number(2, c, 16)) {
        map::Grid* grid = game::GetGrid();
        grid->Cell(static_cast<uint32_t>(a), static_cast<uint32_t>(b)).m_flags = static_cast<uint32_t>(c);
        game::GetPathTiles()->RebuildTile(static_cast<uint32_t>(a), static_cast<uint32_t>(b), 0);
        OBL_LOG("autotest: path-tile x=%ld y=%ld flags=%lx", a, b, c);
    } else if (name == "remove-scenery" && args.size() <= 1 && (args.empty() || number(0, a, 10))) {
        object::Manager* manager = game::GetObjectManager();
        long seen = 0;
        object::Scenery* chosen = nullptr;
        for (uint32_t i = 0; manager && i < manager->m_others.size() && !chosen; ++i) {
            object::Base* object = manager->m_others[i];
            if (!object || object->m_type != object::ObjectType::Scenery) continue;
            auto* scenery = static_cast<object::Scenery*>(object);
            if (scenery->m_definition && scenery->m_definition->m_burn != 0 && seen++ == a) chosen = scenery;
        }
        if (!chosen) {
            OBL_LOG("autotest: FAILED remove-scenery: no burned scenery number %ld", a);
            return true;
        }
        OBL_LOG("autotest: remove-scenery number=%ld id=%u rect=(%d, %d)-(%d, %d)", a, chosen->m_id, chosen->m_ScrXYRect.left,
                chosen->m_ScrXYRect.top, chosen->m_ScrXYRect.right, chosen->m_ScrXYRect.bottom);
        chosen->RequestRemoval();
    } else {
        OBL_LOG("autotest: FAILED %s invalid arguments", name.c_str());
    }
    return true;
}

} // namespace obl::ui
