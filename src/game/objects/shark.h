#pragma once

#include "core/char16.h"
#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_shark.h"
#include "game/ui/sprite.h"
#include "script_types.h"
#include "script_text.h"

#include <cstdint>
#include <cstring>

// The Shark class (docs/architecture/objects.md). The shark is a
// Mover driven by the object script "SharkLogic"; the class adds the script calls around the base class, the swimming of the script's two
// functions (CallFunction 4 and 0xb3) and the save fields.
//
// Pure logic over an environment, so it runs on the game and in the unit tests. Env (everything outside the shark):
//   Construct:      script::ScriptProgram* ScriptLookup(const char*)  the world's "SharkLogic" script
//   Script:         void RunOnEntry(int32_t state) / RunOnExit(int32_t state) / RunOnUpdate(int32_t state)   the shark script's handlers
//                   bool RunOnReceipt(const messages::Message&)        the script's receipt handler, the message by value
//                   void SendThroughScript(const Message&)           the script's SendMessage (the caller checks for a script)
//                   void SendThroughScriptDelayed(const Message&, float delay)   the script's SendMessageDelayed
//   Base class:     bool MovingUpdate                              moving object Update       bool Movement(const Message&)   moving object ReceiveMessage
//   Fatal:          void FatalUpdate(const char* name)               FatalError("Movement update of shark \"%s\" failed")
//                   void FatalInvalidFunction                      FatalError("A script called a function sharks do not have")
//   Animation:      float StartAnimation(void* sprite, const char* name, float startTime, const float* world)   start an animation by name
//                   float WorldTime                                current world time
//   Functions:      int Rand  void Post(const Message&)           the game's RNG and posting a message
//                   uint32_t ObjectsNear(const Shark&, int32_t radius, object::Base* const*& list)   objects near the shark's tile
namespace obl::shark {

// Script values use an out pointer in the game ABI; the logic returns the two words.
template<class Env> script::ScriptValue GetValue(const object::Shark& shark, int32_t key, intptr_t data, Env& env)
{
    int32_t value;
    switch (key) {
    case 0: value = shark.m_curMessage->sender; break;
    case 5: value = shark.m_curMessage->data; break;
    case 0x38: value = static_cast<const ui::SpriteInstance*>(shark.m_sprite)->m_direction; break;
    case 0x40: value = static_cast<int32_t>(shark.m_actionState); break;
    case 0xf5: value = shark.m_myVictim; break;
    case 0xf6:
    case 0xf7: value = FloatToIntWrap(static_cast<double>(env.Object(shark.m_myVictim)->m_WorldXYZ[key - 0xf6])); break;
    case 0xf8: value = static_cast<const ui::SpriteInstance*>(env.Object(shark.m_myVictim)->m_sprite)->m_direction; break;
    case 0x10f: value = shark.m_ownerId; break;
    default: return env.BaseGet(shark, key, data);
    }
    return {2, value};
}

template<class Env> void SetValue(object::Shark& shark, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    if (key == 0x40)
        shark.m_actionState = env.ToInt(type, bits);
    else if (key == 0x38) {
        auto& sprite = *static_cast<ui::SpriteInstance*>(shark.m_sprite);
        const int32_t directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
        const int32_t value = env.ToInt(type, bits);
        // Negative directions stay negative; only the upper bound is clamped.
        sprite.m_direction = value >= directions ? directions - 1 : value;
    } else
        env.BaseSet(shark, key, data, type, bits);
}

template<class Env> void GetVariableText(const object::Shark& shark, const char* key, Char16* out, Env& env)
{
    if (std::strcmp(key, "SHARK_GUESTS_EATEN") == 0) env.PrintInt(out, u"%d", shark.m_guestsEaten);
}

template<class Env> void Draw(object::Shark& shark, Env& env)
{
    env.BaseDraw(shark);
}

constexpr int32_t kFunctionDecideSwimAction = 4;
constexpr int32_t kFunctionAlertGuests = 0xb3;
constexpr int32_t kGuestType = 3; // Entity::m_type of a guest
constexpr int32_t kGuestSwimming = 0x16; // its state while it swims: the only victims and the only guests warned
constexpr int32_t kGuestAttacked = 0x36; // state a guest is told to enter by the bite (ATTACK_BY_SHARK)
constexpr int32_t kSharkAttack = 0x87; // state the shark enters with the bite
constexpr int32_t kMsgStateChange = 3; // MT_STATECHANGE: data = the state
constexpr int32_t kMsgSwim2SeaPoint = 11; // MT_SWIM2SEAPOINT: flags = x, data = y
constexpr int32_t kMsgLookAround = 0x54; // MT_LOOK_AROUND
constexpr const char* kScriptName = "SharkLogic";
constexpr const char* kMessageData = "MessData"; // the animation name that means "the text in the data word of the message being received"

namespace detail {

// The messages fill five words; the rest is zero.
inline messages::Message Make(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{};
    message.sender = sender;
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}

inline int32_t Id(const object::Base& object)
{
    return static_cast<int32_t>(object.m_id);
}

inline bool IsSwimmingGuest(const object::Base& object)
{
    return static_cast<int32_t>(object.m_type) == kGuestType && object.m_state == kGuestSwimming;
}

// The int arithmetic wraps; it runs unsigned so the wrap is defined.
inline int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// The copy of the message the object keeps turns a signalling NaN into a quiet one (bit 22).
inline float Quieted(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits |= 0x00400000u;
    std::memcpy(&value, &bits, sizeof(bits));
    return value;
}

} // namespace detail

// States the shark has: 0 and 1 (the base states) and 0x85..0x88 (the script's).
inline bool HasState(uint32_t state)
{
    return state <= 1 || (state >= 0x85 && state <= 0x88);
}

// SetBounds and SetInitParam: plain stores, left, top, right, bottom of the swim rectangle and the owner.
inline void SetBounds(object::Shark& shark, int32_t a, int32_t b, int32_t c, int32_t d)
{
    shark.m_SwimCircleRect[0] = a;
    shark.m_SwimCircleRect[1] = b;
    shark.m_SwimCircleRect[2] = c;
    shark.m_SwimCircleRect[3] = d;
}
inline void SetInitParam(object::Shark& shark, int32_t value)
{
    shark.m_ownerId = value;
}

// Construct (Shark's constructor, after Mover's). Not written (they keep the zero of their member initializers): m_curMessage, the swim rectangle, m_actionState and
// m_ownerId. The speed limits are the base class's own fields (m_maxForce, m_maxVel).
template<class Env> object::Shark* Construct(object::Shark& shark, Env& env)
{
    shark.m_myVictim = -1;
    shark.m_alertRange = 5;
    shark.m_sniffRange = 10;
    shark.m_script = env.ScriptLookup(kScriptName);
    shark.m_guestsEaten = 0;
    shark.m_maxVel = 100.0f;
    shark.m_maxForce = 50.0f;
    return &shark;
}

// OnEnterState / OnExitState: the script's procedure of the state (the exit one for the shark's current state), always true.
template<class Env> bool OnEnterState(object::Shark&, uint32_t state, Env& env)
{
    env.RunOnEntry(static_cast<int32_t>(state));
    return true;
}
template<class Env> bool OnExitState(object::Shark& shark, Env& env)
{
    env.RunOnExit(shark.m_state);
    return true;
}

// Update: the base class first (false ends the game with the error naming the shark), then the script's update of the current state; always true.
template<class Env> bool Update(object::Shark& shark, Env& env)
{
    if (!env.MovingUpdate()) env.FatalUpdate(shark.m_Name);
    env.RunOnUpdate(shark.m_state);
    return true;
}

// ReceiveMessage. While the script and the base class run, m_curMessage is a copy of the message (the words 0..4
// and 6 are copied, the others are zero) and is put back afterwards. The script gets the message by value and answers whether it has a
// procedure for the type; if not, the base class gets the pointer the caller passed. The result is whichever answered.
template<class Env> bool ReceiveMessage(object::Shark& shark, const messages::Message& message, Env& env)
{
    messages::Message* const saved = shark.m_curMessage;
    messages::Message copy{};
    copy.sender = message.sender;
    copy.receiver = message.receiver;
    copy.type = message.type;
    copy.flags = message.flags;
    copy.data = message.data;
    copy.floatData = detail::Quieted(message.floatData);
    shark.m_curMessage = &copy;
    const bool handled = env.RunOnReceipt(message) ? true : env.Movement(message);
    shark.m_curMessage = saved;
    return handled;
}

// SendMessage and SendMessageDelayed (the delay last): the message by value to the script; nothing without one.
template<class Env> void SendMessage(object::Shark& shark, const messages::Message& message, Env& env)
{
    if (shark.m_script) env.SendThroughScript(message);
}
template<class Env> void SendMessageDelayed(object::Shark& shark, const messages::Message& message, float delay, Env& env)
{
    if (shark.m_script) env.SendThroughScriptDelayed(message, delay);
}

// PlayAnimation (name, a flag, an unused message, a flag, an unused word). The name "MessData" stands for the text the message being received
// carries in its data word. The sprite's animation type is 1 for the first flag, else 0 for the second, else 2. The answer is the length of the animation
// the sprite reports, 1 when it reports zero (or NaN: an unordered compare counts as equal).
template<class Env> float PlayAnimation(object::Shark& shark, const char* animation, bool first, bool second, Env& env)
{
    if (std::strcmp(animation, kMessageData) == 0) animation = script::text::Of(shark.m_curMessage->data);
    auto* sprite = static_cast<ui::SpriteInstance*>(shark.m_sprite);
    sprite->m_animType = first ? 1 : second ? 0 : 2;
    const float length = env.StartAnimation(sprite, animation, env.WorldTime(), shark.m_WorldXYZ);
    return length == 0.0f || length != length ? 1.0f : length;
}

// AlertGuests: every swimming guest within m_alertRange tiles is sent MT_LOOK_AROUND. The list is the object manager's shared buffer
// and delivering a message may change it, so it is walked by index and read again for every element.
template<class Env> void AlertGuests(object::Shark& shark, Env& env)
{
    object::Base* const* list = nullptr;
    const uint32_t count = env.ObjectsNear(shark, shark.m_alertRange, list);
    for (uint32_t i = 0; i < count; ++i) {
        const object::Base* guest = list[i];
        if (!detail::IsSwimmingGuest(*guest)) continue;
        env.Post(detail::Make(detail::Id(shark), detail::Id(*guest), kMsgLookAround, 0, 0));
    }
}

// Choose a swimming action. Without a victim it looks at one random object near it (m_sniffRange): a swimming guest becomes the victim and gets the
// bite (MT_STATECHANGE to the guest and to the shark itself, nothing more). Otherwise (a victim already, or the object drawn is not a swimming guest) the
// victim is cleared and the shark swims on: the next point in the quarter of the swim rectangle m_actionState selects (0 left-top, 1 right-top, 2 right-bottom,
// 3 left-bottom), by MT_SWIM2SEAPOINT. The x is drawn first and goes through a float before the conversion; the y is exact. A rectangle whose half is zero
// would divide by zero: the shark stops there without a message and without moving the state on.
template<class Env> void DecideSwimAction(object::Shark& shark, Env& env)
{
    using detail::Add;
    if (shark.m_myVictim == -1) {
        object::Base* const* list = nullptr;
        const uint32_t count = env.ObjectsNear(shark, shark.m_sniffRange, list);
        if (count != 0) {
            const uint32_t pick = static_cast<uint32_t>(env.Rand()) % count;
            const object::Base* object = list[pick];
            if (detail::IsSwimmingGuest(*object)) {
                shark.m_myVictim = detail::Id(*object);
                env.Post(detail::Make(detail::Id(shark), detail::Id(*list[pick]), kMsgStateChange, 0, kGuestAttacked));
                env.Post(detail::Make(detail::Id(shark), detail::Id(shark), kMsgStateChange, 0, kSharkAttack));
                return;
            }
        }
    } else {
        shark.m_myVictim = -1;
    }
    const int32_t left = shark.m_SwimCircleRect[0], top = shark.m_SwimCircleRect[1];
    const int32_t width = static_cast<int32_t>(static_cast<uint32_t>(shark.m_SwimCircleRect[2]) - static_cast<uint32_t>(left));
    const int32_t height = static_cast<int32_t>(static_cast<uint32_t>(shark.m_SwimCircleRect[3]) - static_cast<uint32_t>(top));
    const uint32_t quarter = shark.m_actionState;
    const int32_t halfWidth = width / 2, halfHeight = height / 2;
    if (halfWidth == 0) return;
    int32_t x = env.Rand() % halfWidth;
    x = Add(x, left);
    if (quarter == 1 || quarter == 2) x = Add(x, halfWidth);
    const float xFloat = static_cast<float>(x);
    if (halfHeight == 0) return;
    int32_t y = env.Rand() % halfHeight;
    y = Add(y, top);
    if (quarter != 0 && quarter != 1) y = Add(y, halfHeight);
    shark.m_actionState = (quarter + 1) & 3;
    env.Post(detail::Make(detail::Id(shark), detail::Id(shark), kMsgSwim2SeaPoint, FloatToIntWrap(static_cast<double>(xFloat)), y));
}

// CallFunction: the two functions of the script, any other id is a fatal error.
template<class Env> void CallFunction(object::Shark& shark, int32_t id, Env& env)
{
    if (id == kFunctionDecideSwimAction)
        DecideSwimAction(shark, env);
    else if (id == kFunctionAlertGuests)
        AlertGuests(shark, env);
    else
        env.FatalInvalidFunction();
}

} // namespace obl::shark
