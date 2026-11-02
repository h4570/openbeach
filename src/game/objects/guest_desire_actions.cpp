#include "game/objects/guest.h"
#include "game/world/world_params.h"

#include "game/objects/guest_desire_names.h"
#include "game/game/game_accessors.h"
#include "core/fatal.h"
#include "game/objects/guest_desire_actions.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstdint>

// MissedDesire, AlterDesires and SetupStartValues (src/game/objects/guest_desire_actions.h, docs/architecture/objects.md,
// guest desires: satisfaction and misses). The script calls the first two through the guest's CallFunction (ids 0x1f and 5), the visit helpers call MissedDesire
// directly.
namespace obl::object {
using game::GetWorldParams;

namespace {

using object::Building;
using object::Guest;

test::CallCounter g_missed("objects.guest-missed-desire");
test::CallCounter g_alter("objects.guest-alter-desires");
test::CallCounter g_setup("objects.guest-setup-start-values");

const char* const* DesireNames()
{
    return guest_search::kDesireNames;
}

// ---- the guest's current building and the key of the commodity family it chose ---------------------------------------------------------

enum class Resolved { Nothing, Family, OutOfRange };

// m_DestObjectID must name a building (type 2); the family is that of the commodity in slot m_desired_commod of its definition. A call with
// indices outside the slot, commodity or family tables ends in UnhandledInput, before anything is written.
Resolved ResolveFamily(Guest& guest, const char*& key, Building** building = nullptr)
{
    if (guest.m_DestObjectID == -1) return Resolved::Nothing;
    object::Base* object = FindObjectById(guest.m_DestObjectID);
    if (!object) return Resolved::OutOfRange;
    if (object->m_type != object::ObjectType::Building) return Resolved::Nothing;
    auto* destination = static_cast<Building*>(object);
    if (building) *building = destination;
    if (!destination->m_definition || static_cast<uint32_t>(guest.m_desired_commod) >= std::size(destination->m_definition->m_Commodity))
        return Resolved::OutOfRange;
    const int32_t commodity = destination->m_definition->m_Commodity[guest.m_desired_commod];
    if (static_cast<uint32_t>(commodity) >= static_cast<uint32_t>(tables::WorldParams::kMaxCommodities)) return Resolved::OutOfRange;
    const int32_t family = world::world_params::GetCommodityFamilyOf(GetWorldParams(), commodity);
    if (static_cast<uint32_t>(family) >= static_cast<uint32_t>(tables::WorldParams::kMaxCommodityFamilies)) return Resolved::OutOfRange;
    key = GetWorldParams()->m_commodityFamilies[family].m_key.c_str();
    return Resolved::Family;
}

// ---- the functions -----------------------------------------------------------------------------------------------------------------------

bool Missed(Guest& guest)
{
    const char* family = nullptr;
    switch (ResolveFamily(guest, family)) {
    case Resolved::OutOfRange: return false;
    case Resolved::Nothing: return true;
    case Resolved::Family: break;
    }
    guest_desire_actions::MissedDesire(guest, *guest.m_definition, family, DesireNames());
    return true;
}

bool Alter(Guest& guest)
{
    const char* family = nullptr;
    switch (ResolveFamily(guest, family)) {
    case Resolved::OutOfRange: return false;
    case Resolved::Nothing: guest.m_NumTimesBlocked = 0; return true;
    case Resolved::Family: break;
    }
    guest_desire_actions::AlterDesires(guest, family, DesireNames());
    return true;
}

bool Setup(Guest& guest)
{
    guest_desire_actions::SetupStartValues(guest, *guest.m_definition);
    return true;
}

// ---- entries ------------------------------------------------------------------------------------------------------------------------

template<bool (*Perform)(Guest&)> void Dispatch(Guest* guest, const char* name, test::CallCounter& counter)
{
    if (!Perform(*guest)) fatal::UnhandledInput(name, "an index outside the tables");
    counter.Count();
}

} // namespace

void Guest::AlterDesires()
{
    Dispatch<Alter>(this, "objects.guest-alter-desires", g_alter);
}

void Guest::SetupStartValues()
{
    Dispatch<Setup>(this, "objects.guest-setup-start-values", g_setup);
}

void Guest::MissedDesire()
{
    Dispatch<Missed>(this, "objects.guest-missed-desire", g_missed);
}

} // namespace obl::object
