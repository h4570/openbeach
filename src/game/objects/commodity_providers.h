#pragma once

#include "core/char16.h"
#include "game/tables/commodity_providers.h"
#include "game/objects/object_building.h"

namespace obl::object {

// The commodity provider layer of the buildings: `Rebuild`,
// `FindProvider`,
// `IsSlotUsable`, `SlotPrice` and
// `SlotReferencePrice`, on commodity::Providers. The only
// registrations are the call-drive drivers.
bool RegisterCommodityProviderDrivers();

// The provider lists the world owns: made by the world's constructor with new, freed by its destructor. The global
// g_CommodityProviders is gone; the current set is World::m_commodityProviders.
namespace commodity_providers {
commodity::Providers* Create();
void Destroy(commodity::Providers* providers);
} // namespace commodity_providers

// Can guests use this slot of the building now? Everything but the six rented commodities (LILOS, SURFHIRE, SURFBOARD,
// BOATHIRE, SNORKEL, PEDHIRE) is always usable; those need the sea area the building stands in (found through the beach area
// chain, see the .cpp) to be open. Needs a running world (object table, grid).
bool IsSlotUsable(int buildingId, int slot);

// Arguments of one commodity provider search. `closed` and `beachClosed` are in/out counters
// the caller initialises; the search adds the refusals it meets (and zeroes both on an empty list).
struct ProviderSearch {
    int preference; // gender of the guest: selects the male/female score of a slot
    int family; // commodity family (desire slot) 0..25
    const int* tile; // guest tile {x, y}
    int excludedId; // building the guest just left, or -1
    int guestId; // guest the refusal thoughts are written for
};
struct ProviderAnswer {
    int id = 0; // the winner (building id); after a failed search the building the last refusal was counted for
    int slot = 0;
    bool found = false;
    bool retry = false; // a candidate was only skipped (excluded or temporarily shut), asking again later may succeed
    int broken = 0; // never counted: the broken test compares (3 - RepairState) * 0.33 with 3.0, which never holds
    // When no refusal assigned id/slot they stay 0 and these flags say so.
    bool idSet = false, slotSet = false;
};
// Where the refusal thoughts go: nullptr writes them into the building's thought list, any other sink gets them instead.
using ThoughtSink = void (*)(void* context, object::Building& building, const Char16* text, int guestId);

// FindProvider: the best open provider of a family for a guest, scored by distance and the slot's
// male/female score plus a random tie-breaker (one rand() per candidate, one more per refusal thought).
ProviderAnswer FindProvider(commodity::Providers& providers, const ProviderSearch& search, int& closed, int& beachClosed,
        ThoughtSink sink = nullptr, void* context = nullptr);

// Rebuild, called by the world update every frame and by the world's constructor: when the number of buildings changed,
// rebuilds the per-family provider lists from the buildings' definitions (one record per building slot whose commodity is
// in the family, in building order, then slot order) into arrays on the game's heap and frees the old ones.
void Rebuild(commodity::Providers& providers);

// What one FindProvider call saw and answered, for the tests' model of the rules (called after every call, whichever side computed it).
struct ProviderSearchRecord {
    ProviderSearch search;
    int tile[2];
    int closedBefore, beachClosedBefore;
    int result;
    int slot, closed, beachClosed, broken;
    bool found, retry;
};
using ProviderSearchObserver = void (*)(const ProviderSearchRecord&);
void SetProviderSearchObserver(ProviderSearchObserver observer);

// The two price queries return the exact product of two floats (or one float), so the
// value is a double. The callers (the guest's price score and price opinion functions, its GetValue) use it unrounded.
double SlotPrice(const object::Building& building, int slot);
double SlotReferencePrice(const object::Building& building, int slot);

// Provider query adapters record diagnostic call counts. Rebuild refreshes the cache only when the building count changes;
// FindProvider reports the search outcome to the registered observer.
namespace commodity_providers {
void Rebuild(commodity::Providers* providers);
int FindProvider(commodity::Providers* providers, int* slot, char* found, int* broken, int* closed, int* beachClosed, char* retry,
        int preference, int family, int* tile, int excludedId, int guestId);
bool IsSlotUsable(commodity::Providers* providers, int buildingId, int slot);
double SlotPrice(object::Building* building, int slot);
double SlotReferencePrice(object::Building* building, int slot);
} // namespace commodity_providers

} // namespace obl::object
