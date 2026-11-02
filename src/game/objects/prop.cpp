#include "game/objects/object_base.h"
#include "game/objects/object_hit_test.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_static.h"
#include "game/objects/prop.h"
#include "game/ui/sprite_instance_geometry.h"

#include "game/game/game_accessors.h"
#include "game/ui/map_view_access.h"
#include "game/messages/message_state.h"
#include "game/ui/renderer_types.h"
#include "game/ui/cursor_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/ui/sprite.h"

#include <cstdint>
#include "game/ui/map_view.h"

// The methods every grid object shares (the logic is prop.h), the vandal claim of scenery and the cursor hit tests
// (object_hit_test.h).
namespace obl::object {
namespace {
using ui::SpriteInstance;
namespace so = prop;
namespace hit = object_hit_test;

test::CallCounter g_calls("objects.static-object");

struct ReceiveEnv {
    bool BaseReceive(Prop& object, const messages::Message& message)
    {
        return object.ReceiveCommonMessage(const_cast<messages::Message*>(&message));
    }
};

struct VandalEnv {
    void Post(const messages::Message& message)
    {
        messages::Message copy = message; // the handler stamps the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
};

struct HitEnv {
    bool IsSelectable(const Base& object) { return const_cast<Base&>(object).IsSelectable(); }
    bool ClipObjectRect(const Base& object) { return ui::GetMapView()->ClipObjectRect(&object); }
    void WorldToScreen(const float world[3], int32_t out[2])
    {
        float copy[3] = {world[0], world[1], world[2]};
        int buffer[2] = {};
        const int* at = ui::GetMapView()->ProjectWorldPoint(buffer, copy);
        out[0] = at[0];
        out[1] = at[1];
    }
    int32_t DrawOffsetX(const SpriteInstance& sprite) { return sprite.GetDrawOffsetX(); }
    void Cursor(int32_t& x, int32_t& y)
    {
        const auto* cursor = game::GetCursor();
        x = cursor->m_x;
        y = cursor->m_y;
    }
    float Zoom() { return game::GetRenderer()->m_zoomScale; }
    bool SpriteHitTest(SpriteInstance& sprite, int x, int y) { return sprite.HitTest(x, y); }
};

} // namespace

Prop::Prop(const char* name) : Base(name)
{
    g_calls.Count();
    so::Construct(*this);
}

bool Prop::Update()
{
    g_calls.Count();
    return so::Update(*this);
}

bool Prop::ReceiveMessage(messages::Message* message)
{
    g_calls.Count();
    ReceiveEnv env;
    return so::ReceiveMessage(*this, *message, env);
}

void Prop::SetGridWidth(uint32_t width)
{
    g_calls.Count();
    so::SetGridWidth(*this, width);
}
void Prop::SetGridHeight(uint32_t height)
{
    g_calls.Count();
    so::SetGridHeight(*this, height);
}
void Prop::SetCleanerClaimed(uint32_t claimed)
{
    g_calls.Count();
    so::SetCleanerClaimed(*this, claimed);
}
void Prop::SetMechanicClaimed(uint32_t claimed)
{
    g_calls.Count();
    so::SetMechanicClaimed(*this, claimed);
}
void Prop::SetCalledRepairing(uint32_t called)
{
    g_calls.Count();
    so::SetCalledRepairing(*this, called);
}
void Prop::SetUrgentCleaningCalled(uint32_t called)
{
    g_calls.Count();
    so::SetUrgentCleaningCalled(*this, called);
}
void Prop::SetUrgentRepairCalled(uint32_t called)
{
    g_calls.Count();
    so::SetUrgentRepairCalled(*this, called);
}
bool Prop::IsCleanerClaimed() const
{
    g_calls.Count();
    return so::IsCleanerClaimed(*this);
}
bool Prop::IsMechanicClaimed() const
{
    g_calls.Count();
    return so::IsMechanicClaimed(*this);
}

void Scenery::SetVandalClaim(uint32_t claimed)
{
    g_calls.Count();
    VandalEnv env;
    so::SetVandalClaim(*this, claimed, env);
}

bool HitTestCursor(Base* object)
{
    HitEnv env;
    return hit::HitTestCursor(*object, env);
}

bool HitTestCursorUnselectable(Base* object)
{
    HitEnv env;
    return hit::HitTestCursorUnselectable(*object, env);
}

bool HitTestSelectionRegion(int32_t* spritePoint, int32_t* screenPoint)
{
    HitEnv env;
    return hit::HitTestSelectionRegion(spritePoint, screenPoint, env);
}
} // namespace obl::object
