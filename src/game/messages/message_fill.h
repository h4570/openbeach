#pragma once
#include "game/messages/message_state.h"

// Fills the fields of a message in place and returns it, for building messages in one expression.
namespace obl::message_fill {
inline messages::Message* Fill(messages::Message* m, int sender, int receiver, int type, int flags, int data)
{
    m->sender = sender;
    m->receiver = receiver;
    m->type = type;
    m->flags = flags;
    m->data = data;
    return m;
}
inline messages::Message* FillWithFloat(
        messages::Message* m, int sender, int receiver, int type, int flags, int data, float floatData, int member)
{
    Fill(m, sender, receiver, type, flags, data);
    m->floatData = floatData;
    m->extra = member;
    return m;
}
} // namespace obl::message_fill
