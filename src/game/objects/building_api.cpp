#include "game/map/grid_state.h"
#include "game/objects/object_base.h"
#include "game/objects/building_queue.h"
#include "game/objects/building_methods.h"
#include "game/objects/building_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_queries.h"
#include "game/construction/construction_manager.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_cell.h"
#include "game/ui/sprite.h"
#include "game/objects/building_thoughts.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "game/world/world_params_queries.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace obl::object {
using game::GetGrid;
using game::GetWorld;
using game::GetWorldParams;

namespace {

using object::Building;

// The queries and setters of docs/architecture/objects.md.

const auto kQueueTailPoint = &BuildingQueueGetTailPoint;

using object::ThoughtEntry;

// ---- the methods ------------------------------------------------------------------------------------------------

void SetXyz(float (&target)[3], float x, float y, float z)
{
    target[0] = x;
    target[1] = y;
    target[2] = z;
}

void SetTopWorldPositionOf(Building& b, float x, float y, float z)
{
    SetXyz(b.m_TopWorldPos, x, y, z);
}

void SetEntrancePointOf(Building& b, float x, float y, float z)
{
    b.m_Entrance = 1;
    SetXyz(b.m_EntrancePoint, x, y, z);
}

void SetServePointOf(Building& b, float x, float y, float z)
{
    SetXyz(b.m_serve_point, x, y, z);
}

void SetQueueDirectionOf(Building& b, int32_t rotation)
{
    QueueDirection direction;
    if (!QueueDirectionFromRotation(static_cast<uint32_t>(rotation), direction)) return;
    b.m_queue_direction[0] = direction.x;
    b.m_queue_direction[1] = direction.y;
}

void ServePositionOf(const Building& b, float* out)
{
    out[0] = b.m_serve_point[0];
    out[1] = b.m_serve_point[1];
    out[2] = b.m_serve_point[2];
}

// The tail of the queue (x, y; z = 0), or the serve point when the building has no queue.
void QueueTailPositionOf(const Building& b, float* out)
{
    if (!b.m_queue) {
        ServePositionOf(b, out);
        return;
    }
    float tail[2];
    kQueueTailPoint(b.m_queue, tail);
    out[0] = tail[0];
    out[1] = tail[1];
    out[2] = 0.0f;
}

bool IsOpenOf(const Building& b)
{
    return b.m_OpenStatus == 0;
}

double AverageOf(const Building& b, int column)
{
    return AverageIntensity(&b.m_Commod_Intensity[0][column], b.m_definition->m_NumCommodities);
}

int32_t OverlayYOffsetOf(const Building& b)
{
    const auto* instance = static_cast<const ui::SpriteInstance*>(b.m_sprite);
    return OverlayYOffset(instance->m_sprite->m_anims[instance->m_animIndex]->m_hotspotY);
}

bool IsSelectableOf(const Building& b)
{
    return !ascii::EqualsNoCase(b.m_definition->m_Menu, "None");
}

bool HasBuildJobOf(const Building& b)
{
    return ProgressIsNonZero(construction::runtime::IsBuilding(GetWorld()->m_constructionManager, b.m_id));
}
bool HasDemolishJobOf(const Building& b)
{
    return ProgressIsNonZero(construction::runtime::IsDemolishing(GetWorld()->m_constructionManager, b.m_id));
}

int32_t ThoughtObjectIdOf(const Building& b, int32_t row)
{
    // A row past the list answers no object.
    const ThoughtEntry* entry = thought_ops::At(b.m_ThoughtList, row);
    return entry ? entry->m_objectId : 0;
}

// GetWalkPoint: entry `index` of the walk-point table (unchecked) as an offset into the footprint, turned by the sprite's
// direction into a world point.

void WalkPointOf(Building& b, int32_t index, float* out)
{
    const float offsetX = b.m_walkPoints[index][0];
    const float offsetY = b.m_walkPoints[index][1];
    int tile[2];
    b.GetTilePosition(tile);
    const auto* sprite = static_cast<const ui::SpriteInstance*>(b.m_sprite);
    const grid_math::PathPoint point = GetGrid()->GetFootprintOffsetPoint(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]),
            b.m_definition->m_GridWidth, b.m_definition->m_GridHeight, offsetX, offsetY, static_cast<uint32_t>(sprite->m_direction));
    out[0] = point.x;
    out[1] = point.y;
}

