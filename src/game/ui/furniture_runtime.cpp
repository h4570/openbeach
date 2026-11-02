#include "core/char16.h"
#include "game/objects/guest.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite_bank.h"
#include "game/world/world_receive.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/ui/furniture_runtime.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/objects/object_table.h"
#include "core/log.h"
#include "game/objects/furniture.h"
#include "game/objects/object_base.h"
#include "game/objects/furniture_draw.h"
#include "game/objects/furniture_receive.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "game/world/world_params.h"
#include "game/objects/object_create_params.h"
#include "game/game/diagnostics.h"
#include "game/objects/script_text.h"
#include "game/objects/object_static.h"

// Furniture: every method.
namespace obl::ui {
namespace {
using messages::Message;
using MessageHandler = messages::Handler;
using object::Furniture;
using object::StaffCreateParams;
using object::TableChair;
using tables::TableDef;
using ui::SpriteInstance;

// Calls counted for `call-report <name>`.
test::CallCounter g_constructCounter("objects.table-construct"), g_destroyCounter("objects.table-destroy"),
        g_seatCounter("objects.table-seat-guest"), g_attachCounter("objects.table-attach-to-building"),
        g_enterCounter("objects.table-on-enter-state"), g_updateCounter("objects.table-update"),
        g_setVariableCounter("objects.table-set-variable"), g_getTextCounter("objects.table-get-variable-text"),
        g_getValueCounter("objects.table-get-value"), g_setValueCounter("objects.table-set-value"),
        g_orientationCounter("objects.table-set-orientation"), g_receiveCounter("objects.table-receive-message"),
        g_drawCounter("objects.table-draw"), g_drawDrive("objects.table-draw.calls");
// Which branch of ReceiveMessage ran (the two types left to the base class are not counted).
test::CallCounter g_branchReady("objects.table-receive-message.im-ready"), g_branchIgnored("objects.table-receive-message.ignored"),
        g_branchServed("objects.table-receive-message.guest-served"), g_branchBase("objects.table-receive-message.base"),
        g_branchMove("objects.table-receive-message.move-im-here"), g_branchNext("objects.table-receive-message.next-move-point"),
        g_branchAction("objects.table-receive-message.do-next-action");

void* SpriteBank()
{
    return ui::GetSpriteBank();
}
MessageHandler* Handler()
{
    return messages::message_facade::Current();
}
object::Base* ObjectById(int32_t id)
{
    return object::object_manager::ObjectById(game::GetObjectManager(), id);
}

// The game side of the Env of src/game/objects/furniture.h.
class GameEnv {
public:
    const TableDef* Definition(const char* key)
    {
        tables::WorldParams* params = game::GetWorldParams();
        const int id = world::world_params::GetTableId(params, key);
        return world::world_params::GetTableDefinition(params, id);
    }
    void* Allocate(uint32_t bytes) { return ::operator new(bytes); }
    void ResetWorldPosition(Furniture& object) { object.SetWorldPosition(0.0f, 0.0f, 0.0f); }
    SpriteInstance* CreateSprite(const char* name) { return static_cast<ui::SpriteBank*>(SpriteBank())->CreateObservedInstance(name); }
    void StartAnim(SpriteInstance* sprite, const char* anim, float startTime) { sprite->SetAnimByName(anim, startTime, nullptr); }
    float WorldTime() { return game::GetWorld()->m_gameTime; }

    void ReleaseSprite(SpriteInstance* sprite) { (static_cast<ui::SpriteBank*>(SpriteBank()))->ReleaseInstance(sprite); }
    void Free(void* memory) { ::operator delete(memory); }
    object::Base* Object(int32_t id) { return ObjectById(id); }

    // No check: the building only seats guests that exist.
    void SetGuestActionState(int32_t guest, int32_t value) { static_cast<object::Guest*>(ObjectById(guest))->m_ActionState = value; }
    void ClearDelayed(int32_t receiver) { messages::message_facade::ClearDelayedForReceiver(Handler(), receiver); }
    void Post(const Message& message)
    {
        Message copy = message;
        messages::message_facade::Post(Handler(), &copy);
    }

    void WaiterSpawnPoint(int32_t building, float out[2])
    {
        auto* object = static_cast<object::Building*>(ObjectById(building));
        float scratch[3];
        const float* point = object::building_api::GetWalkPoint(object, scratch, static_cast<int32_t>(object->m_numWalkPoints) - 1);
        std::memcpy(out, point, 2 * sizeof(float));
    }
    void PlaceWaiter(const Message& command, const StaffCreateParams& waiter)
    {
        Message message = command;
        StaffCreateParams payload = waiter;
        world::PostPlaceCommand(message, &payload);
    }

    bool BaseUpdate(Furniture& object) { return object.Prop::Update(); }
    void Fatal(const char* format, const char* argument) { game::FatalError(format, argument); }
    const script::ScriptValue* BaseGetValue(Furniture& object, script::ScriptValue* scratch, int32_t kind, intptr_t data)
    {
        return object.object::Base::GetValue(scratch, kind, data);
    }
    uint32_t BaseSetValue(Furniture& object, int32_t kind, intptr_t data, int32_t sourceKind, int32_t sourceData)
    {
        object.object::Base::SetValue(kind, data, sourceKind, sourceData);
        return 0; // the base returns nothing; no caller reads the result
    }

