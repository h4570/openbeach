#include "game/ui/sprite_bank.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/entity_lifecycle.h"
#include "game/objects/object_base.h"
#include "game/map/cell_objects.h"
#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"

#include <bit>
#include <cstdint>

// Construction, destruction and script values of the class every object derives from (entity_lifecycle.h has the logic and
// the contract of the Env).
namespace obl::object {
namespace {
using script::ScriptValue;
using ui::SpriteInstance;
namespace life = entity::lifecycle;

test::CallCounter g_construct("objects.base-construct"), g_destruct("objects.base-destruct"), g_getValue("objects.base-get-value");

class LifeEnv {
public:
    void NameTooLong(const char* name) { game::FatalError("Object name is too long: \"%s\"", name); }
    void ReleaseSprite(void* sprite) { static_cast<ui::SpriteBank*>(ui::GetSpriteBank())->ReleaseInstance(sprite); }
    void* CellAt(const uint32_t xyz[3])
    {
        // The position is the raw bits of three floats; the lookup reads only x and y.
        return game::GetGrid()->GetCellAtWorldPoint(std::bit_cast<float>(xyz[0]), std::bit_cast<float>(xyz[1]));
    }
    void RemoveFromCell(void* cell, Base& object) { cell_objects::RemoveObject(*static_cast<map::GridCell*>(cell), object); }
    int Rand() { return game::Rand(); }
    uint32_t ReadWord(intptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }
    uint32_t WorldStartup() { return game::GetWorld()->m_startup; }
    uint32_t GameTimeBits() { return life::Bits(game::GetWorld()->m_gameDayTime); }
    uint32_t CurrentWeather() { return game::GetWeather()->m_currentState; }
    void SetPosition(Base& object, const uint32_t xyz[3]) { object.SetPosition(reinterpret_cast<const float*>(xyz)); }
    int GetDrawOffsetX(SpriteInstance* sprite) { return sprite->GetDrawOffsetX(); }
};

} // namespace

Base::Base(const char* name)
{
    g_construct.Count();
    LifeEnv env;
    life::Construct(*this, name, env);
}

Base::~Base()
{
    g_destruct.Count();
    LifeEnv env;
    life::Destruct(*this, env);
}

ScriptValue* Base::GetValue(ScriptValue* out, int key, intptr_t data)
{
    g_getValue.Count();
    LifeEnv env;
    *out = life::GetValue(*this, key, data, env);
    return out;
}

void Base::SetValue(int key, intptr_t data, int type, int bits)
{
    LifeEnv env;
    life::SetValue(*this, key, data, type, bits, env);
}
} // namespace obl::object
