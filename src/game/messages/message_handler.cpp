#include "game/messages/message_handler.h"
#include "game/game/game_context.h"
#include "game/messages/message_lifecycle.h"
#include "game/messages/message_logic.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_base.h"

#include "game/game/game_accessors.h"
#include "game/world/world_receive.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::messages {
namespace {

using layout::World;
using messages::Handler;
using messages::Message;
namespace om = obl::object_manager;

// Call counters (`call-report <name>`).
test::CallCounter g_postCalls("messages.handler-post");
test::CallCounter g_postDelayedCalls("messages.handler-post-delayed");
test::CallCounter g_updateCalls("messages.handler-update");
test::CallCounter g_clearCalls("messages.handler-clear-delayed-for-receiver");
test::CallCounter g_dispatchCalls("messages.handler-dispatch");
test::CallCounter g_constructCalls("messages.handler-construct");
test::CallCounter g_destructCalls("messages.handler-destruct");

// The world side of the logic's Env (message_logic.h): the clock, the pause flag and the delivery to a receiver id.
struct GameEnv {
    Handler& handler;
    float Now() { return game::GetWorld()->m_gameTime; }
    bool Paused() { return game::GetWorld()->m_paused != 0; }
    void Deliver(Message& message)
    {
        message_facade::Dispatch(handler, object::object_manager::ObjectById(game::GetObjectManager(), message.receiver), message, nullptr);
    }
};

} // namespace

namespace message_facade {

namespace {

// The id of the first building found; -1 (no receiver) when the search found nothing.
int FirstObjectId(const om::ObjectList& found)
{
    return found.empty() ? -1 : static_cast<int>(found.front()->m_id);
}

void RouteToObject(Handler& handler, const Message& base)
{
    Message copy = base;
    GameEnv env{handler};
    messages::Post(handler, env, copy);
}

char DispatchGuestServed(Handler& handler, const Message& message)
{
    object::Manager* objects = game::GetObjectManager();
    Message copy = message;
    auto found = om::FindBuildings(*objects, BuildingBehavior::ShipStop);
    copy.receiver = FirstObjectId(found);
    if (found.empty()) {
        found = om::FindBuildings(*objects, BuildingBehavior::BusStop);
        copy.receiver = FirstObjectId(found);
    }
    RouteToObject(handler, copy);
    found = om::FindBuildings(*objects, BuildingBehavior::ComplaintsHut);
    copy.receiver = FirstObjectId(found);
    if (!found.empty()) RouteToObject(handler, copy);
    return static_cast<char>(found.size());
}

} // namespace

Handler* Create()
{
    auto* handler = new Handler();
    SetCurrent(handler);
    g_constructCalls.Count();
    return handler;
}

void Destroy(Handler* handler)
{
    if (Current() == handler) SetCurrent(nullptr);
    g_destructCalls.Count();
    delete handler;
}

Handler* Current()
{
    return game::GetContext().messageHandler;
}

void SetCurrent(Handler* handler)
{
    game::GetContext().messageHandler = handler;
}

char Dispatch(Handler& handler, object::Base* receiver, Message& message, const char* name)
{
    g_dispatchCalls.Count();
    if (message.receiver != messages::kMessageReceiverWorld) {
        if (!receiver) return 0;
        return receiver->ReceiveMessage(&message) ? 1 : 0;
    }
    if (message.type != messages::MT_GUEST_SERVED) return world::ReceiveMessage(*game::GetWorld(), message, name);
    return DispatchGuestServed(handler, message);
}

PostProbe g_postProbe = nullptr;
PostDelayedProbe g_postDelayedProbe = nullptr;

bool Post(Handler& handler, Message& message)
{
    g_postCalls.Count();
    if (g_postProbe && g_postProbe(&handler, &message)) return true;
    GameEnv env{handler};
    messages::Post(handler, env, message);
    return true;
}

bool PostDelayed(Handler& handler, Message& message, float delay)
{
    g_postDelayedCalls.Count();
    if (g_postDelayedProbe && g_postDelayedProbe(&handler, &message, delay)) return true;
    GameEnv env{handler};
    messages::PostDelayed(handler, env, message, delay);
    return true;
}

void Update(Handler& handler)
{
    g_updateCalls.Count();
    GameEnv env{handler};
    messages::Update(handler, env);
}

void ClearDelayedForReceiver(Handler& handler, int receiver)
{
    g_clearCalls.Count();
    obl::messages::ClearDelayedForReceiver(handler, receiver);
}

bool Post(Handler* handler, Message* message)
{
    return Post(*handler, *message);
}

bool PostDelayed(Handler* handler, Message* message, float delay)
{
    return PostDelayed(*handler, *message, delay);
}

void Update(Handler* handler)
{
    Update(*handler);
}

void ClearDelayedForReceiver(Handler* handler, int receiver)
{
    message_facade::ClearDelayedForReceiver(*handler, receiver);
}

} // namespace message_facade
} // namespace obl::messages
