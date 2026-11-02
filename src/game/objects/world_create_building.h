#pragma once

#include "game/objects/object_building.h"
#include "game/objects/object_create_params.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/world_create_props.h"
#include "game/objects/world_create_staff.h"

#include <bit>

// CreateBuilding of the world: returns the building. Env supplies the game constructors,
// setters, grid/terrain queries, message delivery and the global manager's final insertion by Y.
namespace obl::world_create {

template<class Env> object::Building* CreateBuilding(Env& env, const object::BuildingCreateParams& record)
{
    const auto& def = env.GetBuilding(record.m_key);
    auto* object = static_cast<object::Building*>(env.CreateObjectByKey(record.m_key, object::ObjectType::Building));
    object->m_definition = &def;
    env.SetType(object, object::ObjectType::Building);
    env.SetState(object, 0);
    env.SetBuildingPosition(object, record.m_tileX, record.m_tileY);
    const bool rotated = (record.m_orientation & 1) != 0;
    env.SetGridWidth(object, static_cast<uint8_t>(rotated ? def.m_GridHeight : def.m_GridWidth));
    env.SetGridHeight(object, static_cast<uint8_t>(rotated ? def.m_GridWidth : def.m_GridHeight));
    SetSpriteDirection(*static_cast<ui::SpriteInstance*>(object->m_sprite), record.m_orientation);
    // The queue and footprint queries use the requested rotation, even when the sprite clamps it.
    env.SetQueueDirection(object, record.m_orientation);
    if (def.m_EntranceOffset[0] != 0 || def.m_EntranceOffset[1] != 0) {
        float point[3];
        env.FootprintPoint(point, record, def, def.m_EntranceOffset);
        env.SetEntrancePoint(object, point[0], point[1], point[2]);
    }
    float point[3];
    env.FootprintPoint(point, record, def, def.m_ServeOffset);
    env.SetServePoint(object, point[0], point[1], point[2]);
    object->m_PowerConsumption = static_cast<uint32_t>(def.m_Power);
    for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumCommodities); ++i) {
        // Bits are compared against -1.0f rather than an FP comparison, so a NaN price is still copied.
        if (std::bit_cast<uint32_t>(record.m_prices[i]) != 0xbf800000u) object->m_Commod_Price[i] = record.m_prices[i];
    }
    if (record.m_beerType != -1 && env.BeerEnabled(record.m_beerType)) {
        env.SetBeerType(object, record.m_beerType);
        env.SetBeerStrength(object, record.m_beerStrength);
    }
    object->m_BuildingAge = record.m_age;
    if (record.m_broken != 0) env.SetBroken(object);
    auto message = StartMessage(object->m_id);
    env.Post(message);
    ++env.BuildingCount();
    env.InsertLastBuildingByY();
    return object;
}

} // namespace obl::world_create
