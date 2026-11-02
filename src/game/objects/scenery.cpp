#include "game/objects/scenery_names.h"
#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/map/grid_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_building.h"
#include "game/map/path_queries.h"

#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/objects/building_api.h"
#include "game/ui/map_view_access.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_manager_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_scenery.h"
#include "game/objects/scenery_record.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/scenery_placement.h"
#include "game/objects/scenery_queries.h"
#include "game/objects/scenery_actions.h"
#include "game/objects/object_scratch.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/map_view.h"
#include "game/ui/renderer_ops.h"
#include "core/call_counter.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/map/cell_objects.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/ui/text_table.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_instance_bake.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/ui/map_view_restore.h"
#include "game/game/diagnostics.h"

// The game-bound methods of scenery (the logic is scenery_queries.h, scenery_placement.h and scenery_actions.h).
namespace obl::object {
using game::FatalError;
using game::GetGrid;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetPathTiles;
using game::GetRenderer;
using game::GetWorld;
using ui::GetMapView;

namespace {

using object::Scenery;
using ui::SpriteInstance;

test::CallCounter g_queries("objects.scenery-queries");

bool IsSelectableOf(const Scenery& s)
{
    return scenery::IsSelectable(s.m_definition->m_burn != 0, s.m_canSelect != 0, s.m_hasMenu != 0);
}

int32_t UpdateFreeSeatOf(Scenery& s)
{
    s.m_FreeSeat = s.m_MaxNumAttachedGuests;
    return scenery::FreeSeatsLeft(s.m_MaxNumAttachedGuests, s.m_NumAttachedGuests);
}

bool IsBin(const Scenery& s)
{
    return scenery_names::IsBin(s.m_Name);
}

void SetBinFillOf(Scenery& s, uint32_t requested)
{
    const auto capacity = static_cast<uint32_t>(s.m_definition->m_Capacity);
    const uint32_t fill = scenery::ClampBinFill(requested, capacity);
    s.m_NumItems = static_cast<int32_t>(fill);
    if (const char* anim = scenery::BinAnimName(scenery::BinFillAnim(IsBin(s), fill, capacity)))
        (static_cast<ui::SpriteInstance*>(s.m_sprite))->SetAnimByName(anim, 0.0f, nullptr);
}

// The bench position moved by the seat offset; a lounger answers with the centre of its tile; anything else with its own position.
void ApproachPositionOf(Scenery& s, float* out)
{
    if (scenery_names::IsLounger(s.m_Name)) {
        int tile[2];
        s.GetTilePosition(tile);
        const grid_math::WorldPoint centre = GetGrid()->TileToWorldCenter(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]));
        out[0] = centre.x;
        out[1] = centre.y;
        out[2] = centre.z;
        return;
    }
    if (ascii::EqualsNoCase(s.m_Name, "Bench")) {
        const auto* sprite = static_cast<const SpriteInstance*>(s.m_sprite);
        const scenery::SeatOffset offset = scenery::BenchSeatOffset(s.m_FreeSeat, static_cast<uint32_t>(sprite->m_direction));
        out[0] = offset.x + s.m_WorldXYZ[0];
        out[2] = 0.0f;
        out[1] = offset.y + s.m_WorldXYZ[1];
        return;
    }
    std::memcpy(out, s.m_WorldXYZ, 3 * sizeof(float));
}

// ---- SetPosition / RequestRemoval: the game's side of scenery_placement.h -----------------------------------------------------------

using object::Building;
using screen::Rect;

class PlacementEnv {
public:
    void* CellAt(const uint32_t xyz[3])
    {
        // The position is the raw bits of three floats; the lookup reads only x and y.
        return GetGrid()->GetCellAtWorldPoint(std::bit_cast<float>(xyz[0]), std::bit_cast<float>(xyz[1]));
    }
    bool CellHas(void* cell, Scenery& s) { return cell_objects::ContainsObject(*static_cast<map::GridCell*>(cell), s); }
    void RemoveFromCell(void* cell, Scenery& s) { cell_objects::RemoveObject(*static_cast<map::GridCell*>(cell), s); }
    void AddToCell(void* cell, Scenery& s) { cell_objects::AddObject(*static_cast<map::GridCell*>(cell), s); }
    double TerrainHeight(int worldX, int worldY) { return GetMapView()->GetTerrainHeightF(worldX, worldY); }

