#pragma once

#include "game/messages/message_state.h"

namespace obl::object {
struct Base;
}

namespace obl::messages {

namespace message_facade {

// Create binds the current handler (the one every post goes through, game::Context) and Destroy clears it.
messages::Handler* Create();
void Destroy(messages::Handler* handler);
messages::Handler* Current();
void SetCurrent(messages::Handler* handler);

bool Post(messages::Handler& handler, messages::Message& message);
bool PostDelayed(messages::Handler& handler, messages::Message& message, float delay);
void Update(messages::Handler& handler);
void ClearDelayedForReceiver(messages::Handler& handler, int receiver);
// `receiver` is the object the object manager's lookup found (null for an unknown id); `name` is only used by the world branch.
char Dispatch(messages::Handler& handler, object::Base* receiver, messages::Message& message, const char* name);

bool Post(messages::Handler* handler, messages::Message* message);
bool PostDelayed(messages::Handler* handler, messages::Message* message, float delay);
void Update(messages::Handler* handler);
void ClearDelayedForReceiver(messages::Handler* handler, int receiver);

using PostProbe = bool (*)(messages::Handler* handler, messages::Message* message);
using PostDelayedProbe = bool (*)(messages::Handler* handler, messages::Message* message, float delay);
extern PostProbe g_postProbe;
extern PostDelayedProbe g_postDelayedProbe;

} // namespace message_facade
} // namespace obl::messages
