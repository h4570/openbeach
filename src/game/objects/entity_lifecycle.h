#pragma once

#include "core/float_to_int.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "script_text.h"
#include "script_types.h"

#include <cstdint>
#include <cstring>

// The life cycle, the script variables and the save format of the class every object derives from (docs/architecture/objects.md):
// Construct initializes the named base object (Base's constructor runs it); Destruct releases its resources (Base's destructor).
// GetValue and SetValue expose the shared script fields.
// Pure logic: whatever leaves the object goes through an `Env` that the game side maps onto the game's services
// (src/game/objects/entity_lifecycle.cpp) and the unit test onto a fake.
//
// Env:
//   Construct / Destruct:
//     void NameTooLong(const char* name)                              FatalError with the "name exceeds" text (does not return)
//     void ReleaseSprite(void* sprite)                                releasing the sprite through its bank
//     void* CellAt(const uint32_t xyz[3])                             the grid cell containing the position, preserving its raw float bits
// void RemoveFromCell(void* cell, Entity&) removes every occurrence from the cell object list
//   GetValue:  int Rand   uint32_t ReadWord(intptr_t address)   uint32_t WorldStartup   uint32_t GameTimeBits   uint32_t CurrentWeather
//   SetValue:  void SetPosition(Entity&, const uint32_t xyz[3])   SetPosition (the float bits go through unchanged)
//              int GetDrawOffsetX(ui::SpriteInstance*)            the sprite drawing offset
namespace obl::entity::lifecycle {

using script::ScriptValue;

using object::kNameSize;
constexpr int kSpriteNameSize = 0x80;

inline uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float FromBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(bits));
    return value;
}

// ---- Construct / Destruct --------------------------------------------------------------------------------------------------------------

// The name is copied with its
// terminator into the zeroed name field; a name of 32 characters or more is a FatalError (the copy is also clamped, so a
// handler that returned could never write past the field).
template<class Env> object::Base* Construct(object::Base& base, const char* name, Env& env)
{
    std::memset(base.m_Name, 0, sizeof(base.m_Name));
    base.m_id = 0xffffffffu;
    base.m_type = static_cast<object::ObjectType>(0);
    base.m_owner = 0;
    base.m_state = 0;
    for (float& word : base.m_WorldXYZ) word = FromBits(0);
    base.m_waitingForDeath = 0;
    base.m_requiresAttention = 0;
    base.m_SelectLevel = 0;
    base.m_sprite = nullptr;
    base.m_visible = 1;
    base.m_XDrawOffset = 0;
    base.m_YDrawOffset = 0;
    base.m_debugBreak = 0;
    const size_t length = std::strlen(name);
    if (length >= static_cast<size_t>(kNameSize)) env.NameTooLong(name);
    std::memcpy(base.m_Name, name, length >= static_cast<size_t>(kNameSize) ? static_cast<size_t>(kNameSize) : length + 1);
    return &base;
}

// The sprite instance is given back (the field keeps the dead pointer), and the object leaves the object
// list of the grid cell its position lies in. The position goes through as the raw bits of the three floats.
template<class Env> void Destruct(object::Base& base, Env& env)
{
    if (base.m_sprite) env.ReleaseSprite(base.m_sprite);
    const uint32_t xyz[3] = {Bits(base.m_WorldXYZ[0]), Bits(base.m_WorldXYZ[1]), Bits(base.m_WorldXYZ[2])};
    env.RemoveFromCell(env.CellAt(xyz), base);
}

// ---- GetValue / SetValue ---------------------------------------------------------------------------------------------------------------

// Numbers of the script variables (docs/architecture/objects.md): the ones the base class answers itself. Everything else - the keys of the
// derived classes, anything below 0x36 or above 0xf4 (compared unsigned, so negatives too) - comes back as the call's own `{key, data}`.
constexpr int32_t kKeyTextLiteral = 0x36, kKeyWaitingForDeath = 0x4a, kKeyCurWeather = 0x58, kKeyLightRain = 0x59,
                  kKeyNeedsAttention = 0xb8, kKeyWorldX = 0xc3, kKeyWorldY = 0xc4, kKeyWorldStarted = 0xe1, kKeyMe = 0xe2,
                  kKeyRandDword = 0xe3, kKeyRandom = 0xe4, kKeyNoObject = 0xe5, kKeyTrue = 0xe6, kKeyFalse = 0xe7, kKeyNumber = 0xe8,
                  kKeyState = 0xe9, kKeyGameTime = 0xea, kKeyDrawOffsetX = 0xf3, kKeyDrawOffsetY = 0xf4;
constexpr int32_t kTypeByte = 0, kTypeText = 1, kTypeInt = 2, kTypeFloat = 3; // ScriptValue::type (script_program_exec.h)
constexpr int32_t kWeatherLightRain = 3;

// A byte answer: the low byte of the call's `data` slot is replaced, the other three stay (callers may read the whole word).
inline ScriptValue ByteValue(intptr_t data, uint32_t byte)
{
    return ScriptValue{kTypeByte, static_cast<int32_t>((static_cast<uint32_t>(data) & 0xffffff00u) | (byte & 0xff))};
}

// `RandDWORD<n>` and `Random<n>` keep `n` in a heap int and hand its address over as `data`; the answer is `rand % n` (unsigned) or the flag
// "that is 0". A zero divisor leaves the remainder 0.
template<class Env> uint32_t RandomRemainder(intptr_t data, Env& env)
{
    const int draw = env.Rand();
    const uint32_t divisor = env.ReadWord(data);
    return divisor ? static_cast<uint32_t>(draw) % divisor : 0;
}

