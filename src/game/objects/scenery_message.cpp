#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/ui/sprite.h"
#include "game/objects/object_base.h"
#include "game/objects/object_static.h"
#include "game/objects/object_registry_api.h"
#include "game/feedback/feedback_manager.h"
#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/messages/message_state.h"
#include "game/objects/object_scenery.h"
#include "game/objects/scenery_record.h"
#include "game/objects/scenery_logic.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include <cstring>
#include <string>
#include "game/ui/text_table.h"
#include "game/objects/script_text.h"

namespace obl::object {
using feedback::Feedback;
using game::GetObjectManager;
using game::GetWorld;

namespace {
using namespace world_data;
using messages::Message;
using ui::SpriteInstance;
using enum messages::MessageType;
using object::Scenery;
test::CallCounter g_counter("objects.scenery-receive-message"), g_name("objects.scenery-get-name");
void Post(Scenery& s, uint32_t to, uint32_t type, uint32_t data = 0, bool delayed = false)
{
    Message m{};
    m.sender = s.m_id;
    m.receiver = to;
    m.type = type;
    m.data = data;
    auto* handler = messages::message_facade::Current();
    if (delayed)
        messages::message_facade::PostDelayed(handler, &m, 2.0f);
    else
        messages::message_facade::Post(handler, &m);
}
bool Receive(Scenery& s, Message& m)
{
    auto* sprite = static_cast<SpriteInstance*>(s.m_sprite);
    switch (m.type) {
    case MT_STATECHANGE:
        if ((ascii::EqualsNoCase(s.m_Name, "BENCH") || ascii::EqualsNoCase(s.m_Name, "LOUNGER")) &&
                scenery::IsSeatSender(m.sender, s.m_id) &&
                object_manager::ObjectById(GetObjectManager(), m.sender)->m_type == object::ObjectType::Guest) {
            if (m.data == 4) {
                const uint32_t seat = scenery::FindSeat(s.m_AttachedGuest, s.m_MaxNumAttachedGuests, -1);
                s.m_FreeSeat = seat;
                s.m_AttachedGuest[seat] = m.sender;
                s.m_AttachedGuestState[seat] = object_manager::ObjectById(GetObjectManager(), m.sender)->m_state;
                if (++s.m_NumAttachedGuests != 1) return true;
            } else {
                const uint32_t seat = scenery::FindSeat(s.m_AttachedGuest, s.m_MaxNumAttachedGuests, static_cast<int32_t>(m.sender));
                s.m_AttachedGuest[seat] = -1;
                s.m_AttachedGuestState[seat] = -1;
                if (--s.m_NumAttachedGuests != 0) return true;
            }
        }
        break;
    case MT_REPAIR:
        s.m_RepairState = scenery::NextRepairState(s.m_RepairState);
        Post(s, m.sender, scenery::RepairReplyType(s.m_RepairState));
        if (!s.m_RepairState) {
            s.SetMechanicClaimed(false);
            s.SetCalledRepairing(false);
            s.SetUrgentRepairCalled(false);
        }
        if (s.m_vandalised) {
            s.m_vandalised = 0;
            if (sprite->m_animIndex != static_cast<int>(s.m_PreVandlismAnim)) {
                sprite->m_animIndex = s.m_PreVandlismAnim;
                sprite->m_frameIndex = 0;
            }
            const std::string key = std::string("SP_DESC_") + s.m_definition->m_key;
            const Char16* name = ui::text_table::Lookup(key.c_str());
            const Char16* format = ui::text_table::Lookup("FEEDBACK_BUILDING_REPAIRED");
            Char16 text[256];
            ::obl::ui::host_text::Print(text, format, name);
            feedback::Feedback::OfWorld().AddText(text, s.m_id, 4);
        }
        break;
    case 60:
        sprite->m_animType = 2;
        (void)(sprite)->SetAnimByName(script::text::Of(m.data), GetWorld()->m_gameTime, nullptr);
        break;
    case 84:
        if (ascii::EqualsNoCase(s.m_definition->m_key, "LitterVomit") || s.m_vandalised) {
            uint32_t xy[2];
            s.GetTilePosition(reinterpret_cast<int32_t*>(xy));
            for (object::Base* other : object_manager::GetObjectsNearTile(GetObjectManager(), xy[0], xy[1], 1))
                if (other->m_type == object::ObjectType::Guest) Post(s, other->m_id, 73, scenery::NuisanceLevel(s.m_vandalised));
            Post(s, s.m_id, 84, 0, true);
        }
        break;
    }
    return s.Prop::ReceiveMessage(&m);
}
bool Run(Scenery* s, Message* m)
{
    g_counter.Count();
    return Receive(*s, *m);
}
const char* Name(Scenery* s)
{
    g_name.Count();
    return s->m_ObjectName;
}
} // namespace

bool Scenery::ReceiveMessage(messages::Message* message)
{
    return Run(this, message);
}
const char* Scenery::GetName()
{
    return Name(this);
}
} // namespace obl::object
