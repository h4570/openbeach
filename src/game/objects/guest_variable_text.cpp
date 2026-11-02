#include "core/char16.h"
#include "game/objects/guest.h"
#include "game/objects/object_registry_api.h"

#include "game/objects/guest_desire_names.h"
#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/objects/guest_variable_text.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scratch.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/game/diagnostics.h"

#include <array>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace obl::object {
using game::FatalError;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;

namespace {

using object::Guest;
test::CallCounter g_counter("objects.guest-get-variable-text");
constexpr size_t kBufferChars = 512;

struct GameEnv {
    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
    const Char16* Text(int id) { return ui::text_table::GetString(id); }
    const char* DesireName(int id) { return guest_search::kDesireNames[id]; }
    const char* BuildingKey(int type) { return world::world_params::GetBuildingKey(GetWorldParams(), type); }
    int GameDay() { return GetWorld()->m_gameDay; }
    bool CurrentCommodity(const Guest& guest, std::string& name)
    {
        auto* object = object_manager::ObjectById(GetObjectManager(), guest.m_CurID);
        if (!object) return false;
        const auto* building = static_cast<const object::Building*>(object);
        name = world::world_params::GetCommodityName(GetWorldParams(), building->m_definition->m_Commodity[guest.m_desired_commod]);
        return true;
    }
    using PrintFunc = int(__cdecl*)(Char16*, const Char16*, ...);
    static PrintFunc Printer() { return ::obl::ui::host_text::Print; }
    void Print(Char16* out, const Char16* format) { Printer()(out, format); }
    void PrintInt(Char16* out, const Char16* format, int value) { Printer()(out, format, value); }
    void PrintReal(Char16* out, const Char16* format, double value) { Printer()(out, format, value); }
    void Fatal(const char* key) { FatalError("A script read unrecognized guest variable \"%s\"", key); }
};

std::vector<std::string> Keys()
{
    std::vector<std::string> keys = {"GUEST_NAME", "GUEST_CURRENTACTIVITY", "GUEST_DESIRE", "GUEST_DESIRE_0", "GUEST_THOUGHT_0",
            "GUEST_THOUGHT_1", "GUEST_THOUGHT_2", "GUEST_BEST_TODAY", "GUEST_WORST_TODAY", "GUEST_ILL", "GUEST_OPINION_OVERALL",
            "GUEST_DRUNK", "GUEST_DRUNK_LEVEL", "GUEST_SUNBURNT_LEVEL", "GUEST_OXYGEN_LEVEL", "GUEST_HAPPY_PERCENT", "GUEST_HAPPY_VALUE",
            "GUEST_CASH", "GUEST_ARRIVED", "GUEST_DEPART"};
    for (const char* desire : guest_text::kDesires) keys.push_back(std::string("GUEST_DESIRE_") + desire);
    for (const char* opinion : guest_text::kOpinions) keys.push_back(std::string("GUEST_OPINION_") + opinion);
    return keys;
}

} // namespace

void ForceGuestVariableText()
{
    object::Manager* manager = GetObjectManager();
    if (!manager || manager->m_guests.size() == 0) return;
    auto* guest = static_cast<Guest*>(manager->m_guests[0]);
    // Other forced helpers may temporarily leave inconsistent activity fields on the live guest.
    Scratch<Guest> copy(*guest); // shares the guest's sprites and name: its teardown must not run
    copy->m_activity = 0;
    copy->m_state = 12;
    copy->m_Best = copy->m_Worst = -2;
    for (int& thought : copy->m_Thoughts) thought = -1;
    std::array<Char16, kBufferChars> out;
    for (const auto& key : Keys()) copy->GetVariableText(key.c_str(), out.data());
}

} // namespace obl::object

namespace obl::object {
void Guest::GetVariableText(const char* key, Char16* out)
{
    GameEnv env;
    g_counter.Count();
    guest_text::GetVariableText(*this, key, out, env);
}
} // namespace obl::object
