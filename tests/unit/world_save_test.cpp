#include "unit_test.h"
#include "game/world/world_state.h"

#include "game/map/grid_state.h"
#include "game/world/world_save.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using world_data::World;
namespace save = world_save;

namespace {
// Collects the names of the field table: the snapshot records and the save-only ones.
struct NameVisitor {
    std::vector<std::string> both, saveOnly;
    void Add(const char* name) { both.emplace_back(name); }
    void Constant(const char* name, uint32_t) { Add(name); }
    void String(const char* name, const char*) { Add(name); }
    void Int(const char* name, int32_t) { Add(name); }
    void UInt(const char* name, uint32_t) { Add(name); }
    void Float(const char* name, float) { Add(name); }
    void Bool(const char* name, uint8_t) { Add(name); }
    void Ints(const char* name, const int*, int, save::Scope scope) { (scope == save::Scope::Both ? both : saveOnly).emplace_back(name); }
    void Floats(const char* name, const float*, int, save::Scope scope)
    {
        (scope == save::Scope::Both ? both : saveOnly).emplace_back(name);
    }
    void ScriptState(const char* name, uint32_t) { Add(name); }
};
} // namespace

OBL_TEST_CASE("world_save.field_table_marks_the_save_only_records")
{
    auto world = std::make_unique<World>();
    const uint32_t scriptState = 0;
    NameVisitor names;
    save::VisitFields(static_cast<const World&>(*world), scriptState, names);
    // 74 records after the class name: the 70 of the snapshot and the boat list and the three histories of the save only.
    OBL_CHECK_EQ(names.both.size(), size_t{69});
    OBL_CHECK(names.saveOnly == (std::vector<std::string>{"boat_list", "profit_history", "income_history", "cost_history"}));
    OBL_CHECK_EQ(names.both.back(), std::string("script_state"));
}

namespace {
std::vector<int> g_destroyed;
void LogTeardown(obl::world::Subsystem which, void*)
{
    g_destroyed.push_back(static_cast<int>(which));
}
} // namespace

OBL_TEST_CASE("world_save.destructor_takes_subsystems_down_in_order_and_skips_null_ones")
{
    g_destroyed.clear();
    obl::world::SetSubsystemTeardown(&LogTeardown);
    {
        auto world = std::make_unique<World>();
        int dummy[4] = {};
        world->m_scriptedSound.reset(reinterpret_cast<audio::ScriptedSound*>(&dummy[2]));
        world->m_grid.reset(reinterpret_cast<obl::map::Grid*>(&dummy[1]));
        world->m_player.reset(reinterpret_cast<world_data::Player*>(&dummy[0]));
        world->m_objectiveText[1] = "a text the World owns";
    }
    obl::world::SetSubsystemTeardown(nullptr);
    using S = save::Subsystem;
    OBL_CHECK(
            g_destroyed == (std::vector<int>{static_cast<int>(S::Player), static_cast<int>(S::Grid), static_cast<int>(S::ScriptedSound)}));
}