// GetProviderScore: the commodity is named by the *preference* index (`def.m_Commodity[preference]`, not by the slot: a quirk
// the callers live with); LIVEVENT reads the intensity of the show in progress, anything else the building's own table.
float ProviderScoreOf(const Building& b, int32_t slot, int32_t preference)
{
    const char* key = world_params_queries::GetCommodityName(*GetWorldParams(), b.m_definition->m_Commodity[preference]);
    if (!ascii::EqualsNoCase(key, "LIVEVENT")) return (&b.m_Commod_Intensity[0][0])[slot * 2 + preference];
    const object::BuildingEvents& events = *b.m_events;
    return events.m_slots[events.m_CurrentSlot].m_intensity[preference != 0 ? 1 : 0];
}

// The slot that sells beer, or kNoBeerSlot. The loop compares the display text (CommodityDef::m_name) with "Beer".
int32_t BeerSlotOf(const Building& b)
{
    const tables::WorldParams& params = *GetWorldParams();
    const tables::BuildingDef& def = *b.m_definition;
    return FindBeerSlot(static_cast<uint32_t>(def.m_NumCommodities),
            [&](uint32_t i) { return world_params_queries::RecordAt(params.m_commodities, def.m_Commodity[i])->m_name.c_str(); });
}

// Writes the cost per serving of the current beer at its strength into the beer slot. A building without a beer slot gets it at
// m_Commod_Cost[-1] (SetBeerStrength does not check the search result), which is the last price factor.
void StoreBeerCostOf(Building& b, int32_t slot)
{
    const tables::BeerDef* beer = world_params_queries::GetBeerDefinition(*GetWorldParams(), b.m_BeerType);
    const float cost = BeerSlotCost(beer->m_BaseCost, beer->m_StrCost, b.m_BeerStrength);
    (slot == kNoBeerSlot ? b.m_Commod_PriceFactor[4] : b.m_Commod_Cost[slot]) = cost;
}

void SetBeerStrengthOf(Building& b, float strength)
{
    b.m_BeerStrength = strength;
    StoreBeerCostOf(b, BeerSlotOf(b));
}

// A different beer: the slot's price becomes the beer's starting price and the cost follows. Without a beer slot only the type changes.
void SetBeerTypeOf(Building& b, int32_t beerId)
{
    if (b.m_BeerType == beerId) return;
    b.m_BeerType = beerId;
    const int32_t slot = BeerSlotOf(b);
    if (slot == kNoBeerSlot) return;
    StoreBeerCostOf(b, slot);
    b.m_Commod_Price[slot] = world_params_queries::GetBeerDefinition(*GetWorldParams(), beerId)->m_Price;
}

// ---- the call counters ----------------------------------------------------------------------------------------------------

// Each counter counts the calls a facade answered.
using Counter = test::CallCounter;

Counter g_top("objects.building-set-top-world-position");
Counter g_entrance("objects.building-set-entrance-point");
Counter g_servePoint("objects.building-set-serve-point");
Counter g_queueDirection("objects.building-set-queue-direction-from-rotation");
Counter g_servePosition("objects.building-get-serve-position");
Counter g_queueTail("objects.building-get-queue-tail-position");
Counter g_hasState("objects.building-has-state");
Counter g_isOpen("objects.building-is-open");
Counter g_male("objects.building-get-average-male-intensity");
Counter g_female("objects.building-get-average-female-intensity");
Counter g_overlay("objects.building-get-overlay-y-offset");
Counter g_selectable("objects.building-is-selectable");
Counter g_buildJob("objects.building-has-build-job");
Counter g_demolishJob("objects.building-has-demolish-job");
Counter g_thoughtId("objects.building-get-thought-object-id");
Counter g_walkPoint("objects.building-get-walk-point");
Counter g_providerScore("objects.building-get-provider-score");
Counter g_beerType("objects.building-set-beer-type");
Counter g_beerStrength("objects.building-set-beer-strength");

} // namespace

