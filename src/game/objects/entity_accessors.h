#pragma once

#include "game/objects/object_base.h"

#include <cstdint>
#include <cstring>

// The small methods of the class every object derives from (docs/architecture/objects.md; the life cycle, GetValue/SetValue and Save/Load are in
// entity_lifecycle.h): the plain setters and getters, the two senders, the base `Draw` and the "invalid keyword" stub of `GetVariableText`.
// Members:
//   SetWorldXYZ (three raw dwords)    SetType / SetId / SetState
//   GetPosition (out float[3], returns the out pointer)    GetTilePosition (out int[2], returns it)
//   HasBasicState   PlayAnimation (starts at time 0.0)   ClearSelection
//   SendMessage (the 32-byte message by value)   SendMessageDelayed (the message, then the delay)
//   GetVariableText   Draw
// Floats travel as raw bits wherever they are only copied, never computed with, so a NaN pattern is kept.
//
// Env:
//   GetTilePosition:  void WorldToTile(const float world[3], int32_t tile[2])    the map view world-to-tile conversion of the global map view
//   SendMessage:      bool Post(const WireMessage&)                              Post of g_MessageHandler
//                     bool PostDelayed(const WireMessage&, uint32_t delayBits)   PostDelayed of g_MessageHandler
//   GetVariableText:  void InvalidKeyword(const char* key, uint32_t type)        FatalError with k_EntityBadKeywordText (does not return)
//   Draw:             uint32_t GameTimeBits   world game time                void DrawInWorld(void* sprite, const float world[3], uint32_t timeBits)
namespace obl::entity::accessors {

// The message as the senders pass it: messages::Message with the floats as bits.
struct WireMessage {
    uint32_t words[8];
};

inline void SetType(object::Base& base, uint8_t type)
{
    base.m_type = static_cast<object::ObjectType>(type);
}
inline void SetId(object::Base& base, uint32_t id)
{
    base.m_id = id;
}
inline void SetState(object::Base& base, uint32_t state)
{
    base.m_state = static_cast<int>(state);
}

inline void SetWorldXYZ(object::Base& base, uint32_t x, uint32_t y, uint32_t z)
{
    std::memcpy(&base.m_WorldXYZ[0], &x, sizeof(x));
    std::memcpy(&base.m_WorldXYZ[1], &y, sizeof(y));
    std::memcpy(&base.m_WorldXYZ[2], &z, sizeof(z));
}

inline void ClearSelection(object::Base& base)
{
    base.m_SelectLevel = 0;
}

inline float* GetPosition(const object::Base& base, float* out)
{
    std::memcpy(out, base.m_WorldXYZ, sizeof(base.m_WorldXYZ));
    return out;
}

// The tile is computed into a local first and copied, so an `out` that aliases the position is still read before it is written.
template<class Env> int32_t* GetTilePosition(const object::Base& base, int32_t* out, Env& env)
{
    int32_t tile[2];
    env.WorldToTile(base.m_WorldXYZ, tile);
    out[0] = tile[0];
    out[1] = tile[1];
    return out;
}

// States 0 and 1 are the basic ones (unsigned compare: `state <= 1`).
inline bool HasBasicState(uint32_t state)
{
    return state <= 1;
}

// The base class plays nothing and reports a zero duration.
inline float PlayAnimation()
{
    return 0.0f;
}

template<class Env> bool SendMessage(const WireMessage& message, Env& env)
{
    return env.Post(message);
}

template<class Env> bool SendMessageDelayed(const WireMessage& message, uint32_t delayBits, Env& env)
{
    return env.PostDelayed(message, delayBits);
}

// A script variable the class does not know is a fatal error naming the key and the object's type byte.
template<class Env> void GetVariableText(const object::Base& base, const char* key, Env& env)
{
    env.InvalidKeyword(key, static_cast<uint32_t>(static_cast<uint8_t>(base.m_type)));
}

template<class Env> void Draw(const object::Base& base, Env& env)
{
    env.DrawInWorld(base.m_sprite, base.m_WorldXYZ, env.GameTimeBits());
}

} // namespace obl::entity::accessors
