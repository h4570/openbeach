#pragma once

#include <cstdint>

namespace obl::object {

struct Building;
} // namespace obl::object

namespace obl::object {

// The building_api:: facades answer the small Building queries and setters (docs/architecture/objects.md)
// with the building as the first argument, like the game's member call did. The pure logic is in src/game/objects/building_queries.h.
// Every facade keeps its own call counter.
namespace building_api {

void SetTopWorldPosition(object::Building* building, float x, float y, float z);
void SetEntrancePoint(object::Building* building, float x, float y, float z);
void SetServePoint(object::Building* building, float x, float y, float z);
void SetQueueDirectionFromRotation(object::Building* building, int32_t rotation);
// The getters fill `out` (x, y, z, or x, y) and return it.
float* GetServePosition(const object::Building* building, float* out);
float* GetQueueTailPosition(const object::Building* building, float* out);
// `building` is never read; only the state is.
bool HasState(const object::Building* building, uint32_t state);
bool IsOpen(const object::Building* building);
bool IsSelectable(const object::Building* building);
bool HasBuildJob(const object::Building* building);
bool HasDemolishJob(const object::Building* building);
double GetAverageMaleIntensity(const object::Building* building);
double GetAverageFemaleIntensity(const object::Building* building);
int32_t GetOverlayYOffset(const object::Building* building);
int32_t GetThoughtObjectId(const object::Building* building, int32_t row);
// Entry `index` of the walk-point table as a world point in `out`; the argument order is (building, out, index).
float* GetWalkPoint(object::Building* building, float* out, int32_t index);
float GetProviderScore(const object::Building* building, int32_t slot, int32_t preference);
void SetBeerType(object::Building* building, int32_t beerId);
void SetBeerStrength(object::Building* building, float strength);

// Test seam of the commodity provider scores: when set, called after every GetProviderScore with its answer. Null in the game.
using ProviderScoreObserver = void (*)(const object::Building* building, int32_t slot, int32_t preference, float score);
extern ProviderScoreObserver g_providerScoreObserver;

} // namespace building_api
} // namespace obl::object