    void RestoreBackground(Rect& rect)
    {
        const map_restore::PixelRect pixels{rect.left, rect.top, rect.right, rect.bottom};
        GetMapView()->RestoreBackgroundRect(&pixels);
    }
    void RebuildPathTiles(Rect& rect) { GetPathTiles()->RebuildRect(rect); }
    uint32_t NumBuildings() { return GetObjectManager()->m_buildings.size(); }
    object::Building* Building(uint32_t i) { return static_cast<object::Building*>(GetObjectManager()->m_buildings[i]); }
    bool HasDemolishJob(object::Building& b) { return building_api::HasDemolishJob(&b); }
    bool HasBuildJob(object::Building& b) { return building_api::HasBuildJob(&b); }
    const Rect* BuildingRect(object::Building& b) { return static_cast<const Rect*>(b.UpdateScreenRect()); }
    bool Intersect(const Rect& a, const Rect& b, Rect& out) { return renderer_ops::IntersectRects(a, b, out); }
    void BakeBuilding(object::Building& b, const Rect& clip, const float position[3], int gridW, int gridH)
    {
        (static_cast<ui::SpriteInstance*>(b.m_sprite))->BakeBuilding(clip, position, gridW, gridH);
    }
    void Post(const messages::Message& message)
    {
        messages::Message copy = message; // the handler stamps the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
};

// ---- selection, GUI text and vandalism -------------------------------------------------------------------------------------------

// The action driver (`call-drive objects.scenery-actions`) runs the actions on copies with the UI and animation calls swallowed.
bool g_actionDry = false;
struct ActionEnv : PlacementEnv {
    void Post(const messages::Message& message)
    {
        if (!g_actionDry) PlacementEnv::Post(message);
    }
    ui::MapView* GetMapView() { return ui::GetMapView(); }
    void ShowDialog(const char* name, Scenery* provider)
    {
        if (!g_actionDry) GetGuiManager()->ShowDialog(name, provider, false);
    }
    void ForgetSelection() { GetGuiManager()->m_selection = -1; }
    void WorldToInt(const float* xyz, int& x, int& y)
    {
        float copy[3] = {xyz[0], xyz[1], xyz[2]};
        ui::MapView::WorldToInt(copy, &x, &y);
    }
    uint32_t Width() { return static_cast<uint32_t>(GetRenderer()->m_width); }
    uint32_t Height() { return static_cast<uint32_t>(GetRenderer()->m_height); }
    float Zoom() { return GetRenderer()->m_zoomScale; }
    void SetCamera(int x, int y)
    {
        if (!g_actionDry) GetMapView()->SetCameraPos(x, y);
    }
    void StopCamera()
    {
        if (!g_actionDry) GetMapView()->StopCamera();
    }
    void PrintInt(Char16* out, int value) { ui::host_text::Print(out, u"%d", value); }
    int Count(int type) { return object_manager::CountByClass(GetObjectManager(), type); }
    unsigned RepairStatus(int id)
    {
        return static_cast<unsigned char>(repair::repair_facade::GetJobStatus(GetWorld()->m_repairManager, id));
    }
    unsigned CleaningStatus(int id)
    {
        return static_cast<unsigned char>(cleaning::cleaning_facade::GetJobStatus(GetWorld()->m_cleaningManager, id));
    }
    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
    void Fatal(const char* key) { FatalError("A script read unrecognized scenery variable \"%s\"", key); }
    bool EqualName(const char* a, const char* b) { return ascii::EqualsNoCase(a, b); }
    float Time() { return GetWorld()->m_gameTime; }
    void Animate(void* sprite, const char* anim, float time)
    {
        if (!g_actionDry) (static_cast<ui::SpriteInstance*>(sprite))->SetAnimByName(anim, time, nullptr);
    }
};

// ---- call-drive ------------------------------------------------------------------------------------------------------------------

std::vector<Scenery*> LiveScenery()
{
    std::vector<Scenery*> result;
    object::Manager* manager = GetObjectManager();
    if (!manager) return result;
    const auto list = object_manager::GetListByClass(manager, static_cast<int>(object::ObjectType::Scenery));
    const auto count = static_cast<uint32_t>(list.size());
    for (uint32_t i = 0; i < count; ++i)
        if (list[i] && list[i]->m_type == object::ObjectType::Scenery) result.push_back(static_cast<Scenery*>(list[i]));
    return result;
}

void DriveActions(int rounds)
{
    g_actionDry = true;
    auto* gui = GetGuiManager();
    const int savedSelection = gui->m_selection;
    for (int round = 0; round < rounds; ++round)
        for (Scenery* live : LiveScenery()) {
            Scratch<Scenery> scratch(*live); // shares the live scenery's sprites and cell: its teardown must not run
            Scenery& copy = *scratch;
            for (int flags = 0; flags < 4; ++flags) copy.Scenery::OnSelect(flags & 1, (flags >> 1) & 1);
            for (int flags = 0; flags < 8; ++flags) {
                copy.m_NumItems = flags - 2;
                copy.m_urgentCleaningCalled = flags & 1;
                copy.m_urgentRepairCalled = (flags >> 1) & 1;
                copy.m_calledRepairing = (flags >> 2) & 1;
                for (const char* key : {"BIN_LITTER", "BI_CALL_CLEANER_STATE", "BI_CALL_MECHANIC_STATE", "SCENERY_REPAIRSTATUS",
                             "SCENERY_CLEANINGSTATUS"}) {
                    Char16 out[512] = {};
                    copy.Scenery::GetVariableText(key, out);
                }
            }
            SpriteInstance sprite = *static_cast<SpriteInstance*>(live->m_sprite);
            copy.m_sprite = &sprite;
            for (const char* name : {"STREET_LAMP", "bin", "BENCH", "other"})
                for (int fill : {0, copy.m_definition->m_Capacity, copy.m_definition->m_Capacity + 1})
                    for (uint8_t flag : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
                        sprite.m_name = const_cast<char*>(name);
                        sprite.m_animIndex = 3;
                        copy.m_NumItems = fill;
                        copy.m_vandalised = flag;
                        copy.m_RepairState = 7;
                        copy.m_PreVandlismAnim = 42;
                        copy.Vandalise();
                    }
        }
    gui->m_selection = savedSelection;
    g_actionDry = false;
}

test::CallCounter g_actions("objects.scenery-actions", &DriveActions);

} // namespace

