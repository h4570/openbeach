#include "core/char16.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/objects/staff_roster.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "game/objects/staff_variable_text.h"
#include "core/call_counter.h"
#include "game/ui/text_table.h"
#include "game/game/diagnostics.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// The text fields of a staff member's panels (the logic is staff_variable_text.h). Every class but the builder and the base asks its
// object for the text of its current feedback on the way.
namespace obl::object {
using game::FatalError;
using game::GetObjectManager;

namespace {

using object::Staff;

// ---- The game side of the Env of staff_variable_text.h --------------------------------------------------------------------------------

struct GameEnv {
    Staff& member;

    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
    // The host's swprintf: the two-argument form without a count.
    void Print(Char16* out, const Char16* format) { Swprintf()(out, format); }
    void PrintInt(Char16* out, const Char16* format, int value) { Swprintf()(out, format, value); }
    void PrintReal(Char16* out, const Char16* format, double value) { Swprintf()(out, format, value); }
    bool BuildingKey(int32_t id, std::string& key)
    {
        const object::Base* object = object_manager::ObjectById(GetObjectManager(), id);
        if (!object) return false;
        key = static_cast<const object::Building*>(object)->m_definition->m_key;
        return true;
    }
    bool BuildingName(int32_t id, Char16* out)
    {
        object::Base* object = object_manager::ObjectById(GetObjectManager(), id);
        if (!object) return false;
        object->GetVariableText("BUILDING_NAME", out);
        return true;
    }
    // Whether the lifeguard's home tower has its beach open: a pure question to the beach grid.
    bool BeachOpen(int32_t id, bool& open)
    {
        object::Base* object = object_manager::ObjectById(GetObjectManager(), id);
        if (!object) return false;
        open = static_cast<object::Building*>(object)->IsBeachOpenMode(0);
        return true;
    }
    void AskFeedback(uint32_t flags) { member.GetFeedbackText(flags); }
    void Fatal(const char* key) { FatalError("A script read unrecognized staff variable \"%s\"", key); }

private:
    using SwprintfFunc = int(__cdecl*)(Char16*, const Char16*, ...);
    static SwprintfFunc Swprintf() { return ui::host_text::Print; }
};

test::CallCounter g_text[] = {test::CallCounter("objects.staff-text.base"), test::CallCounter("objects.staff-text.builder"),
        test::CallCounter("objects.staff-text.rep"), test::CallCounter("objects.staff-text.cleaner"),
        test::CallCounter("objects.staff-text.lifeguard"), test::CallCounter("objects.staff-text.mechanic"),
        test::CallCounter("objects.staff-text.security-guard")};
enum TextClass { TextBase, TextBuilder, TextRep, TextCleaner, TextLifeguard, TextMechanic, TextSecurityGuard };

} // namespace
} // namespace obl::object

// Each method writes only the buffer: the object stays as it was.
namespace obl::object {
using object::GameEnv;

void Staff::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextBase].Count();
    GameEnv env{*this};
    staff::BaseVariableText(*this, key, out, env);
}
void Builder::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextBuilder].Count();
    GameEnv env{*this};
    staff::BuilderVariableText(*this, key, out, env);
}
void Rep::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextRep].Count();
    GameEnv env{*this};
    staff::RepVariableText(*this, key, out, env);
}
void Cleaner::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextCleaner].Count();
    GameEnv env{*this};
    staff::CleanerVariableText(*this, key, out, env);
}
void Lifeguard::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextLifeguard].Count();
    GameEnv env{*this};
    staff::LifeguardVariableText(*this, key, out, env);
}
void Mechanic::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextMechanic].Count();
    GameEnv env{*this};
    staff::MechanicVariableText(*this, key, out, env);
}
void SecurityGuard::GetVariableText(const char* key, Char16* out)
{
    object::g_text[object::TextSecurityGuard].Count();
    GameEnv env{*this};
    staff::SecurityGuardVariableText(*this, key, out, env);
}
} // namespace obl::object