    bool BaseReceive(Furniture& object, const Message& message) { return object.Prop::ReceiveMessage(const_cast<Message*>(&message)); }
    void SetWaiterPosition(int32_t waiter, const float xyz[3])
    {
        object::Base* object = ObjectById(waiter);
        float point[3];
        std::memcpy(point, xyz, sizeof(point));
        object->SetPosition(point);
    }
    void ShowWaiter(int32_t waiter) { ObjectById(waiter)->m_visible = 1; }
    void HideWaiter(int32_t waiter) { ObjectById(waiter)->m_visible = 0; }
    void SetWaiterDirection(int32_t waiter, int32_t direction)
    {
        table::SetDirection(*static_cast<SpriteInstance*>(ObjectById(waiter)->m_sprite), direction);
    }
    void PostWaiterAnimation(const Furniture& object, const char* name, int32_t flags)
    {
        // Message delivery is synchronous, but keep the text alive like the shared buffer.
        static char animation[256];
        strcpy_s(animation, name);
        Message message{};
        message.sender = static_cast<int32_t>(object.m_id);
        message.receiver = object.m_WaiterID;
        message.type = messages::MT_PLAYANIMATION;
        message.flags = flags;
        message.data = script::text::Id(animation);
        Post(message);
    }
    void StartWaiterAnim(int32_t waiter, const char* anim)
    {
        float position[3];
        std::memcpy(position, ObjectById(waiter)->m_WorldXYZ, sizeof(position));
        const float time = game::GetWorld()->m_gameTime;
        (static_cast<SpriteInstance*>(ObjectById(waiter)->m_sprite))->SetAnimByName(anim, time, position);
        static_cast<SpriteInstance*>(ObjectById(waiter)->m_sprite)->m_animType = 0;
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(Handler(), &copy, delay);
    }