// Entity has no position setter of its own: every class that keeps the base slot places itself like scenery.
void Base::SetPosition(const float* xyz)
{
    PlacementEnv env;
    scenery::SetPosition(env, *static_cast<Scenery*>(this), xyz);
}

void Scenery::SetName(const char* name)
{
    g_queries.Count();
    scenery::CopyName(m_Name, name);
}

bool Scenery::HasState(uint32_t state)
{
    g_queries.Count();
    return scenery::StateIsKnown(state);
}

bool Scenery::IsSelectable()
{
    g_queries.Count();
    return IsSelectableOf(*this);
}

int32_t Scenery::UpdateFreeSeat()
{
    g_queries.Count();
    return UpdateFreeSeatOf(*this);
}

void Scenery::SetBinFill(uint32_t fill)
{
    g_queries.Count();
    SetBinFillOf(*this, fill);
}

float* Scenery::GetApproachPosition(float* out)
{
    g_queries.Count();
    ApproachPositionOf(*this, out);
    return out;
}

void Scenery::RequestRemoval()
{
    g_queries.Count();
    PlacementEnv env;
    scenery::RequestRemoval(env, *this);
}

void Scenery::GetVariableText(const char* key, Char16* out)
{
    ActionEnv env;
    g_actions.Count();
    scenery::GetVariableText(*this, key, out, env);
}

void Scenery::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    ActionEnv env;
    g_actions.Count();
    scenery::OnSelect(*this, clicked != 0, centerCamera != 0, env);
}

void Scenery::Vandalise()
{
    ActionEnv env;
    g_actions.Count();
    scenery::Vandalise(*this, env);
}
} // namespace obl::object
