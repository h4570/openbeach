#include "core/text16.h"
#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/script_text.h"

#include "importer/src/readers/save/save_writer.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/shark.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using obl::object::Shark;
using obl::script::ScriptProgram;
using obl::script::ScriptValue;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;

namespace {

obl::script::ScriptProgram* const kScript = reinterpret_cast<obl::script::ScriptProgram*>(static_cast<uintptr_t>(0xc0de0));

// The game as a script of answers; every call is written to `log` in order.
struct SharkEnv {
    std::string log;
    bool moving = true;
    bool receipt = false, movement = false;
    std::vector<int> rands;
    size_t nextRand = 0;
    std::vector<object::Base*> objects;
    std::vector<Message> posted;
    float animLength = 2.5f;
    const char* animName = nullptr;
    int32_t typeAtStart = -1;
    std::string seenCurrent; // m_curMessage as the script saw it
    const Shark* shark = nullptr;
    const char* fatalName = nullptr;
    int invalidFunctions = 0;
    std::vector<int32_t> radii;

    // What Mover's constructor (the base object's, in the game) leaves before Shark's runs.
    void BaseConstruct(Shark& creature, const char* name)
    {
        add(std::string("Base(") + name + ")");
        std::memset(reinterpret_cast<char*>(&creature) + sizeof(void*), 0x5a, 0x24 - sizeof(void*));
    }
    obl::script::ScriptProgram* ScriptLookup(const char* name)
    {
        add(std::string("Lookup(") + name + ")");
        return kScript;
    }
    void RunOnEntry(int32_t state) { add("Entry(" + std::to_string(state) + ")"); }
    void RunOnExit(int32_t state) { add("Exit(" + std::to_string(state) + ")"); }
    void RunOnUpdate(int32_t state) { add("ScriptUpdate(" + std::to_string(state) + ")"); }
    bool RunOnReceipt(const Message& m)
    {
        add("Receipt(" + std::to_string(m.type) + ")");
        record();
        return receipt;
    }
    bool Movement(const Message& m)
    {
        add("Movement(" + std::to_string(m.type) + ")");
        record();
        return movement;
    }
    void record()
    {
        char text[160];
        const Message* current = shark->m_curMessage;
        std::snprintf(text, sizeof(text), "%d/%d/%d/%d/%d/%d", current->sender, current->receiver, current->type, current->flags,
                current->data, current->extra);
        seenCurrent = text;
    }
    void SendThroughScript(const Message& m) { add("Send(" + std::to_string(m.type) + ")"); }
    void SendThroughScriptDelayed(const Message& m, float delay)
    {
        add("Delayed(" + std::to_string(m.type) + "," + std::to_string(delay) + ")");
    }
    bool MovingUpdate()
    {
        add("MovingUpdate");
        return moving;
    }
    void FatalUpdate(const char* name)
    {
        add("FatalUpdate");
        fatalName = name;
    }
    void FatalInvalidFunction()
    {
        add("FatalInvalid");
        ++invalidFunctions;
    }
    float StartAnimation(void*, const char* name, float time, const float*)
    {
        animName = name;
        add("Anim(" + std::string(name) + "," + std::to_string(time) + ")");
        return animLength;
    }
    float WorldTime() { return 12.0f; }
    int Rand()
    {
        add("Rand");
        return nextRand < rands.size() ? rands[nextRand++] : 0;
    }
    void Post(const Message& m)
    {
        add("Post(" + std::to_string(m.receiver) + "," + std::to_string(m.type) + ")");
        posted.push_back(m);
    }
    uint32_t ObjectsNear(const Shark&, int32_t radius, object::Base* const*& list)
    {
        radii.push_back(radius);
        list = objects.data();
        return static_cast<uint32_t>(objects.size());
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

std::unique_ptr<Shark> NewShark()
{
    auto shark = std::make_unique<Shark>();
    std::strcpy(shark->m_Name, "Shark1");
    shark->m_id = 900;
    shark->m_state = 0x86;
    shark->m_script = kScript;
    return shark;
}

object::Base* Swimmer(uint32_t id, int type, int state)
{
    auto* object = new object::Base();
    obl::unit::FillObject<object::Base>(*object);
    object->m_id = id;
    object->m_type = static_cast<object::ObjectType>(type);
    object->m_state = state;
    return object;
}

struct Crowd {
    std::vector<object::Base*> all;
    object::Base* Add(uint32_t id, int type, int state) { return all.emplace_back(Swimmer(id, type, state)); }
    ~Crowd()
    {
        for (object::Base* object : all) delete object;
    }
};

} // namespace

OBL_TEST_CASE("shark.has_state_is_zero_one_and_the_four_script_states")
{
    for (uint32_t state = 0; state < 0x120; ++state) {
        const bool expected = state == 0 || state == 1 || (state >= 0x85 && state <= 0x88);
        OBL_CHECK_EQ(shark::HasState(state), expected);
    }
    OBL_CHECK(!shark::HasState(0xffffffffu));
}

OBL_TEST_CASE("shark.set_bounds_and_init_param_are_plain_stores")
{
    auto s = NewShark();
    shark::SetBounds(*s, 1, -2, 300, 400);
    OBL_CHECK_EQ(s->m_SwimCircleRect[0], 1);
    OBL_CHECK_EQ(s->m_SwimCircleRect[1], -2);
    OBL_CHECK_EQ(s->m_SwimCircleRect[2], 300);
    OBL_CHECK_EQ(s->m_SwimCircleRect[3], 400);
    shark::SetInitParam(*s, 77);
    OBL_CHECK_EQ(s->m_ownerId, 77);
}

OBL_TEST_CASE("shark.construct_sets_ranges_script_and_speed_and_leaves_the_rest")
{
    auto s = std::make_unique<Shark>();
    obl::unit::FillObject<Shark>(*s, 0xa5);
    SharkEnv env;
    env.BaseConstruct(*s, "SHARK");
    OBL_CHECK(shark::Construct(*s, env) == s.get());
    OBL_CHECK_EQ(env.log, std::string("Base(SHARK) Lookup(SharkLogic)"));
    OBL_CHECK_EQ(s->m_myVictim, -1);
    OBL_CHECK_EQ(s->m_alertRange, 5);
    OBL_CHECK_EQ(s->m_sniffRange, 10);
    OBL_CHECK(s->m_script == kScript);
    OBL_CHECK_EQ(s->m_guestsEaten, 0);
    OBL_CHECK_EQ(s->m_maxForce, 50.0f);
    OBL_CHECK_EQ(s->m_maxVel, 100.0f);
    // not written: the fixture's fill stays
    OBL_CHECK_EQ(s->m_SwimCircleRect[0], static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK_EQ(s->m_actionState, 0xa5a5a5a5u);
    OBL_CHECK_EQ(s->m_ownerId, static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK(s->m_curMessage == reinterpret_cast<Message*>((~uintptr_t{0} / 0xff * 0xa5)));
}

OBL_TEST_CASE("shark.states_run_the_script_of_the_state")
{
    auto s = NewShark();
    SharkEnv env;
    OBL_CHECK(shark::OnEnterState(*s, 0x87, env));
    OBL_CHECK(shark::OnExitState(*s, env));
    OBL_CHECK_EQ(env.log, std::string("Entry(135) Exit(134)"));
}

OBL_TEST_CASE("shark.update_runs_the_base_then_the_script_and_reports_a_failed_base")
{
    auto s = NewShark();
    SharkEnv env;
    OBL_CHECK(shark::Update(*s, env));
    OBL_CHECK_EQ(env.log, std::string("MovingUpdate ScriptUpdate(134)"));
    env.log.clear();
    env.moving = false;
    OBL_CHECK(shark::Update(*s, env));
    OBL_CHECK_EQ(env.log, std::string("MovingUpdate FatalUpdate ScriptUpdate(134)"));
    OBL_CHECK_EQ(std::string(env.fatalName), std::string("Shark1"));
}

OBL_TEST_CASE("shark.receive_message_keeps_a_copy_for_the_script_and_puts_the_old_one_back")
{
    auto s = NewShark();
    Message previous{};
    s->m_curMessage = &previous;
    SharkEnv env;
    env.shark = s.get();
    Message message{};
    message.sender = 1;
    message.receiver = 2;
    message.type = 60;
    message.flags = 4;
    message.data = 5;
    message.extra = 6;
    message.floatData = 1.0f;
    env.receipt = true;
    OBL_CHECK(shark::ReceiveMessage(*s, message, env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(60)"));
    OBL_CHECK_EQ(env.seenCurrent, std::string("1/2/60/4/5/0"));
    OBL_CHECK(s->m_curMessage == &previous);

    env.log.clear();
    env.receipt = false;
    env.movement = true;
    OBL_CHECK(shark::ReceiveMessage(*s, message, env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(60) Movement(60)"));
    env.log.clear();
    env.movement = false;
    OBL_CHECK(!shark::ReceiveMessage(*s, message, env));
    OBL_CHECK(s->m_curMessage == &previous);
}

OBL_TEST_CASE("shark.send_message_needs_a_script")
{
    auto s = NewShark();
    SharkEnv env;
    Message message{};
    message.type = 9;
    shark::SendMessage(*s, message, env);
    shark::SendMessageDelayed(*s, message, 2.0f, env);
    OBL_CHECK_EQ(env.log, std::string("Send(9) Delayed(9,2.000000)"));
    env.log.clear();
    s->m_script = nullptr;
    shark::SendMessage(*s, message, env);
    shark::SendMessageDelayed(*s, message, 2.0f, env);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("shark.play_animation_picks_the_type_and_the_message_text")
{
    auto s = NewShark();
    SpriteInstance sprite{};
    s->m_sprite = &sprite;
    SharkEnv env;
    OBL_CHECK_EQ(shark::PlayAnimation(*s, "swim", true, false, env), 2.5f);
    OBL_CHECK_EQ(sprite.m_animType, 1);
    shark::PlayAnimation(*s, "swim", true, true, env);
    OBL_CHECK_EQ(sprite.m_animType, 1);
    shark::PlayAnimation(*s, "swim", false, true, env);
    OBL_CHECK_EQ(sprite.m_animType, 0);
    shark::PlayAnimation(*s, "swim", false, false, env);
    OBL_CHECK_EQ(sprite.m_animType, 2);
    OBL_CHECK_EQ(env.log, std::string("Anim(swim,12.000000) Anim(swim,12.000000) Anim(swim,12.000000) Anim(swim,12.000000)"));

    // "MessData": the text whose script::text id is the data word of the message being received
    Message current{};
    static const char text[] = "bite";
    current.data = obl::script::text::Id(text);
    s->m_curMessage = &current;
    env.log.clear();
    shark::PlayAnimation(*s, "MessData", false, false, env);
    OBL_CHECK_EQ(env.log, std::string("Anim(bite,12.000000)"));
    shark::PlayAnimation(*s, "messdata", false, false, env); // the comparison is exact
    OBL_CHECK_EQ(std::string(env.animName), std::string("messdata"));

    env.animLength = 0.0f;
    OBL_CHECK_EQ(shark::PlayAnimation(*s, "swim", false, false, env), 1.0f);
    env.animLength = -0.0f;
    OBL_CHECK_EQ(shark::PlayAnimation(*s, "swim", false, false, env), 1.0f);
    env.animLength = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(shark::PlayAnimation(*s, "swim", false, false, env), 1.0f);
    env.animLength = -3.0f;
    OBL_CHECK_EQ(shark::PlayAnimation(*s, "swim", false, false, env), -3.0f);
}

OBL_TEST_CASE("shark.alert_guests_warns_every_swimming_guest_in_range")
{
    auto s = NewShark();
    Crowd crowd;
    SharkEnv env;
    env.objects = {crowd.Add(1, 3, 0x16), crowd.Add(2, 3, 0x15), crowd.Add(3, 2, 0x16), crowd.Add(4, 3, 0x16)};
    shark::AlertGuests(*s, env);
    OBL_CHECK_EQ(env.log, std::string("Post(1,84) Post(4,84)"));
    OBL_CHECK_EQ(env.radii.size(), 1u);
    OBL_CHECK_EQ(env.radii[0], 5);
    OBL_CHECK_EQ(env.posted[0].sender, 900);
    OBL_CHECK_EQ(env.posted[0].flags, 0);
    OBL_CHECK_EQ(env.posted[0].data, 0);
    env.objects.clear();
    env.log.clear();
    shark::AlertGuests(*s, env);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("shark.decide_swim_action_bites_a_swimming_guest")
{
    auto s = NewShark();
    Crowd crowd;
    SharkEnv env;
    env.objects = {crowd.Add(11, 3, 0x16), crowd.Add(12, 3, 0x16), crowd.Add(13, 3, 0x16)};
    env.rands = {7}; // 7 % 3 = 1: the second
    s->m_actionState = 2;
    shark::DecideSwimAction(*s, env);
    OBL_CHECK_EQ(env.log, std::string("Rand Post(12,3) Post(900,3)"));
    OBL_CHECK_EQ(env.radii[0], 10);
    OBL_CHECK_EQ(s->m_myVictim, 12);
    OBL_CHECK_EQ(s->m_actionState, 2u); // the swim state does not move on
    OBL_CHECK_EQ(env.posted[0].sender, 900);
    OBL_CHECK_EQ(env.posted[0].data, 0x36);
    OBL_CHECK_EQ(env.posted[1].receiver, 900);
    OBL_CHECK_EQ(env.posted[1].data, 0x87);
}

OBL_TEST_CASE("shark.decide_swim_action_swims_in_the_quarter_of_the_state")
{
    struct Row {
        uint32_t state;
        int32_t x, y;
    };
    // rectangle (100, 200)-(300, 600): halves 100 and 200; draws 30 (x) and 50 (y)
    const Row rows[] = {{0, 130, 250}, {1, 230, 250}, {2, 230, 450}, {3, 130, 450}};
    for (const Row& row : rows) {
        auto s = NewShark();
        shark::SetBounds(*s, 100, 200, 300, 600);
        s->m_actionState = row.state;
        SharkEnv env;
        env.rands = {30, 50};
        shark::DecideSwimAction(*s, env);
        OBL_CHECK_EQ(env.log, std::string("Rand Rand Post(900,11)"));
        OBL_CHECK_EQ(env.posted[0].flags, row.x);
        OBL_CHECK_EQ(env.posted[0].data, row.y);
        OBL_CHECK_EQ(env.posted[0].sender, 900);
        OBL_CHECK_EQ(env.posted[0].receiver, 900);
        OBL_CHECK_EQ(s->m_actionState, (row.state + 1) & 3);
    }
}

OBL_TEST_CASE("shark.decide_swim_action_clears_the_victim_and_falls_back_to_swimming")
{
    auto s = NewShark();
    shark::SetBounds(*s, 0, 0, 100, 100);
    s->m_myVictim = 12;
    SharkEnv env;
    env.rands = {5, 6};
    Crowd crowd;
    env.objects = {crowd.Add(11, 3, 0x16)};
    shark::DecideSwimAction(*s, env);
    OBL_CHECK_EQ(s->m_myVictim, -1);
    OBL_CHECK(env.radii.empty()); // no search while a victim is set
    OBL_CHECK_EQ(env.log, std::string("Rand Rand Post(900,11)"));

    // without a victim, an object that is not a swimming guest is no victim and the shark swims
    auto t = NewShark();
    shark::SetBounds(*t, 0, 0, 100, 100);
    SharkEnv other;
    other.rands = {1, 5, 6};
    other.objects = {crowd.Add(21, 3, 0x10)};
    shark::DecideSwimAction(*t, other);
    OBL_CHECK_EQ(other.log, std::string("Rand Rand Rand Post(900,11)"));
    OBL_CHECK_EQ(t->m_myVictim, -1);

    // nobody near: no draw for the search
    SharkEnv empty;
    empty.rands = {5, 6};
    shark::DecideSwimAction(*t, empty);
    OBL_CHECK_EQ(empty.log, std::string("Rand Rand Post(900,11)"));
}

OBL_TEST_CASE("shark.decide_swim_action_x_goes_through_a_float_and_y_stays_exact")
{
    auto s = NewShark();
    shark::SetBounds(*s, 0, 0, 0x7ffffff0, 0x7ffffff0); // halves 0x3ffffff8
    s->m_actionState = 1;
    SharkEnv env;
    env.rands = {0x7fff, 0x7fff};
    shark::DecideSwimAction(*s, env);
    const int32_t half = 0x3ffffff8;
    OBL_CHECK_EQ(env.posted[0].flags, static_cast<int32_t>(static_cast<float>(0x7fff + half)));
    OBL_CHECK_EQ(env.posted[0].data, 0x7fff);
}

OBL_TEST_CASE("shark.decide_swim_action_with_an_empty_rectangle_stops_instead_of_trapping_it")
{
    auto s = NewShark();
    shark::SetBounds(*s, 5, 5, 6, 100); // width 1: half 0
    s->m_actionState = 3;
    SharkEnv env;
    shark::DecideSwimAction(*s, env);
    OBL_CHECK(env.log.empty());
    OBL_CHECK_EQ(s->m_actionState, 3u);
    shark::SetBounds(*s, 5, 5, 100, 6); // height 1: the x was already drawn
    shark::DecideSwimAction(*s, env);
    OBL_CHECK_EQ(env.log, std::string("Rand"));
    OBL_CHECK_EQ(s->m_actionState, 3u);
}

OBL_TEST_CASE("shark.call_function_dispatches_the_two_script_functions")
{
    auto s = NewShark();
    shark::SetBounds(*s, 0, 0, 100, 100);
    SharkEnv env;
    shark::CallFunction(*s, 4, env);
    OBL_CHECK_EQ(env.log, std::string("Rand Rand Post(900,11)"));
    env.log.clear();
    shark::CallFunction(*s, 0xb3, env);
    OBL_CHECK(env.log.empty()); // nobody near
    shark::CallFunction(*s, 5, env);
    shark::CallFunction(*s, -1, env);
    OBL_CHECK_EQ(env.invalidFunctions, 2);
}

namespace {
// Checks every field table entry's obl.save key against the converter's bridge table for the class.
struct BridgeKeys {
    uint8_t type;
    int checked = 0;
    void Check(const char* key)
    {
        OBL_CHECK_EQ(std::string(convert::OldObjectFieldKeyKnown(type, key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
    }
    template<class T> void Int(const char* key, T&, object::Hashed) { Check(key); }
    template<class T> void UInt(const char* key, T&, object::Hashed) { Check(key); }
};
} // namespace

namespace {
struct ValuesEnv {
    object::Base victim{};
    int key = 0, type = 0, bits = 0, draws = 0;
    intptr_t data = 0;
    object::Base* Object(int id)
    {
        OBL_CHECK_EQ(id, 77);
        return &victim;
    }
    ScriptValue BaseGet(const Shark&, int k, intptr_t d)
    {
        key = k;
        data = d;
        return {3, 123};
    }
    void BaseSet(Shark&, int k, intptr_t d, int t, int b)
    {
        key = k;
        data = d;
        type = t;
        bits = b;
    }
    int ToInt(int t, int b)
    {
        type = t;
        bits = b;
        return b;
    }
    void PrintInt(Char16* out, const Char16* format, int value) { obl::text16::Format(out, 32, format, value); }
    void BaseDraw(Shark&) { ++draws; }
};
} // namespace
OBL_TEST_CASE("shark.script_values_and_inherited_arguments")
{
    auto creature = NewShark();
    ValuesEnv env;
    Message message{};
    message.sender = -7;
    message.data = 1234;
    creature->m_curMessage = &message;
    SpriteInstance sprite{}, victimSprite{};
    creature->m_sprite = &sprite;
    sprite.m_direction = -3;
    env.victim.m_sprite = &victimSprite;
    victimSprite.m_direction = 6;
    env.victim.m_WorldXYZ[0] = -17.75f;
    env.victim.m_WorldXYZ[1] = 23.875f;
    creature->m_myVictim = 77;
    creature->m_actionState = UINT32_MAX;
    creature->m_ownerId = INT32_MIN;
    const int keys[] = {0, 5, 0x38, 0x40, 0xf5, 0xf6, 0xf7, 0xf8, 0x10f};
    const int values[] = {-7, 1234, -3, -1, 77, -17, 23, 6, INT32_MIN};
    for (int i = 0; i < 9; ++i) {
        const auto value = shark::GetValue(*creature, keys[i], 91, env);
        OBL_CHECK_EQ(value.type, 2);
        OBL_CHECK_EQ(value.bits, values[i]);
    }
    const auto inherited = shark::GetValue(*creature, -100, 91, env);
    OBL_CHECK_EQ(inherited.type, 3);
    OBL_CHECK_EQ(inherited.bits, 123);
    OBL_CHECK_EQ(env.key, -100);
    OBL_CHECK_EQ(env.data, 91);
    shark::SetValue(*creature, 0xf0, 93, 1, 987, env);
    OBL_CHECK_EQ(env.key, 0xf0);
    OBL_CHECK_EQ(env.data, 93);
    OBL_CHECK_EQ(env.type, 1);
    OBL_CHECK_EQ(env.bits, 987);
    shark::SetValue(*creature, 0x40, 93, 2, -1, env);
    OBL_CHECK_EQ(creature->m_actionState, UINT32_MAX);
}
OBL_TEST_CASE("shark.direction_clamps_only_upper_bound_and_text_preserves_unrecognized_buffer")
{
    auto creature = NewShark();
    ValuesEnv env;
    SpriteInstance instance{};
    Sprite sprite{};
    SpriteAnim anim{};
    SpriteAnim* animations[] = {&anim};
    creature->m_sprite = &instance;
    instance.m_sprite = &sprite;
    sprite.m_anims = animations;
    for (int directions : {0, 1, 8, 255}) {
        anim.m_directions = static_cast<uint8_t>(directions);
        for (int value : {INT32_MIN, -1, 0, 1, 7, 8, 255, INT32_MAX}) {
            shark::SetValue(*creature, 0x38, 0, 2, value, env);
            OBL_CHECK_EQ(instance.m_direction, value >= directions ? directions - 1 : value);
        }
    }
    Char16 out[32] = u"unchanged";
    for (const char* key : {"", "SHARK_GUESTS_EATEN_", "shark_guests_eaten"}) {
        shark::GetVariableText(*creature, key, out, env);
        OBL_CHECK(obl::text16::Compare(out, u"unchanged") == 0);
    }
    creature->m_guestsEaten = INT32_MIN;
    shark::GetVariableText(*creature, "SHARK_GUESTS_EATEN", out, env);
    OBL_CHECK(obl::text16::Compare(out, u"-2147483648") == 0);
    shark::Draw(*creature, env);
    OBL_CHECK_EQ(env.draws, 1);
}

OBL_TEST_CASE("shark.field_keys_match_the_save_bridge")
{
    BridgeKeys keys{static_cast<uint8_t>(object::ObjectType::Shark)};
    Shark shark{};
    object::VisitFields(shark, keys);
    OBL_CHECK_EQ(keys.checked, 10);
}
