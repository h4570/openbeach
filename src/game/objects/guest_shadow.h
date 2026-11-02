#pragma once

#include "game/game/game_accessors.h"
#include "game/objects/guest.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// Helpers of the guest class: the messages its helpers post, the random draws of the test drivers, and the access to guests the test helpers
// (ForceGuest*) and drivers use.
namespace obl::object::guest_shadow {

// The fields past data are left as they were; nobody reads them.
inline void Post(uint32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{};
    message.sender = static_cast<int>(sender);
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

inline void PostDelayed(uint32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data, float delay)
{
    messages::Message message{};
    message.sender = static_cast<int>(sender);
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    messages::message_facade::PostDelayed(messages::message_facade::Current(), &message, delay);
}

struct Rng {
    uint32_t seed;
    uint32_t Next() { return (seed = seed * 1664525u + 1013904223u) >> 8; }
    float Fraction() { return static_cast<float>(Next() & 0xffff) / 65536.0f; }
};

inline object::Guest* GuestAt(uint32_t index)
{
    object::Manager* manager = game::GetObjectManager();
    if (!manager || index >= manager->m_guests.size()) return nullptr;
    return static_cast<object::Guest*>(manager->m_guests[index]);
}

// The whole guest, restored on destruction.
class GuestBackup {
public:
    explicit GuestBackup(object::Guest* guest) : m_guest(guest), m_bytes(sizeof(object::Guest))
    {
        std::copy_n(reinterpret_cast<const uint8_t*>(guest), m_bytes.size(), m_bytes.begin());
    }
    ~GuestBackup() { std::copy(m_bytes.begin(), m_bytes.end(), reinterpret_cast<uint8_t*>(m_guest)); }

private:
    object::Guest* m_guest;
    std::vector<uint8_t> m_bytes;
};

} // namespace obl::object::guest_shadow