// ---- the facades -----------------------------------------------------------------------------------------------------------

namespace building_api {

ProviderScoreObserver g_providerScoreObserver = nullptr;

void SetTopWorldPosition(Building* building, float x, float y, float z)
{
    g_top.Count();
    SetTopWorldPositionOf(*building, x, y, z);
}

void SetEntrancePoint(Building* building, float x, float y, float z)
{
    g_entrance.Count();
    SetEntrancePointOf(*building, x, y, z);
}

void SetServePoint(Building* building, float x, float y, float z)
{
    g_servePoint.Count();
    SetServePointOf(*building, x, y, z);
}

void SetQueueDirectionFromRotation(Building* building, int32_t rotation)
{
    g_queueDirection.Count();
    SetQueueDirectionOf(*building, rotation);
}

float* GetServePosition(const Building* building, float* out)
{
    g_servePosition.Count();
    ServePositionOf(*building, out);
    return out;
}

float* GetQueueTailPosition(const Building* building, float* out)
{
    g_queueTail.Count();
    QueueTailPositionOf(*building, out);
    return out;
}

bool HasState(const Building*, uint32_t state)
{
    g_hasState.Count();
    return BuildingStateIsKnown(state);
}

bool IsOpen(const Building* building)
{
    g_isOpen.Count();
    return IsOpenOf(*building);
}

bool IsSelectable(const Building* building)
{
    g_selectable.Count();
    return IsSelectableOf(*building);
}

bool HasBuildJob(const Building* building)
{
    g_buildJob.Count();
    return HasBuildJobOf(*building);
}

bool HasDemolishJob(const Building* building)
{
    g_demolishJob.Count();
    return HasDemolishJobOf(*building);
}

double GetAverageMaleIntensity(const Building* building)
{
    g_male.Count();
    return AverageOf(*building, 0);
}

double GetAverageFemaleIntensity(const Building* building)
{
    g_female.Count();
    return AverageOf(*building, 1);
}

int32_t GetOverlayYOffset(const Building* building)
{
    g_overlay.Count();
    return OverlayYOffsetOf(*building);
}

int32_t GetThoughtObjectId(const Building* building, int32_t row)
{
    g_thoughtId.Count();
    return ThoughtObjectIdOf(*building, row);
}

float* GetWalkPoint(Building* building, float* out, int32_t index)
{
    g_walkPoint.Count();
    WalkPointOf(*building, index, out);
    return out;
}

float GetProviderScore(const Building* building, int32_t slot, int32_t preference)
{
    g_providerScore.Count();
    const float score = ProviderScoreOf(*building, slot, preference);
    if (g_providerScoreObserver) g_providerScoreObserver(building, slot, preference, score);
    return score;
}

void SetBeerType(Building* building, int32_t beerId)
{
    g_beerType.Count();
    SetBeerTypeOf(*building, beerId);
}

void SetBeerStrength(Building* building, float strength)
{
    g_beerStrength.Count();
    SetBeerStrengthOf(*building, strength);
}

} // namespace building_api

} // namespace obl::object

// Building queries used by loop sounds, scenery and staff dispatch through these virtual methods.
namespace obl::object {
bool Building::HasState(uint32_t state)
{
    return object::building_api::HasState(this, state);
}
int Building::GetOverlayOffset()
{
    return object::building_api::GetOverlayYOffset(this);
}
bool Building::IsSelectable()
{
    return object::building_api::IsSelectable(this);
}
} // namespace obl::object