template<class Env> ScriptValue GetValue(const object::Base& base, int32_t key, intptr_t data, Env& env)
{
    switch (key) {
    case kKeyTextLiteral: // the call's data word is the literal's address, the value carries its id
        return ScriptValue{kTypeText, script::text::Id(reinterpret_cast<const char*>(data))};
    case kKeyCurWeather: return ScriptValue{kTypeInt, static_cast<int32_t>(env.CurrentWeather())};
    case kKeyLightRain: return ScriptValue{kTypeInt, kWeatherLightRain};
    case kKeyNeedsAttention: return ByteValue(data, base.m_requiresAttention);
    case kKeyWorldStarted: return ByteValue(data, env.WorldStartup() == 0);
    case kKeyMe: return ScriptValue{kTypeInt, static_cast<int32_t>(base.m_id)};
    case kKeyRandDword: return ScriptValue{kTypeInt, static_cast<int32_t>(RandomRemainder(data, env))};
    case kKeyRandom: return ByteValue(data, RandomRemainder(data, env) == 0);
    case kKeyNoObject: return ScriptValue{kTypeInt, -1};
    case kKeyTrue: return ByteValue(data, 1);
    case kKeyFalse: return ByteValue(data, 0);
    case kKeyNumber: return ScriptValue{kTypeFloat, static_cast<int32_t>(env.ReadWord(data))};
    case kKeyState: return ScriptValue{kTypeInt, base.m_state};
    case kKeyGameTime: return ScriptValue{kTypeFloat, static_cast<int32_t>(env.GameTimeBits())};
    case kKeyDrawOffsetX: return ScriptValue{kTypeInt, base.m_XDrawOffset};
    case kKeyDrawOffsetY: return ScriptValue{kTypeInt, base.m_YDrawOffset};
    default: return ScriptValue{key, static_cast<int32_t>(data)};
    }
}

// The animation of the sprite instance whose frame offsets the draw offsets move; null when the object has no sprite (tolerated by the callers).
inline ui::SpriteAnim* CurrentAnim(const object::Base& base)
{
    const auto* sprite = static_cast<const ui::SpriteInstance*>(base.m_sprite);
    if (!sprite || !sprite->m_sprite || sprite->m_animIndex < 0 || sprite->m_animIndex >= sprite->m_sprite->m_animCount) return nullptr;
    return sprite->m_sprite->m_anims[sprite->m_animIndex];
}

inline int16_t Wrap16(int value)
{
    return static_cast<int16_t>(static_cast<uint16_t>(value));
}

// `DrawOffsetX`: the old offset is first taken back out of the animation's hotspot (the draw offset the sprite reports plus the old offset), then the
// new one (the script's float truncated toward zero) goes in as `reported - offset`. The hotspot is the 16-bit x word of the animation record;
// the second GetDrawOffsetX call sees the hotspot the first step left.
template<class Env> void SetDrawOffsetX(object::Base& base, int32_t bits, Env& env)
{
    ui::SpriteAnim* anim = CurrentAnim(base);
    auto* sprite = static_cast<ui::SpriteInstance*>(base.m_sprite);
    const int offset = FloatToIntWrap(static_cast<double>(FromBits(static_cast<uint32_t>(bits))));
    if (base.m_XDrawOffset != 0 && anim) anim->m_hotspotX = Wrap16(env.GetDrawOffsetX(sprite) + base.m_XDrawOffset);
    base.m_XDrawOffset = offset;
    if (anim) anim->m_hotspotX = Wrap16(env.GetDrawOffsetX(sprite) - offset);
}

// `DrawOffsetY` is the same on the hotspot's y word, read and written directly: 16-bit arithmetic on the word.
template<class Env> void SetDrawOffsetY(object::Base& base, int32_t bits, Env&)
{
    ui::SpriteAnim* anim = CurrentAnim(base);
    const int offset = FloatToIntWrap(static_cast<double>(FromBits(static_cast<uint32_t>(bits))));
    if (base.m_YDrawOffset != 0 && anim) anim->m_hotspotY = Wrap16(anim->m_hotspotY + static_cast<int16_t>(base.m_YDrawOffset));
    base.m_YDrawOffset = offset;
    if (anim) anim->m_hotspotY = Wrap16(anim->m_hotspotY - static_cast<int16_t>(offset));
}

// The type of the value is ignored by all of them: its bits are taken as they are (an unsigned int for the world position, a byte for the
// flags, a float for the draw offsets). `WorldX/WorldY` move the object through its own virtual `setPosition` with the other two coordinates as they
// are; the int is converted as an unsigned 64-bit value, so the full 32 bits count.
template<class Env> void SetValue(object::Base& base, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    (void)data;
    (void)type;
    switch (key) {
    case kKeyWorldX:
    case kKeyWorldY: {
        uint32_t xyz[3] = {Bits(base.m_WorldXYZ[0]), Bits(base.m_WorldXYZ[1]), Bits(base.m_WorldXYZ[2])};
        xyz[key == kKeyWorldX ? 0 : 1] = Bits(static_cast<float>(static_cast<uint32_t>(bits)));
        env.SetPosition(base, xyz);
        break;
    }
    case kKeyWaitingForDeath: base.m_waitingForDeath = static_cast<uint8_t>(bits); break;
    case kKeyNeedsAttention: base.m_requiresAttention = static_cast<uint8_t>(bits); break;
    case kKeyDrawOffsetX: SetDrawOffsetX(base, bits, env); break;
    case kKeyDrawOffsetY: SetDrawOffsetY(base, bits, env); break;
    default: break;
    }
}

} // namespace obl::entity::lifecycle
