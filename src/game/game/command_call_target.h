#pragma once

#include "game/cleaning/cleaning_logic.h"
#include "game/tables/world_params_leaves.h"

namespace obl::command {

// The scenery that call-repair and call-cleaner may name. A scenery dialog (a lamp's, a bin's) has the same call buttons as a building's,
// and the repair and cleaning queues take scenery. A job is accepted only for scenery its queue can do something with: the repair queue
// only for repairable scenery, the cleaning queue only for the kinds a cleaner knows (litter and bins, by name). Any other job would wait
// in the queue forever. Returns nullptr for a valid target, otherwise the reason for the refusal.
enum class Call { Repair, Clean };
inline const char* SceneryCallRefusal(Call call, const tables::SceneryDef& def, const char* name)
{
    if (call == Call::Repair) return def.m_repair ? nullptr : "scenery cannot be repaired";
    return cleaning::TypeOfName(name) != cleaning::kTypeNone ? nullptr : "scenery cannot be cleaned";
}

} // namespace obl::command
