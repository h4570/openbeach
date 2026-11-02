#include "core/char16.h"
#include "game/world/world_receive.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "game/ui/sprite.h"
#include "game/world/world_script_host_game.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/game/host_text.h"
#include "game/events/disaster_placement.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_shark.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "formats/map_pack.h"
#include "game/map/grid_lifecycle.h"
#include "game/world/world_state.h"
#include "game/objects/shark.h"
#include "game/objects/object_base.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <algorithm>
#include <cstring>
#include "game/objects/script_program_run.h"
#include "game/game/diagnostics.h"
#include "game/objects/script_text.h"
#include "game/objects/script_value_convert.h"
#include "game/objects/object_scratch.h"

// The shark of the Sharks disaster (the logic is shark.h), and the test drivers that put sharks into a world.
namespace obl::object {
using game::Rand;

namespace {
using messages::Message;
using script::ScriptProgram;
using ui::SpriteInstance;

test::CallCounter g_construct("objects.shark-construct"), g_bounds("objects.shark-bounds"), g_initParam("objects.shark-init-param"),
        g_state("objects.shark-state"), g_enter("objects.shark-enter-state"), g_update("objects.shark-update"),
        g_receive("objects.shark-receive"), g_play("objects.shark-play-animation"), g_call("objects.shark-call-function"),
        g_setValue("objects.shark-set-value"), g_text("objects.shark-variable-text"), g_draw("objects.shark-draw");

// The game side of the Env of src/game/objects/shark.h.
struct GameEnv {
    Shark& owner;
    object::Base* Object(int32_t id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    script::ScriptValue BaseGet(const Shark& shark, int32_t key, intptr_t data)
    {
        script::ScriptValue out;
        return *const_cast<Shark&>(shark).Mover::GetValue(&out, key, data);
    }
    void BaseSet(Shark& shark, int32_t key, intptr_t data, int32_t type, int32_t bits) { shark.Mover::SetValue(key, data, type, bits); }
    int32_t ToInt(int32_t type, int32_t bits) { return script_value::ToInt(type, static_cast<uint32_t>(bits)); }
    void PrintInt(Char16* out, const Char16* format, int32_t value) { ui::host_text::Print(out, format, value); }
    void BaseDraw(Shark& shark) { shark.object::Base::Draw(); }

    ScriptProgram* ScriptLookup(const char* name) { return game::GetWorldScriptHost()->Find(name); }
    void RunOnEntry(int32_t state) { object::ScriptProgramRunOnEntry(*owner.m_script, &owner, state); }
    void RunOnExit(int32_t state) { object::ScriptProgramRunOnExit(*owner.m_script, &owner, state); }
    void RunOnUpdate(int32_t state) { object::ScriptProgramRunOnUpdate(*owner.m_script, &owner, state); }
    bool RunOnReceipt(const Message& message) { return object::ScriptProgramRunOnReceipt(*owner.m_script, &owner, message) != 0; }
    void SendThroughScript(const Message& message) { object::ScriptProgramSendMessage(*owner.m_script, &owner, message); }
    void SendThroughScriptDelayed(const Message& message, float delay)
    {
        object::ScriptProgramSendMessageDelayed(*owner.m_script, &owner, message, delay);
    }
    bool MovingUpdate() { return owner.Mover::Update(); }
    bool Movement(const Message& message)
    {
        Message copy = message;
        return owner.Mover::ReceiveMessage(&copy);
    }
    void FatalUpdate(const char* name) { game::FatalError("Movement update of shark \"%s\" failed", name); }
    void FatalInvalidFunction() { game::FatalError("A script called a function sharks do not have"); }
    float StartAnimation(void* sprite, const char* name, float startTime, const float* world)
    {
        return static_cast<SpriteInstance*>(sprite)->SetAnimByName(name, startTime, const_cast<float*>(world));
    }
    float WorldTime() { return game::GetWorld()->m_gameTime; }
    int Rand() { return game::Rand(); }
    void Post(const Message& message)
    {
        Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    uint32_t ObjectsNear(const Shark& shark, int32_t radius, object::Base* const*& list)
    {
        int tile[2];
        shark.GetTilePosition(tile);
        m_found = object::object_manager::GetObjectsNearTile(game::GetObjectManager(), tile[0], tile[1], radius);
        list = m_found.data();
        return static_cast<uint32_t>(m_found.size());
    }

    object::object_manager::ObjectList m_found; // the last query, read by the pure logic through the pointer it returned
};

// ---- Drivers ----------------------------------------------------------------------------------------------------------------------------

Shark* FirstShark()
{
    const auto objects = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<int>(ObjectType::Shark));
    return objects.empty() ? nullptr : static_cast<Shark*>(objects[0]);
}

// All shark branches, conversion kinds and inherited values on copies of a live shark.
void DriveValues(int rounds)
{
    Shark* live = FirstShark();
    auto* manager = game::GetObjectManager();
    if (!live || !manager || !manager->m_guests.size()) {
        OBL_LOG("autotest: FAILED shark values driver needs shark and guest");
        return;
    }
    Scratch<Shark> scratch(*live); // shares the live shark's sprite and route: its teardown must not run
    Shark& copy = *scratch;
    SpriteInstance sprite = *static_cast<SpriteInstance*>(live->m_sprite);
    copy.m_sprite = &sprite;
    Message message{};
    message.sender = -123;
    message.data = INT32_MAX;
    copy.m_curMessage = &message;
    copy.m_myVictim = manager->m_guests[0]->m_id;
    for (int round = 0; round < rounds; ++round) {
        for (int32_t bits : {INT32_MIN, -1, 0, 1, 7, INT32_MAX}) {
            copy.m_ownerId = bits;
            copy.m_actionState = bits;
            copy.m_guestsEaten = bits;
            for (int32_t key : {0, 5, 0x38, 0x40, 0xf5, 0xf6, 0xf7, 0xf8, 0x10f, 0xef, 0xf0}) {
                script::ScriptValue out;
                copy.GetValue(&out, key, 0x76543210);
            }
            for (const char* key : {"SHARK_GUESTS_EATEN", "shark_guests_eaten", "SHARK_GUESTS_EATEN_", "", "INVALID_KEY"}) {
                Char16 out[32];
                std::fill_n(out, 32, Char16{0x7777});
                copy.GetVariableText(key, out);
            }
            for (int32_t type : {0, 2, 3}) {
                for (int32_t key : {0x38, 0x40, 0xf0}) copy.SetValue(key, 99, type, bits);
            }
            for (int32_t key : {0x38, 0x40, 0xf0}) copy.SetValue(key, 99, 1, script::text::Id("-17"));
        }
    }
}

// The place command of the Sharks disaster (event_manager.cpp): `count` sharks in the rectangle the disaster scripts use, each at its own
// point. Unlike the disaster's synthetic burst it leaves nothing in the event schedule, so a world with these sharks can be saved and loaded.
void DriveSpawn(int count)
{
    if (!game::GetWorld()) {
        OBL_LOG("autotest: FAILED call-drive objects.shark-spawn: needs gameplay");
        return;
    }
    std::array<int, 4> eventArea;
    std::string areaError;
    if (!map_pack::ReadEventArea(map::PackDirectory(), game::GetWorld()->m_grid->m_gridFileName, eventArea, areaError)) {
        game::FatalError("%s", areaError.c_str());
        return;
    }
    for (int i = 0; i < count; ++i) {
        Message message{};
        message.sender = message.receiver = -2;
        message.data = static_cast<int>(object::ObjectType::Shark);
        disaster::DisasterPlacement placement{};
        std::strcpy(placement.m_Name, "Shark");
        placement.m_EventIndex = i;
        std::memcpy(placement.m_Rect, eventArea.data(), sizeof(placement.m_Rect));
        placement.m_Position[0] = 3000.0f + 250.0f * static_cast<float>(i);
        placement.m_Position[1] = 5600.0f + 300.0f * static_cast<float>(i);
        world::PostPlaceCommand(message, &placement);
    }
}
// `call-drive objects.shark-spawn <count>` puts sharks in the world (a fixture); `call-drive objects.shark-get-value <rounds>` runs
// every value branch on copies of a live shark.
test::CallCounter g_spawn("objects.shark-spawn", &DriveSpawn), g_getValue("objects.shark-get-value", &DriveValues);
} // namespace

Shark::Shark(const char* name) : Mover(name)
{
    g_construct.Count();
    GameEnv env{*this};
    shark::Construct(*this, env);
}

void Shark::SetBounds(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    g_bounds.Count();
    shark::SetBounds(*this, left, top, right, bottom);
}

void Shark::SetInitParam(int32_t value)
{
    g_initParam.Count();
    shark::SetInitParam(*this, value);
}

bool Shark::HasState(uint32_t state)
{
    g_state.Count();
    return shark::HasState(state);
}
// OnEnterState, OnExitState, Update, ReceiveMessage and the two Send methods run the script, which can do anything to the world: the
// unit test pins the order of the calls.
bool Shark::OnEnterState(uint32_t state)
{
    g_enter.Count();
    GameEnv env{*this};
    return shark::OnEnterState(*this, state, env);
}
bool Shark::Update()
{
    g_update.Count();
    GameEnv env{*this};
    return shark::Update(*this, env);
}
bool Shark::OnExitState()
{
    GameEnv env{*this};
    return shark::OnExitState(*this, env);
}
bool Shark::ReceiveMessage(messages::Message* message)
{
    g_receive.Count();
    GameEnv env{*this};
    return shark::ReceiveMessage(*this, *message, env);
}
void Shark::GetVariableText(const char* key, Char16* out)
{
    g_text.Count();
    GameEnv env{*this};
    shark::GetVariableText(*this, key, out, env);
}
void Shark::Send(messages::Message message)
{
    GameEnv env{*this};
    shark::SendMessage(*this, message, env);
}
void Shark::SendDelayed(messages::Message message, float delay)
{
    GameEnv env{*this};
    shark::SendMessageDelayed(*this, message, delay, env);
}
// Only the low byte of each flag counts.
float Shark::PlayAnimation(const char* name, uint32_t reverse, int, uint32_t loop, const char*)
{
    g_play.Count();
    GameEnv env{*this};
    return shark::PlayAnimation(*this, name, (reverse & 0xff) != 0, (loop & 0xff) != 0, env);
}
// GetValue reads the object manager and the current message, without mutating them.
script::ScriptValue* Shark::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    g_getValue.Count();
    GameEnv env{*this};
    *out = shark::GetValue(*this, key, data, env);
    return out;
}
void Shark::SetValue(int key, intptr_t data, int type, int bits)
{
    g_setValue.Count();
    GameEnv env{*this};
    shark::SetValue(*this, key, data, type, bits, env);
}
// The script functions: deciding the next swim action and alerting the guests nearby.
void Shark::CallFunction(int id)
{
    g_call.Count();
    GameEnv env{*this};
    shark::CallFunction(*this, id, env);
}
void Shark::Draw()
{
    g_draw.Count();
    GameEnv env{*this};
    shark::Draw(*this, env);
}
bool Shark::IsSelectable()
{
    return false;
}
} // namespace obl::object
