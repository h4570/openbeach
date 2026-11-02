#include "game/objects/scenery_names.h"
#include "game/world/world_messages.h"
#include "game/objects/object_base.h"
#include "game/objects/object_static.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite.h"
#include "game/map/cell_objects.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_state.h"
#include "game/objects/scenery_draw.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/messages/message_state.h"
#include "game/objects/object_scenery.h"
#include "game/objects/scenery_logic.h"
#include "game/objects/scenery_record.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include <cstring>
#include "game/ui/map_view_math.h"
#include "game/game/diagnostics.h"

namespace obl::object {
using game::FatalError;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;
using ui::GetMapView;

namespace {
using namespace world_data;
using messages::Message;
using ui::SpriteInstance;
using enum messages::MessageType;
using object::Scenery;
test::CallCounter g_enter("objects.scenery-on-enter-state"), g_update("objects.scenery-update");

uint32_t* Tile(Scenery& s, uint32_t* out)
{
    return reinterpret_cast<uint32_t*>(s.GetTilePosition(reinterpret_cast<int32_t*>(out)));
}
void Neighbour(uint32_t* xy, int dir)
{
    const grid_math::TileXY next = GetGrid()->GetNeighbour(xy[0], xy[1], dir);
    xy[0] = next.x;
    xy[1] = next.y;
}
void Post(Scenery& s, uint32_t receiver, uint32_t type, uint32_t data = 0)
{
    Message m{};
    m.sender = s.m_id;
    m.receiver = receiver;
    m.type = type;
    m.data = data;
    messages::message_facade::Post(messages::message_facade::Current(), &m);
}
void Broadcast(Scenery& s, int cls)
{
    Message m{};
    m.sender = s.m_id;
    m.receiver = 0xffffffffu;
    m.type = 44;
    world::PostMessageToWorldClass(*GetWorld(), m, cls);
}
void Position(Scenery& s, float* xyz)
{
    s.SetPosition(xyz);
}
Scenery::DrawFunc DrawFor(scenery_record::Pick p)
{
    switch (p) {
    case scenery_record::Pick::Preview: return &SceneryDrawPreview;
    case scenery_record::Pick::Bench: return &SceneryDrawBench;
    case scenery_record::Pick::Lounger: return &SceneryDrawLounger;
    case scenery_record::Pick::Burned: return &SceneryDrawBurned;
    default: return &SceneryDrawGeneric;
    }
}
uint8_t Enter(Scenery& s, uint32_t state)
{
    const auto& def = *s.m_definition;
    auto* sprite = static_cast<SpriteInstance*>(s.m_sprite);
    if (!scenery::EnterStateActs(state)) return 1;
    if (state == 1) {
        Post(s, s.m_id, 3);
        float xyz[3];
        uint32_t xy[2];
        if (scenery_names::IsLounger(s.m_Name)) {
            sprite->m_paletteIndex = Rand() % sprite->m_sprite->m_paletteCount;
            if (sprite->m_paletteIndex >= sprite->m_sprite->m_paletteCount) sprite->m_paletteIndex = sprite->m_sprite->m_paletteCount - 1;
            uint32_t rays[8][2];
            for (auto& ray : rays) Tile(s, ray);
            bool found = false;
            while (!found)
                for (int d = 0; d < 8; ++d) {
                    Neighbour(rays[d], d);
                    if (GetGrid()->Cell(rays[d][0], rays[d][1]).m_flags & 4) {
                        sprite->m_direction = scenery::LoungerDirection(d, sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions);
                        found = true;
                        break;
                    }
                }
            Tile(s, xy);
            const grid_math::WorldPoint centre = GetGrid()->TileToWorldCenter(xy[0], xy[1]);
            xyz[0] = centre.x;
            xyz[1] = centre.y;
            xyz[2] = centre.z;
            Position(s, xyz);
            (void)(sprite)->SetAnimByName("static", GetWorld()->m_gameTime, nullptr);
            Post(s, s.m_OwnerBuildID, 81);
        } else if (scenery_names::IsLitter(s.m_Name)) {
            if (ascii::EqualsNoCase(def.m_key, "LitterPuke")) Post(s, s.m_id, 84);
            Tile(s, xy);
            const grid_math::WorldPoint centre = GetGrid()->TileToWorldCenter(xy[0], xy[1]);
            xyz[0] = centre.x;
            xyz[1] = centre.y;
            xyz[2] = centre.z;
            xyz[0] = xyz[0] + scenery::LitterJitterX(Rand());
            xyz[1] = xyz[1] + scenery::LitterJitterY(Rand());
            xyz[2] = static_cast<float>(GetMapView()->GetTerrainHeightF(static_cast<int>(xyz[0]), static_cast<int>(xyz[1])));
            Position(s, xyz);
        } else {
            const grid_math::TileXY tile = GetGrid()->WorldPointToTile(s.m_WorldXYZ[0], s.m_WorldXYZ[1]);
            xy[0] = tile.x;
            xy[1] = tile.y;
            const grid_math::WorldPoint point =
                    GetGrid()->TileToWorldPoint(xy[0], xy[1], scenery::SnapPoint(scenery_names::IsHoarding(s.m_Name)));
            xyz[0] = point.x;
            xyz[1] = point.y;
            xyz[2] = point.z;
            Position(s, xyz);
        }
        if (sprite->m_direction % 2) {
            s.m_gridH = static_cast<uint8_t>(def.m_GridWidth);
            s.m_gridW = static_cast<uint8_t>(def.m_GridHeight);
        }
    } else {
        GetWorld()->m_player->m_cash = scenery::SellRefund(GetWorld()->m_player->m_cash, static_cast<uint32_t>(def.m_Cost2Build));
        s.m_waitingForDeath = 1;
    }
    uint32_t start[2];
    Tile(s, start);
    uint32_t column[2] = {start[0], start[1]};
    for (uint32_t i = 0; i < s.m_gridW; ++i) {
        uint32_t xy[2] = {column[0], column[1]};
        for (uint32_t j = 0; j < s.m_gridH; ++j) {
            auto& cell = GetGrid()->Cell(xy[0], xy[1]);
            if (xy[0] != start[0] || xy[1] != start[1]) {
                if (state == 1)
                    cell_objects::AddObject(cell, s);
                else
                    cell_objects::RemoveObject(cell, s);
            }
            for (uint32_t flag : {def.m_collide ? 0x8000u : 0u, def.m_vegFlag ? 0x40u : 0u})
                if (flag) cell.m_flags = scenery::ApplyCellFlag(cell.m_flags, flag, state == 1);
            if (state == 1 && !scenery_names::IsLitter(s.m_Name)) {
                // Copy the list before posts: delivery may change its membership.
                std::vector<object::Base*> objects;
                cell_objects::ForEach(*GetObjectManager(), cell, [&](object::Base* object) { objects.push_back(object); });
                for (auto* litter : objects)
                    if (scenery_names::IsLitter(litter->m_Name) &&
                            cleaning::cleaning_facade::GetJobStatus(GetWorld()->m_cleaningManager, static_cast<int>(litter->m_id)) != 5) {
                        auto& object = *static_cast<Scenery*>(litter);
                        Post(object, object.m_id, 3, 3);
                        object.m_waitingForDeath = 1;
                        Broadcast(object, 11);
                    }
            }
            Neighbour(xy, 0);
        }
        Neighbour(column, 6);
    }
    if (state == 1)
        s.m_draw = DrawFor(scenery_record::PickDraw(s));
    else {
        repair::repair_facade::RemoveJob(GetWorld()->m_repairManager, static_cast<int>(s.m_id));
        cleaning::cleaning_facade::RemoveJob(GetWorld()->m_cleaningManager, static_cast<int>(s.m_id));
        Broadcast(s, 8);
        Broadcast(s, 11);
    }
    return 1;
}
uint8_t Update(Scenery& s)
{
    if (!s.Prop::Update()) FatalError("Update of scenery \"%s\" failed", s.m_Name);
    const auto& def = *s.m_definition;
    if (s.m_state == 0) {
        switch (scenery::RepairUpdate(
                def.m_repair, s.m_vandalised, s.m_calledRepairing, s.m_urgentRepairCalled, s.m_lightSprite != nullptr)) {
        case scenery::RepairAction::CallMechanic:
            repair::repair_facade::AddJob(GetWorld()->m_repairManager, static_cast<int>(s.m_id), 1);
            s.m_calledRepairing = 1;
            break;
        case scenery::RepairAction::Flicker: s.m_flashLight = scenery::FlashLight(Rand()); break;
        default: break;
        }
        // Not in scenery_logic.h: the cleaner-claimed check is a game call and must stay short-circuited in this order.
        if (def.m_clean && s.m_NumItems == def.m_Capacity && !s.m_vandalised && !s.IsCleanerClaimed() && !s.m_calledCleaning &&
                !s.m_urgentCleaningCalled) {
            cleaning::cleaning_facade::AddJob(GetWorld()->m_cleaningManager, static_cast<int>(s.m_id));
            s.m_calledCleaning = 1;
        }
    }
    if (s.m_state == 0 || s.m_state == 4)
        for (uint32_t i = 0; i < static_cast<uint32_t>(s.m_MaxNumAttachedGuests); ++i)
            if (s.m_AttachedGuest[i] != -1) {
                auto* guest = object_manager::ObjectById(GetObjectManager(), s.m_AttachedGuest[i]);
                if (scenery::IsSeatedGuestState(guest->m_state)) continue;
                // Fixed bug: the game used to end with "Guest thinks they've no Longer got <key>" when a seated guest left its
                // seat state without standing up (seen on a beach-hut lounger in scenario 9). The seat is released instead, with the
                // stand-up message the guest's script would have posted.
                Message standUp{};
                standUp.sender = s.m_AttachedGuest[i];
                standUp.receiver = static_cast<int>(s.m_id);
                standUp.type = MT_STATECHANGE;
                standUp.data = 0;
                s.ReceiveMessage(&standUp);
            }
    return 1;
}
uint8_t RunEnter(Scenery* s, uint32_t state)
{
    g_enter.Count();
    return Enter(*s, state);
}
uint8_t RunUpdate(Scenery* s)
{
    g_update.Count();
    return Update(*s);
}
} // namespace

bool Scenery::OnEnterState(uint32_t state)
{
    return RunEnter(this, state);
}
bool Scenery::Update()
{
    return RunUpdate(this);
}
} // namespace obl::object
