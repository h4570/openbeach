#include "core/char16.h"
#include "core/call_counter.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_building.h"
#include "game/objects/object_table.h"
#include "game/objects/object_performers.h"

#include "game/game/game_accessors.h"
#include "game/objects/entity_accessors.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"
#include "game/messages/message_handler.h"
#include "game/ui/map_view_math.h"
#include "game/game/diagnostics.h"

#include <bit>
#include <cstdint>
#include <cstring>

// The small methods every object shares (entity_accessors.h has the logic and the conventions).
namespace obl::object {
namespace {
namespace acc = entity::accessors;
using acc::WireMessage;

uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct TileEnv {
    void WorldToTile(const float world[3], int32_t tile[2]) { ui::MapView::WorldToTile(tile, world); }
};

class SendEnv {
public:
    // The handler stamps the delivery time into the record, so it gets a copy of the caller's const message.
    static_assert(sizeof(messages::Message) == sizeof(WireMessage));
    bool Post(const WireMessage& message)
    {
        messages::Message copy{};
        std::memcpy(&copy, &message, sizeof copy); // the same eight words
        return messages::message_facade::Post(Handler(), &copy);
    }
    bool PostDelayed(const WireMessage& message, uint32_t delayBits)
    {
        messages::Message copy{};
        std::memcpy(&copy, &message, sizeof copy); // the same eight words
        return messages::message_facade::PostDelayed(Handler(), &copy, std::bit_cast<float>(delayBits));
    }

private:
    static messages::Handler* Handler() { return messages::message_facade::Current(); }
};

void SendMessage(const messages::Message& message)
{
    SendEnv env;
    acc::SendMessage(std::bit_cast<WireMessage>(message), env);
}

void SendMessageDelayed(const messages::Message& message, float delay)
{
    SendEnv env;
    acc::SendMessageDelayed(std::bit_cast<WireMessage>(message), std::bit_cast<uint32_t>(delay), env);
}

struct VariableTextEnv {
    void InvalidKeyword(const char* key, uint32_t type)
    {
        game::FatalError("Script variable \"%s\" is not defined for objects of type %d", key, type);
    }
};

struct DrawEnv {
    uint32_t GameTimeBits() { return Bits(game::GetWorld()->m_gameTime); }
    void DrawInWorld(void* sprite, const float world[3], uint32_t timeBits)
    {
        static_cast<ui::SpriteInstance*>(sprite)->DrawInWorld(world, std::bit_cast<float>(timeBits));
    }
};

// What a gameplay session reaches (tests/entity_accessors.txt).
test::CallCounter g_accessors("objects.base-accessors"), g_draw("objects.base-draw");
} // namespace

void Base::SetType(uint8_t type)
{
    acc::SetType(*this, type);
}

void Base::SetId(uint32_t id)
{
    acc::SetId(*this, id);
}

void Base::SetState(uint32_t state)
{
    g_accessors.Count();
    acc::SetState(*this, state);
}

void Base::SetWorldPosition(float x, float y, float z)
{
    acc::SetWorldXYZ(*this, Bits(x), Bits(y), Bits(z));
}

float* Base::GetPosition(float* out) const
{
    g_accessors.Count();
    return acc::GetPosition(*this, out);
}

int32_t* Base::GetTilePosition(int32_t* out) const
{
    g_accessors.Count();
    TileEnv env;
    return acc::GetTilePosition(*this, out, env);
}

void Base::GetVariableText(const char* key, Char16* out)
{
    VariableTextEnv env;
    acc::GetVariableText(*this, key, env);
}
void Base::Draw()
{
    g_draw.Count();
    DrawEnv env;
    acc::Draw(*this, env);
}
float Base::PlayAnimation(const char*, uint32_t, int, uint32_t, const char*)
{
    return acc::PlayAnimation();
}
void Base::Send(messages::Message message)
{
    g_accessors.Count();
    SendMessage(message);
}
void Base::SendDelayed(messages::Message message, float delay)
{
    g_accessors.Count();
    SendMessageDelayed(message, delay);
}
void ServiceStaff::Send(messages::Message message)
{
    SendMessage(message);
}
void ServiceStaff::SendDelayed(messages::Message message, float delay)
{
    SendMessageDelayed(message, delay);
}
void Staff::ClearSelection()
{
    acc::ClearSelection(*this);
}
void Building::ReduceSelectionToHover()
{
    acc::ClearSelection(*this);
}
bool SnootyStaff::HasState(uint32_t state)
{
    return acc::HasBasicState(state);
}
bool Furniture::HasState(uint32_t state)
{
    return acc::HasBasicState(state);
}
bool Cabaret::HasState(uint32_t state)
{
    return acc::HasBasicState(state);
}
} // namespace obl::object