    // MT_NEXT_MOVE_POINT: the guest is any object with a sprite; the position slot and the direction clamp are the waiter's.
    int32_t GuestActionState(int32_t guest) { return static_cast<object::Guest*>(ObjectById(guest))->m_ActionState; }
    void ShowGuest(int32_t guest) { ObjectById(guest)->m_visible = 1; }
    void SetGuestPosition(int32_t guest, const float xyz[3]) { SetWaiterPosition(guest, xyz); }
    void SetGuestDirection(int32_t guest, int32_t direction) { SetWaiterDirection(guest, direction); }
    void BuildingPosition(int32_t building, float out[2])
    {
        const object::Base* object = ObjectById(building);
        out[0] = object->m_WorldXYZ[0];
        out[1] = object->m_WorldXYZ[1];
    }
    void PostGuestAnimation(const Furniture& object, int32_t guest, const char* name)
    {
        static char animation[256];
        strcpy_s(animation, name);
        Message message{};
        message.sender = static_cast<int32_t>(object.m_id);
        message.receiver = guest;
        message.type = messages::MT_PLAYANIMATION;
        message.data = script::text::Id(animation);
        message.extra = table::kActionMarker;
        Post(message);
    }
    void StartChairAnim(const Furniture& object, uint32_t chair, const char* anim, int32_t animType)
    {
        SpriteInstance* sprite = object.m_chairSprites[chair];
        StartAnim(sprite, anim, WorldTime());
        sprite->m_animType = animType;
    }
    int Rand() { return ::obl::game::Rand(); }
};

// ---- The other methods -------------------------------------------------------------------------------------------------------------------

bool EnterStateTable(Furniture* tableObject, uint32_t state)
{
    g_enterCounter.Count();
    GameEnv env;
    return table::OnEnterState(*tableObject, state, env);
}

bool UpdateTable(Furniture* tableObject)
{
    g_updateCounter.Count();
    GameEnv env;
    return table::Update(*tableObject, env);
}

void SetTableVariable(Furniture* tableObject, const char* key, const void* value)
{
    g_setVariableCounter.Count();
    GameEnv env;
    table::SetVariable(*tableObject, key, value, env);
}

void GetTableVariableText(Furniture* tableObject, const char* key, Char16* out)
{
    g_getTextCounter.Count();
    GameEnv env;
    table::GetVariableText(*tableObject, key, out, env);
}

script::ScriptValue* GetTableValue(Furniture* tableObject, script::ScriptValue* out, int32_t kind, intptr_t data)
{
    g_getValueCounter.Count();
    GameEnv env;
    return table::GetValue(*tableObject, out, kind, data, env);
}

uint32_t SetTableValue(Furniture* tableObject, int32_t kind, intptr_t data, int32_t sourceKind, int32_t sourceData)
{
    g_setValueCounter.Count();
    GameEnv env;
    return table::SetValue(*tableObject, kind, data, sourceKind, sourceData, env);
}

// ---- ReceiveMessage --------------------------------------------------------------------------------------------------------------------

bool ReceiveTableMessage(Furniture* tableObject, Message* message)
{
    const int32_t type = message->type;
    const bool ignored = type == messages::MT_CONSTRUCT || type == messages::MT_NEED_REPAIR || type == messages::MT_CONSTRUCTION_FINISHED;
    (type == messages::MT_DO_NEXT_ACTION                   ? g_branchAction
                    : type == messages::MT_NEXT_MOVE_POINT ? g_branchNext
                    : type == messages::MT_MOVEIMHERE      ? g_branchMove
                    : type == messages::MT_IM_READY        ? g_branchReady
                    : type == messages::MT_GUEST_SERVED    ? g_branchServed
                    : ignored                              ? g_branchIgnored
                                                           : g_branchBase)
            .Count();
    g_receiveCounter.Count();
    GameEnv env;
    return table::ReceiveMessage(*tableObject, *message, env);
}

// ---- Draw ------------------------------------------------------------------------------------------------------------------------------

// The renderer's sprite functions  (the order and arguments of the calls are covered by the unit test).
struct DrawEnv {
    float Time() { return game::GetWorld()->m_gameTime; }
    object::Base* Object(int32_t id) { return ObjectById(id); }
    void WorldDrawPos(SpriteInstance* sprite, float* world, float time, int& x, int& y, int16_t& depth)
    {
        (sprite)->GetWorldDrawPos(world, time, &x, &y, &depth);
    }
    void DrawInWorld(SpriteInstance* sprite, float* world, float time) { (sprite)->DrawInWorld(world, time); }
    void DrawClipped(SpriteInstance* sprite, int x, int y, uint32_t depth, float time) { (sprite)->DrawClipped(x, y, depth, time); }
    float GuestIllness(object::Base& guest) { return object::GuestFoodIllness(static_cast<object::Guest&>(guest)); }
    float GuestTone(object::Base& guest) { return static_cast<float>(object::GuestPaletteTone(static_cast<object::Guest&>(guest))); }
    void DrawInWorldToned(SpriteInstance* sprite, float* world, float time, float tone, float illness)
    {
        (sprite)->DrawInWorldToned(world, time, tone, illness);
    }
    void DrawClippedToned(SpriteInstance* sprite, int x, int y, uint32_t depth, float time, float tone, float illness)
    {
        (sprite)->DrawClippedToned(x, y, depth, time, tone, illness);
    }
};

void DrawTable(Furniture* tableObject)
{
    g_drawCounter.Count();
    DrawEnv env;
    table::Draw(*tableObject, env);
}

// ---- Driver --------------------------------------------------------------------------------------------------------------------------
// Calls DrawTable on every table of the world (`call-drive objects.table-draw.calls`), so a test sees the draw reach the renderer.

void DriveDraw(int rounds)
{
    auto* manager = game::GetObjectManager();
    if (!manager) return;
    for (int round = 0; round < rounds; ++round) {
        for (uint32_t i = 0; i < manager->m_others.size(); ++i) {
            object::Base* base = manager->m_others[i];
            if (base && base->m_type == object::ObjectType::Table) DrawTable(static_cast<Furniture*>(base));
        }
    }
}

} // namespace

bool TableSeatGuest(Furniture& tableObject, int32_t guest)
{
    g_seatCounter.Count();
    GameEnv env;
    return table::SeatGuest(tableObject, guest, env);
}

void TableAttachToBuilding(Furniture& tableObject, int32_t building)
{
    g_attachCounter.Count();
    GameEnv env;
    table::AttachToBuilding(tableObject, building, env);
}

void TableSetOrientation(Furniture& tableObject, int32_t orientation)
{
    g_orientationCounter.Count();
    table::SetOrientation(tableObject, orientation);
}

bool RegisterFurniture()
{
    g_drawDrive.SetDriver(&DriveDraw);
    return true;
}
} // namespace obl::ui

namespace obl::object {
Furniture::Furniture(const char* name) : Prop(name)
{
    ui::g_constructCounter.Count();
    ui::GameEnv env;
    table::Construct(*this, name, env);
}

Furniture::~Furniture()
{
    ui::g_destroyCounter.Count();
    ui::GameEnv env;
    table::Destruct(*this, env);
}
bool Furniture::OnEnterState(uint32_t state)
{
    return ui::EnterStateTable(this, state);
}
bool Furniture::Update()
{
    return ui::UpdateTable(this);
}
bool Furniture::ReceiveMessage(messages::Message* message)
{
    return ui::ReceiveTableMessage(this, message);
}
void Furniture::GetVariableText(const char* key, Char16* out)
{
    ui::GetTableVariableText(this, key, out);
}
void Furniture::SetVariable(const char* key, const void* value)
{
    ui::SetTableVariable(this, key, value);
}
script::ScriptValue* Furniture::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    return ui::GetTableValue(this, out, key, data);
}
void Furniture::SetValue(int key, intptr_t data, int type, int bits)
{
    ui::SetTableValue(this, key, data, type, bits);
}
void Furniture::Draw()
{
    ui::DrawTable(this);
}
} // namespace obl::object
