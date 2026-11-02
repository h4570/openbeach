#include "game/objects/object_static.h"
#include "game/ui/sprite_instance_geometry.h"

#include "core/fatal.h"
#include "game/game/game_accessors.h"
#include "game/ui/map_view_access.h"
#include "game/objects/person_methods.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <cstdint>
#include "game/ui/sprite.h"
#include "game/game/diagnostics.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_hit_test.h"

// The methods of Person (the logic is person_methods.h). The class changes no state of its own: each method passes on to the
// Mover method of the same job (the Env below).
namespace obl::object {
namespace {
using messages::Message;
using script::ScriptValue;

// The Mover methods of the same job.
struct GameEnv {
    ScriptValue BaseGet(Person& human, int32_t key, intptr_t data)
    {
        ScriptValue out{};
        human.Mover::GetValue(&out, key, data);
        return out;
    }
    void BaseSet(Person& human, int32_t key, intptr_t data, int32_t type, int32_t bits) { human.Mover::SetValue(key, data, type, bits); }
    bool BaseReceive(Person& human, Message* message) { return human.Mover::ReceiveMessage(message); }
    bool BaseUpdate(Person& human) { return human.Mover::Update(); }
    void Fatal(const char* name) { game::FatalError("Movement update of human \"%s\" failed", name); }
};

// Everything TrySelectAtScreenPoint asks of the game about one object.
struct SelectEnv {
    Person& human;
    ui::SpriteInstance* Sprite() const { return static_cast<ui::SpriteInstance*>(human.m_sprite); }
    void WorldToScreen(const float* world, int32_t* out)
    {
        float copy[3] = {world[0], world[1], world[2]};
        int buffer[2] = {};
        const int* at = ui::GetMapView()->ProjectWorldPoint(buffer, copy);
        out[0] = at[0];
        out[1] = at[1];
    }
    int32_t DrawOffsetX() { return Sprite()->GetDrawOffsetX(); }
    int32_t HotspotY() const { return Sprite()->m_sprite->m_anims[Sprite()->m_animIndex]->m_hotspotY; }
    void FirstOpaquePixel(int32_t& x, int32_t& y) { Sprite()->FindFirstOpaque(reinterpret_cast<int*>(&x), reinterpret_cast<int*>(&y)); }
    bool HitTest(const int32_t* point, int32_t* screenPoint)
    {
        int32_t copy[2] = {point[0], point[1]};
        return object::HitTestSelectionRegion(copy, screenPoint);
    }
    void Select() { human.OnSelect(1, 0); } // clicked, without centering the camera
};

} // namespace

ScriptValue* Person::GetValue(ScriptValue* out, int key, intptr_t data)
{
    GameEnv env;
    *out = human::GetValue(*this, key, data, env);
    return out;
}

void Person::SetValue(int key, intptr_t data, int type, int bits)
{
    GameEnv env;
    human::SetValue(*this, key, data, type, bits, env);
}

bool Person::ReceiveMessage(Message* message)
{
    GameEnv env;
    return human::ReceiveMovementMessage(*this, message, env);
}

bool Person::Update()
{
    GameEnv env;
    return human::Update(*this, env);
}

bool Person::TrySelectAtScreenPoint(int32_t* screenPoint)
{
    if (m_visible && !m_sprite) fatal::UnhandledInput("Person::TrySelectAtScreenPoint", "a visible object has no sprite");
    SelectEnv env{*this};
    return human::TrySelectAtScreenPoint(*this, screenPoint, env);
}
} // namespace obl::object
