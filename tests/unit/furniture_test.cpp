#include "core/char16.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/furniture.h"
#include "game/messages/message_state.h"
#include "game/objects/furniture_receive.h"
#include "object_storage.h"
#include "unit_test.h"
#include "importer/src/readers/save/save_writer.h"

#include <array>
#include <climits>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "game/objects/object_create_params.h"

namespace {
using obl::messages::Message;
using obl::object::Furniture;
using obl::object::StaffCreateParams;
using obl::object::TableChair;
using obl::tables::TableDef;
using obl::ui::Sprite;
using obl::ui::SpriteAnim;
using obl::ui::SpriteInstance;
namespace table = obl::table;

// What a pointer field of a 0xa5-filled allocation reads as.
const uintptr_t kFilledPointer = static_cast<uintptr_t>(~uintptr_t{0} / 0xff * 0xa5);

// The definition's arrays are vectors: these copy the C arrays the fixtures keep for their own checks.
template<class T, size_t N> std::vector<T> Vec(const T (&values)[N])
{
    return std::vector<T>(values, values + N);
}
template<size_t N, size_t M> std::vector<std::vector<int>> Lists(const int (&rows)[N][M])
{
    std::vector<std::vector<int>> lists;
    for (const auto& row : rows) lists.emplace_back(row, row + M);
    return lists;
}
template<size_t N> std::vector<std::array<float, 2>> Points(const float (&pairs)[N][2])
{
    std::vector<std::array<float, 2>> points;
    for (const auto& pair : pairs) points.push_back({pair[0], pair[1]});
    return points;
}

// A bank sprite whose every animation has `directions` directions.
struct FakeSprite {
    SpriteAnim anims[2]{};
    SpriteAnim* list[2] = {&anims[0], &anims[1]};
    Sprite sprite{};
    explicit FakeSprite(uint8_t directions)
    {
        anims[0].m_directions = anims[1].m_directions = directions;
        sprite.m_anims = list;
        sprite.m_animCount = 2;
    }
};

// Records every call in order; memory comes from vectors the test owns.
struct FakeEnv {
    std::vector<std::string> calls;
    TableDef definition{};
    int chairDirs[8] = {0, 2, 4, 6, 1, 3, 5, 7};
    char waiterKey[16] = "SNOOTY";
    FakeSprite tableBank{1}, chairBank{8}, plateBank{8};
    std::vector<std::unique_ptr<SpriteInstance>> instances;
    std::vector<std::unique_ptr<uint8_t[]>> blocks;
    std::vector<void*> freed, released;
    float time = 12.5f;
    obl::object::Base waiter{};
    bool hasWaiter = true;
    int32_t askedObject = 0;
    std::vector<Message> posts;
    std::vector<int32_t> cleared;
    std::vector<std::pair<int32_t, int32_t>> actionStates;
    Message placed{};
    StaffCreateParams placedWaiter{};
    int places = 0;
    int32_t spawnBuilding = 0;
    bool baseUpdate = true;
    std::string fatal, fatalArgument;
    obl::script::ScriptValue baseValue = {0x11111111u, 0x22222222u};
    bool useValueScratch = false;
    int32_t getArgs[2] = {}, setArgs[4] = {};
    struct Anim {
        SpriteInstance* sprite;
        std::string name;
        float start;
    };
    std::vector<Anim> anims;

    FakeEnv()
    {
        definition.m_TableSprite = "table";
        definition.m_ChairSprite = "chair";
        definition.m_NumChairs = 4;
        definition.m_chairDirections = Vec(chairDirs);
        definition.m_waiterSprite = waiterKey;
        waiter.m_visible = 1;
    }

    // What Prop's constructor (the base object's, in the game) leaves before Furniture's runs.
    void BaseConstruct(Furniture& o, const char* name)
    {
        calls.push_back(std::string("base ") + name);
        o.m_id = 77;
        o.m_sprite = nullptr;
    }
    const TableDef* Definition(const char*)
    {
        calls.push_back("definition");
        return &definition;
    }
    void* Allocate(uint32_t bytes)
    {
        calls.push_back("allocate " + std::to_string(bytes));
        blocks.push_back(std::make_unique<uint8_t[]>(bytes ? bytes : 1));
        std::memset(blocks.back().get(), 0xa5, bytes);
        return blocks.back().get();
    }
    void ResetWorldPosition(Furniture& o)
    {
        calls.push_back("position");
        o.m_WorldXYZ[0] = o.m_WorldXYZ[1] = o.m_WorldXYZ[2] = 0;
    }
    SpriteInstance* CreateSprite(const char* name)
    {
        calls.push_back(std::string("sprite ") + name);
        instances.push_back(std::make_unique<SpriteInstance>());
        SpriteInstance* s = instances.back().get();
        const std::string which = name;
        s->m_sprite = which == "table" ? &tableBank.sprite : which == "Waiter" ? &plateBank.sprite : &chairBank.sprite;
        return s;
    }
    void StartAnim(SpriteInstance* sprite, const char* anim, float start) { anims.push_back({sprite, anim, start}); }
    float WorldTime() { return time; }

    void ReleaseSprite(SpriteInstance* sprite)
    {
        calls.push_back("release");
        released.push_back(sprite);
    }
    void Free(void* memory)
    {
        calls.push_back("free");
        freed.push_back(memory);
    }
    obl::object::Base* Object(int32_t id)
    {
        askedObject = id;
        return hasWaiter ? &waiter : nullptr;
    }

    void SetGuestActionState(int32_t guest, int32_t value)
    {
        calls.push_back("action state");
        actionStates.push_back({guest, value});
        guestState = value;
    }
    void ClearDelayed(int32_t receiver)
    {
        calls.push_back("clear");
        cleared.push_back(receiver);
    }
    void Post(const Message& message)
    {
        calls.push_back("post");
        posts.push_back(message);
    }
    void WaiterSpawnPoint(int32_t building, float out[2])
    {
        spawnBuilding = building;
        out[0] = 100.5f;
        out[1] = 200.25f;
    }
    void PlaceWaiter(const Message& command, const StaffCreateParams& w)
    {
        placed = command;
        placedWaiter = w;
        ++places;
    }
    bool BaseUpdate(Furniture&)
    {
        calls.push_back("base update");
        return baseUpdate;
    }
    void Fatal(const char* format, const char* argument)
    {
        fatal = format;
        fatalArgument = argument;
    }
    const obl::script::ScriptValue* BaseGetValue(Furniture&, obl::script::ScriptValue* scratch, int32_t kind, intptr_t data)
    {
        getArgs[0] = kind;
        getArgs[1] = static_cast<int32_t>(data);
        if (useValueScratch) {
            *scratch = baseValue;
            return scratch;
        }
        return &baseValue;
    }
    uint32_t BaseSetValue(Furniture&, int32_t kind, intptr_t data, int32_t sourceKind, int32_t sourceData)
    {
        setArgs[0] = kind;
        setArgs[1] = static_cast<int32_t>(data);
        setArgs[2] = sourceKind;
        setArgs[3] = sourceData;
        return 0xcafe;
    }

    bool baseReceive = false;
    int32_t baseReceived = 0;
    bool BaseReceive(Furniture&, const Message& message)
    {
        calls.push_back("base receive");
        baseReceived = message.type;
        return baseReceive;
    }
    int32_t movedWaiter = 0, shownWaiter = 0, animWaiter = 0;
    float waiterPoint[3] = {-1, -1, -1};
    std::string waiterAnim;
    std::vector<std::pair<Message, float>> delayed;
    void SetWaiterPosition(int32_t id, const float xyz[3])
    {
        calls.push_back("waiter position");
        movedWaiter = id;
        std::memcpy(waiterPoint, xyz, sizeof(waiterPoint));
    }
    void ShowWaiter(int32_t id)
    {
        calls.push_back("waiter visible");
        shownWaiter = id;
    }
    int32_t waiterDirection = -99;
    std::vector<std::pair<std::string, int32_t>> animationMessages;
    void HideWaiter(int32_t) { calls.push_back("waiter hidden"); }
    void SetWaiterDirection(int32_t, int32_t direction)
    {
        calls.push_back("waiter direction");
        waiterDirection = direction;
    }
    void PostWaiterAnimation(const Furniture&, const char* name, int32_t flags)
    {
        calls.push_back("animation message");
        animationMessages.emplace_back(name, flags);
    }
    void StartWaiterAnim(int32_t id, const char* anim)
    {
        calls.push_back("waiter anim");
        animWaiter = id;
        waiterAnim = anim;
    }
    void PostDelayed(const Message& message, float delay)
    {
        calls.push_back("post delayed");
        delayed.push_back({message, delay});
    }

    // MT_NEXT_MOVE_POINT
    int32_t guestState = 0;
    int32_t shownGuest = 0, movedGuest = 0, directedGuest = 0, guestDirection = -99;
    float guestPoint[3] = {-1, -1, -1};
    float buildingPoint[2] = {1000.5f, 2000.25f};
    int32_t askedBuilding = 0;
    std::vector<std::pair<std::string, int32_t>> guestAnimations; // text, receiver
    std::vector<std::pair<uint32_t, std::string>> chairAnims;
    int32_t GuestActionState(int32_t) { return guestState; }
    void ShowGuest(int32_t id)
    {
        calls.push_back("guest visible");
        shownGuest = id;
    }
    void SetGuestPosition(int32_t id, const float xyz[3])
    {
        calls.push_back("guest position");
        movedGuest = id;
        std::memcpy(guestPoint, xyz, sizeof(guestPoint));
    }
    void SetGuestDirection(int32_t id, int32_t direction)
    {
        calls.push_back("guest direction");
        directedGuest = id;
        guestDirection = direction;
    }
    void BuildingPosition(int32_t building, float out[2])
    {
        askedBuilding = building;
        out[0] = buildingPoint[0];
        out[1] = buildingPoint[1];
    }
    void PostGuestAnimation(const Furniture&, int32_t guest, const char* name)
    {
        calls.push_back("guest animation");
        guestAnimations.emplace_back(name, guest);
    }
    std::vector<int32_t> chairAnimTypes;
    void StartChairAnim(const Furniture&, uint32_t chair, const char* anim, int32_t animType)
    {
        calls.push_back("chair anim");
        chairAnims.emplace_back(chair, anim);
        chairAnimTypes.push_back(animType);
    }
    int randomValue = 0;
    int Rand()
    {
        calls.push_back("rand");
        return randomValue;
    }
};

struct Made {
    Furniture object;
    Made(FakeEnv& env, uint8_t fill = 0xa5)
    {
        obl::unit::FillObject(object, fill);
        env.BaseConstruct(object, "TABLE");
        table::Construct(object, "TABLE", env);
    }
};

// Four chairs' sprite pointer array (16 bytes on Win32).
const std::string kPointerArray = "allocate " + std::to_string(4 * sizeof(void*));

// The serialisation Env: one line per record.
struct RecordEnv {
    std::vector<std::string> records;
    void Bool(const char* name, uint8_t& value, obl::object::Hashed)
    {
        records.push_back(std::string("bool ") + name + "=" + std::to_string(value));
    }
    void Int(const char* name, int32_t& value, obl::object::Hashed)
    {
        records.push_back(std::string("int ") + name + "=" + std::to_string(value));
    }
    void UInt(const char* name, int32_t& value, obl::object::Hashed)
    {
        records.push_back(std::string("uint ") + name + "=" + std::to_string(value));
    }
    void Sprite(const char*, SpriteInstance* sprite) { records.push_back("sprite " + std::to_string(sprite->m_direction)); }
    void Base(Furniture&) { records.push_back("base"); }
};
// Reads: every field gets the next number.
struct FillEnv {
    int32_t next = 1;
    void Bool(const char*, uint8_t& value, obl::object::Hashed) { value = static_cast<uint8_t>(next++ & 1); }
    void Int(const char*, int32_t& value, obl::object::Hashed) { value = next++; }
    void UInt(const char*, int32_t& value, obl::object::Hashed) { value = next++; }
    void Sprite(const char*, SpriteInstance* sprite) { sprite->m_frameIndex = next++; }
    void Base(Furniture& tableObject) { tableObject.m_gridW = static_cast<uint8_t>(next++); }
};
} // namespace

OBL_TEST_CASE("Table construct fills the chairs, the state and leaves the rest of the allocation alone")
{
    FakeEnv env;
    env.definition.m_serveTable = 1;
    Made made(env);
    const Furniture& t = made.object;
    OBL_CHECK(t.m_definition == &env.definition);
    for (uint32_t i = 0; i < 4; ++i) {
        OBL_CHECK(t.m_chairs[i].m_ChairDir == env.chairDirs[i]);
        OBL_CHECK(t.m_chairs[i].m_ChairOccupier == -1);
        OBL_CHECK(t.m_chairs[i].m_ChairSeated == 0 && t.m_chairs[i].m_waitingForWaiter == 0);
        OBL_CHECK(t.m_chairs[i].m_position == static_cast<int32_t>(0xa5a5a5a5)); // not written
    }
    OBL_CHECK(t.m_rotation == 0 && t.m_GuestsRemoved == 0 && t.m_GuestsAdded == 0);
    OBL_CHECK(t.m_serveTable == 1 && t.m_WaiterServing == -1 && t.m_drawWaiter == 0 && t.m_waiterReady == 1);
    OBL_CHECK(t.m_TestVar == static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK(t.m_WaiterPos == static_cast<int32_t>(0xa5a5a5a5) && t.m_WaiterID == static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK(t.m_ownerId == static_cast<int32_t>(0xa5a5a5a5));
    // No plates: the plate pointers keep the allocation's contents.
    OBL_CHECK(reinterpret_cast<uintptr_t>(t.m_plateSprites) == kFilledPointer);
    OBL_CHECK(t.m_sprite == env.instances[0].get());
    const std::vector<std::string> expected = {"base TABLE", "definition", "allocate 64", "position", "sprite table", kPointerArray,
            "sprite chair", "sprite chair", "sprite chair", "sprite chair"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("Table construct turns eight-direction chairs to the definition's direction and halves it for four-direction chairs")
{
    {
        FakeEnv env;
        Made made(env);
        for (uint32_t i = 0; i < 4; ++i) OBL_CHECK(made.object.m_chairSprites[i]->m_direction == env.chairDirs[i]);
    }
    {
        FakeEnv env;
        env.chairBank.anims[0].m_directions = env.chairBank.anims[1].m_directions = 4;
        int dirs[4] = {0, 3, 6, 7};
        env.definition.m_chairDirections = Vec(dirs);
        Made made(env);
        const int32_t expected[4] = {0, 1, 3, 3};
        for (uint32_t i = 0; i < 4; ++i) OBL_CHECK(made.object.m_chairSprites[i]->m_direction == expected[i]);
    }
    {
        // The current animation decides the clamp, the first one whether there are eight directions.
        FakeEnv env;
        env.chairBank.anims[0].m_directions = 2;
        int dirs[4] = {0, 3, 6, -3};
        env.definition.m_chairDirections = Vec(dirs);
        Made made(env);
        const int32_t expected[4] = {0, 1, 1, -1};
        for (uint32_t i = 0; i < 4; ++i) OBL_CHECK(made.object.m_chairSprites[i]->m_direction == expected[i]);
    }
}

OBL_TEST_CASE("Table construct with plates makes a plate per chair, without chair sprites none at all")
{
    {
        FakeEnv env;
        env.definition.m_hasPlates = 1;
        env.plateBank.anims[0].m_directions = 4;
        Made made(env);
        const Furniture& t = made.object;
        OBL_REQUIRE(env.anims.size() == 4);
        for (uint32_t i = 0; i < 4; ++i) {
            OBL_CHECK(env.anims[i].sprite == t.m_plateSprites[i]);
            OBL_CHECK(env.anims[i].name == "d_Static_plate");
            OBL_CHECK(env.anims[i].start == 12.5f);
            // The plate takes the chair's direction unhalved, clamped to its own animation.
            OBL_CHECK(t.m_plateSprites[i]->m_direction == (env.chairDirs[i] < 4 ? env.chairDirs[i] : 3));
            OBL_CHECK(t.m_pbDrawPlate[i] == 0);
        }
        OBL_CHECK(env.calls[5] == kPointerArray && env.calls[6] == kPointerArray && env.calls[7] == "allocate 4");
        OBL_CHECK(env.calls[8] == "sprite chair" && env.calls[9] == "sprite Waiter");
    }
    {
        FakeEnv env;
        env.definition.m_hasPlates = 1;
        env.definition.m_ChairSprite = "NONE";
        Made made(env);
        OBL_CHECK(made.object.m_chairSprites == nullptr);
        OBL_CHECK(env.instances.size() == 1);
        OBL_CHECK(reinterpret_cast<uintptr_t>(made.object.m_pbDrawPlate) == kFilledPointer);
    }
    {
        // The sprite name is compared without case.
        FakeEnv env;
        env.definition.m_ChairSprite = "none";
        Made made(env);
        OBL_CHECK(made.object.m_chairSprites == nullptr);
    }
}

OBL_TEST_CASE("Table destruct releases chairs, plates and the waiter in the fixed order")
{
    FakeEnv env;
    env.definition.m_hasPlates = 1;
    Made made(env);
    Furniture& t = made.object;
    t.m_WaiterID = 4321;
    env.calls.clear();
    std::vector<void*> chairSprites(t.m_chairSprites, t.m_chairSprites + 4), plateSprites(t.m_plateSprites, t.m_plateSprites + 4);
    void* chairArray = t.m_chairSprites;
    void* plateArray = t.m_plateSprites;
    void* chairs = t.m_chairs;
    table::Destruct(t, env);
    std::vector<void*> expectedReleased = chairSprites;
    expectedReleased.insert(expectedReleased.end(), plateSprites.begin(), plateSprites.end());
    OBL_CHECK(env.released == expectedReleased);
    // The plate flags are never freed.
    const std::vector<void*> expectedFreed = {chairArray, chairs, plateArray};
    OBL_CHECK(env.freed == expectedFreed);
    OBL_CHECK(env.askedObject == 4321);
    OBL_CHECK(env.waiter.m_visible == 0 && env.waiter.m_waitingForDeath == 1);
}

OBL_TEST_CASE("Table destruct without plates leaves the waiter alone")
{
    FakeEnv env;
    Made made(env);
    Furniture& t = made.object;
    env.calls.clear();
    table::Destruct(t, env);
    OBL_CHECK(env.askedObject == 0);
    OBL_CHECK(env.waiter.m_visible == 1 && env.waiter.m_waitingForDeath == 0);
    OBL_CHECK(env.released.size() == 4);
    OBL_CHECK(env.freed.size() == 2);

    FakeEnv plates;
    plates.definition.m_hasPlates = 1;
    plates.hasWaiter = false;
    Made other(plates);
    table::Destruct(other.object, plates); // a waiter that no longer exists is skipped
    OBL_CHECK(plates.waiter.m_visible == 1);
}

OBL_TEST_CASE("An ordinary table seats guests on the first free chair until it is full")
{
    FakeEnv env;
    Made made(env);
    Furniture& t = made.object;
    t.m_chairs[0].m_ChairOccupier = 5;
    t.m_chairs[1].m_ChairSeated = 1;
    env.calls.clear();
    OBL_CHECK(table::SeatGuest(t, 900, env));
    OBL_CHECK(t.m_chairs[1].m_ChairOccupier == 900 && t.m_chairs[1].m_ChairSeated == 0 && t.m_chairs[1].m_position == 1);
    OBL_CHECK(t.m_GuestsAdded == 1);
    const std::vector<std::string> expected = {"action state", "post"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK(env.actionStates[0] == std::make_pair(900, 0));
    const Message& m = env.posts[0];
    OBL_CHECK(m.sender == 77 && m.receiver == 900 && m.type == 3 && m.flags == 0 && m.data == 0x2e);
    // The same guest again takes another chair: an ordinary table does not look for it.
    OBL_CHECK(table::SeatGuest(t, 900, env));
    OBL_CHECK(t.m_chairs[2].m_ChairOccupier == 900);
    OBL_CHECK(table::SeatGuest(t, 901, env));
    OBL_CHECK(!table::SeatGuest(t, 902, env));
    OBL_CHECK(t.m_GuestsAdded == 3 && env.posts.size() == 3 && env.cleared.empty());
}

OBL_TEST_CASE("A serve table recognises a guest that already sits there")
{
    FakeEnv env;
    env.definition.m_serveTable = 1;
    Made made(env);
    Furniture& t = made.object;
    env.calls.clear();
    OBL_CHECK(table::SeatGuest(t, 900, env));
    const std::vector<std::string> fresh = {"clear", "action state", "post"};
    OBL_CHECK(env.calls == fresh);
    OBL_CHECK(env.cleared[0] == 900 && env.actionStates[0] == std::make_pair(900, 0));
    OBL_CHECK(t.m_chairs[0].m_ChairOccupier == 900 && t.m_GuestsAdded == 1);

    env.calls.clear();
    env.posts.clear();
    t.m_chairs[0].m_ChairSeated = 1;
    OBL_CHECK(table::SeatGuest(t, 900, env));
    const std::vector<std::string> again = {"action state", "post", "post", "post"};
    OBL_CHECK(env.calls == again);
    OBL_CHECK(env.actionStates[1] == std::make_pair(900, 2));
    OBL_CHECK(t.m_GuestsAdded == 1 && t.m_chairs[0].m_ChairSeated == 1 && t.m_chairs[1].m_ChairOccupier == -1);
    OBL_CHECK(env.posts[0].type == 3 && env.posts[0].data == 0x2e);
    for (int i = 1; i < 3; ++i) {
        const Message& next = env.posts[i];
        OBL_CHECK(next.sender == 77 && next.receiver == 900 && next.type == 0x39 && next.flags == 0 && next.data == 0);
        OBL_CHECK(next.extra == 0x2e && next.floatData == 0.0f);
    }

    // Full of other guests: turned away without a message.
    for (uint32_t i = 0; i < 4; ++i) t.m_chairs[i].m_ChairOccupier = 10 + static_cast<int32_t>(i);
    env.calls.clear();
    OBL_CHECK(!table::SeatGuest(t, 900, env));
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("A table without chairs seats nobody")
{
    for (uint8_t serve : {uint8_t{0}, uint8_t{1}}) {
        FakeEnv env;
        env.definition.m_NumChairs = 0;
        env.definition.m_serveTable = serve;
        Made made(env);
        env.calls.clear();
        OBL_CHECK(!table::SeatGuest(made.object, 900, env));
        OBL_CHECK(env.calls.empty());
    }
}

OBL_TEST_CASE("Attaching a table stores the owner and asks for the waiter of a table with plates")
{
    {
        FakeEnv env;
        Made made(env);
        table::AttachToBuilding(made.object, 31, env);
        OBL_CHECK(made.object.m_ownerId == 31 && env.places == 0);
    }
    {
        FakeEnv env;
        env.definition.m_hasPlates = 1;
        Made made(env);
        table::AttachToBuilding(made.object, 31, env);
        OBL_CHECK(made.object.m_ownerId == 31 && env.places == 1 && env.spawnBuilding == 31);
        OBL_CHECK(env.placed.sender == -2 && env.placed.receiver == -2 && env.placed.type == 0 && env.placed.flags == 0 &&
                env.placed.data == 0x13);
        OBL_CHECK(std::string(env.placedWaiter.m_key) == "SNOOTY");
        OBL_CHECK(env.placedWaiter.m_pos[0] == 100.5f && env.placedWaiter.m_pos[1] == 200.25f && env.placedWaiter.m_pos[2] == 0.0f);
        OBL_CHECK(env.placedWaiter.m_homeId == 77);
    }
}

OBL_TEST_CASE("Entering state 1 announces the table to its owner and restarts its own state")
{
    FakeEnv env;
    Made made(env);
    made.object.m_ownerId = 31;
    for (uint32_t state : {0u, 2u, 0x2eu, 0xffffffffu}) OBL_CHECK(table::OnEnterState(made.object, state, env));
    OBL_CHECK(env.posts.empty());
    OBL_CHECK(table::OnEnterState(made.object, 1, env));
    OBL_REQUIRE(env.posts.size() == 2);
    OBL_CHECK(env.posts[0].sender == 77 && env.posts[0].receiver == 31 && env.posts[0].type == 0x50 && env.posts[0].data == 0);
    OBL_CHECK(env.posts[1].sender == 77 && env.posts[1].receiver == 77 && env.posts[1].type == 3 && env.posts[1].data == 0);
}

OBL_TEST_CASE("Table update is the base update and always answers true")
{
    FakeEnv env;
    Made made(env);
    std::strcpy(made.object.m_Name, "Table7");
    OBL_CHECK(table::Update(made.object, env));
    OBL_CHECK(env.fatal.empty());
    env.baseUpdate = false;
    OBL_CHECK(table::Update(made.object, env));
    OBL_CHECK(env.fatal == "Update of furniture \"%s\" failed" && env.fatalArgument == "Table7");
}

OBL_TEST_CASE("The table's only variable is TABLE_TEST_VAR")
{
    FakeEnv env;
    Made made(env);
    const int32_t value = -123456;
    table::SetVariable(made.object, "TABLE_TEST_VAR", &value, env);
    OBL_CHECK(made.object.m_TestVar == value);
    obl::Char16 text[16];
    table::GetVariableText(made.object, "TABLE_TEST_VAR", text, env);
    OBL_CHECK(obl::Text16(text) == u"-123456");
    OBL_CHECK(env.fatal.empty());

    table::SetVariable(made.object, "table_test_var", &value, env);
    OBL_CHECK(env.fatal == "A script set unrecognized furniture variable \"%s\"" && env.fatalArgument == "table_test_var");
    text[0] = u'x';
    table::GetVariableText(made.object, "OTHER", text, env);
    OBL_CHECK(env.fatal == "A script read unrecognized furniture variable \"%s\"" && env.fatalArgument == "OTHER");
    OBL_CHECK(text[0] == u'x');
}

OBL_TEST_CASE("FormatDecimal writes what swprintf %d writes")
{
    struct Row {
        int32_t value;
        const obl::Char16* text;
    };
    const Row rows[] = {
            {0, u"0"}, {7, u"7"}, {-1, u"-1"}, {10, u"10"}, {123456789, u"123456789"}, {INT_MAX, u"2147483647"}, {INT_MIN, u"-2147483648"}};
    for (const Row& row : rows) {
        obl::Char16 text[16];
        table::FormatDecimal(text, row.value);
        OBL_CHECK(obl::Text16(text) == row.text);
    }
}

OBL_TEST_CASE("Table GetValue and SetValue are the base class's")
{
    FakeEnv env;
    Made made(env);
    obl::script::ScriptValue out{};
    OBL_CHECK(table::GetValue(made.object, &out, 0xe9, 44, env) == &out);
    OBL_CHECK(out.type == 0x11111111 && out.bits == 0x22222222);
    OBL_CHECK(env.getArgs[0] == 0xe9 && env.getArgs[1] == 44);
    env.useValueScratch = true;
    env.baseValue = {-1, INT32_MIN};
    OBL_CHECK(table::GetValue(made.object, &out, 0xe9, 44, env) == &out);
    OBL_CHECK(out.type == -1 && out.bits == INT32_MIN);
    OBL_CHECK(table::SetValue(made.object, 1, 2, 3, 4, env) == 0xcafe);
    OBL_CHECK(env.setArgs[0] == 1 && env.setArgs[1] == 2 && env.setArgs[2] == 3 && env.setArgs[3] == 4);
}

OBL_TEST_CASE("Orientation turns eight-direction chairs and plates by two steps per quarter")
{
    FakeEnv env;
    env.definition.m_hasPlates = 1;
    Made made(env);
    Furniture& t = made.object;
    table::SetOrientation(t, 0);
    OBL_CHECK(t.m_rotation == 0);
    for (uint32_t i = 0; i < 4; ++i) OBL_CHECK(t.m_chairSprites[i]->m_direction == env.chairDirs[i]);
    table::SetOrientation(t, 1); // +6
    for (uint32_t i = 0; i < 4; ++i) {
        OBL_CHECK(t.m_chairSprites[i]->m_direction == (env.chairDirs[i] + 6) % 8);
        OBL_CHECK(t.m_plateSprites[i]->m_direction == (env.chairDirs[i] + 6) % 8);
    }
    OBL_CHECK(t.m_rotation == 1);
    // A sum below zero keeps C's negative remainder: 0 + (4 - 9) * 2 = -10 -> -2.
    t.m_chairSprites[0]->m_direction = 0;
    t.m_plateSprites[0]->m_direction = 7;
    table::SetOrientation(t, 9);
    OBL_CHECK(t.m_chairSprites[0]->m_direction == -2);
    OBL_CHECK(t.m_plateSprites[0]->m_direction == -3);
    OBL_CHECK(t.m_rotation == 9);
}

OBL_TEST_CASE("Orientation turns four-direction chairs once, twice with plates, and never the plates")
{
    for (uint8_t plates : {uint8_t{0}, uint8_t{1}}) {
        FakeEnv env;
        env.chairBank.anims[0].m_directions = env.chairBank.anims[1].m_directions = 4;
        env.definition.m_hasPlates = plates;
        int dirs[4] = {0, 2, 4, 6};
        env.definition.m_chairDirections = Vec(dirs);
        Made made(env);
        Furniture& t = made.object;
        std::vector<int32_t> platesBefore;
        if (plates)
            for (uint32_t i = 0; i < 4; ++i) platesBefore.push_back(t.m_plateSprites[i]->m_direction);
        table::SetOrientation(t, 1); // +3 per pass
        for (uint32_t i = 0; i < 4; ++i) {
            const int32_t start = dirs[i] / 2;
            OBL_CHECK(t.m_chairSprites[i]->m_direction == (plates ? (start + 6) % 4 : (start + 3) % 4));
            if (plates) OBL_CHECK(t.m_plateSprites[i]->m_direction == platesBefore[i]);
        }
    }
}

OBL_TEST_CASE("Orientation only stores the rotation for a table without chair sprites or chairs")
{
    FakeEnv env;
    env.definition.m_ChairSprite = "NONE";
    Made made(env);
    table::SetOrientation(made.object, 3);
    OBL_CHECK(made.object.m_rotation == 3);

    FakeEnv none;
    none.definition.m_NumChairs = 0;
    Made empty(none);
    table::SetOrientation(empty.object, 2);
    OBL_CHECK(empty.object.m_rotation == 2);
}

OBL_TEST_CASE("Table fields are saved in the fixed order and slots")
{
    FakeEnv env;
    env.definition.m_NumChairs = 2;
    env.definition.m_hasPlates = 1;
    Made made(env);
    Furniture& t = made.object;
    t.m_chairs[0] = {2, 55, 1, 0, 3};
    t.m_chairs[1] = {6, -1, 0, 1, 1};
    t.m_chairSprites[0]->m_direction = 10;
    t.m_chairSprites[1]->m_direction = 11;
    t.m_plateSprites[0]->m_direction = 20;
    t.m_plateSprites[1]->m_direction = 21;
    t.m_pbDrawPlate[1] = 1;
    t.m_drawWaiter = 1;
    t.m_WaiterServing = 55;
    t.m_WaiterPos = 2;
    t.m_WaiterID = 700;
    t.m_rotation = 3;
    t.m_ownerId = 31;
    t.m_serveTable = 1;
    t.m_GuestsRemoved = 4;
    t.m_GuestsAdded = 5;
    RecordEnv records;
    table::Fields(t, records);
    const std::vector<std::string> expected = {"bool table.chairs[].seated=1", "int table.chairs[].occupier=55",
            "uint table.chairs[].dir=2", "uint table.chairs[].position=3", "bool table.chairs[].waiting_for_waiter=0", "sprite 10",
            "bool table.chairs[].seated=0", "int table.chairs[].occupier=-1", "uint table.chairs[].dir=6", "uint table.chairs[].position=1",
            "bool table.chairs[].waiting_for_waiter=1", "sprite 11", "bool table.draw_waiter=1", "int table.waiter_serving=55",
            "uint table.waiter_pos=2", "int table.waiter=700", "bool table.waiter_ready=1", "uint table.rotation=3", "int table.owner=31",
            "bool table.serve_table=1", "uint table.guests_removed=4", "uint table.guests_added=5", "sprite 20",
            "bool table.plates[].draw=0", "sprite 21", "bool table.plates[].draw=1", "base"};
    OBL_CHECK(records.records == expected);
}

OBL_TEST_CASE("Table fields without chair sprites or plates skip the sprite records, and a load fills every field")
{
    FakeEnv env;
    env.definition.m_NumChairs = 1;
    env.definition.m_ChairSprite = "NONE";
    Made made(env);
    RecordEnv records;
    table::Fields(made.object, records);
    OBL_CHECK(records.records.size() == 5 + 10 + 1);
    OBL_CHECK(records.records[4].rfind("bool table.chairs[].waiting_for_waiter", 0) == 0 &&
            records.records[5].rfind("bool table.draw_waiter", 0) == 0);
    OBL_CHECK(records.records.back() == "base");

    FakeEnv full;
    full.definition.m_NumChairs = 1;
    full.definition.m_hasPlates = 1;
    Made loaded(full);
    FillEnv fill;
    table::Fields(loaded.object, fill);
    const Furniture& t = loaded.object;
    OBL_CHECK(t.m_chairs[0].m_ChairSeated == 1 && t.m_chairs[0].m_ChairOccupier == 2 && t.m_chairs[0].m_ChairDir == 3);
    OBL_CHECK(t.m_chairs[0].m_position == 4 && t.m_chairs[0].m_waitingForWaiter == 1 && t.m_chairSprites[0]->m_frameIndex == 6);
    OBL_CHECK(t.m_drawWaiter == 1 && t.m_WaiterServing == 8 && t.m_WaiterPos == 9 && t.m_WaiterID == 10 && t.m_waiterReady == 1);
    OBL_CHECK(t.m_rotation == 12 && t.m_ownerId == 13 && t.m_serveTable == 0 && t.m_GuestsRemoved == 15 && t.m_GuestsAdded == 16);
    OBL_CHECK(t.m_plateSprites[0]->m_frameIndex == 17 && t.m_pbDrawPlate[0] == 0 && t.m_gridW == 19);
}

OBL_TEST_CASE("AddRotatedOffset turns the offset by the table's rotation")
{
    const float base[2] = {100.0f, 200.0f}, offset[2] = {3.0f, 7.0f};
    const float expected[4][2] = {{103, 207}, {107, 197}, {97, 193}, {93, 203}};
    for (int rotation = 0; rotation < 4; ++rotation) {
        float out[2] = {};
        OBL_CHECK(table::AddRotatedOffset(out, base, offset, rotation));
        OBL_CHECK(out[0] == expected[rotation][0] && out[1] == expected[rotation][1]);
    }
    float untouched[2] = {1.5f, 2.5f};
    OBL_CHECK(!table::AddRotatedOffset(untouched, base, offset, 4));
    OBL_CHECK(!table::AddRotatedOffset(untouched, base, offset, -1));
    OBL_CHECK(untouched[0] == 1.5f && untouched[1] == 2.5f);
}

OBL_TEST_CASE("Table ReceiveMessage answers the small messages")
{
    FakeEnv env;
    Made made(env);
    Furniture& t = made.object;
    env.calls.clear();
    Message message{};
    // Nobody sits at a fresh table's chairs, so the guest's messages find no chair and the answer is true.
    for (int32_t type : {57, 100}) {
        message.type = type;
        OBL_CHECK(table::ReceiveMessage(t, message, env));
    }
    message.type = 30;
    message.sender = 4242;
    OBL_CHECK(table::ReceiveMessage(t, message, env) == std::optional<bool>(true));
    OBL_CHECK(t.m_WaiterID == 4242);
    for (int32_t type : {40, 42, 95}) {
        message.type = type;
        OBL_CHECK(table::ReceiveMessage(t, message, env) == std::optional<bool>(true));
    }
    OBL_CHECK(env.calls.empty());
    for (int32_t type : {1, 3, 56, 99, 101, -1}) {
        message.type = type;
        env.baseReceive = type % 2 != 0;
        OBL_CHECK(table::ReceiveMessage(t, message, env) == (type % 2 != 0));
        OBL_CHECK(env.baseReceived == type);
    }
    OBL_CHECK(env.calls.size() == 6);
}

namespace {
struct ArrivalFixture {
    FakeEnv env;
    Furniture table{};
    TableChair chairs[4]{};
    int routes[4][4] = {{3, 0, 1, 2}, {3, 2, 1, 0}, {3, 1, 2, 0}, {3, 0, 2, 1}};
    float walk[3][2] = {{-3.75f, 2.25f}, {10.75f, -4.5f}, {8.5f, 6.25f}};
    float serve[4][2] = {{5.25f, 7.5f}, {1, 2}, {3, 4}, {5, 6}};
    char walkAnim[8] = "walk", serveAnim[8] = "serve";
    ArrivalFixture()
    {
        env.definition.m_chairWalkPoints = Lists(routes);
        env.definition.m_walkPoints = Points(walk);
        env.definition.m_servePoints = Points(serve);
        env.definition.m_waiterWalkAnim = walkAnim;
        env.definition.m_waiterServeAnim = serveAnim;
        table.m_definition = &env.definition;
        table.m_chairs = chairs;
        table.m_id = 77;
        table.m_WaiterID = 88;
        table.m_ownerId = 99;
        table.m_WorldXYZ[0] = 100.5f;
        table.m_WorldXYZ[1] = 200.25f;
        table.m_WorldXYZ[2] = 9.5f;
        table.m_WaiterServing = 0;
    }
    void Receive()
    {
        Message message{};
        message.type = obl::messages::MT_MOVEIMHERE;
        message.sender = -123; // This branch does not validate the sender.
        OBL_CHECK(table::ReceiveMessage(table, message, env) == std::optional<bool>(true));
        for (const Message& post : env.posts) {
            OBL_CHECK(post.sender == 77);
            OBL_CHECK(post.receiver == 88);
            OBL_CHECK(post.type == obl::messages::MT_MOVE2SEAPOINT);
        }
    }
};
} // namespace

OBL_TEST_CASE("Waiter arrival advances the chair route and then uses the rotated serve point")
{
    const int32_t expectedWalk[4][2] = {{111, 195}, {96, 189}, {89, 204}, {105, 211}};
    const int32_t expectedServe[4][2] = {{105, 207}, {108, 195}, {95, 192}, {93, 205}};
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (int position = 0; position < 2; ++position) {
            ArrivalFixture f;
            f.table.m_rotation = rotation;
            f.table.m_WaiterPos = position;
            f.chairs[0].m_waitingForWaiter = 1;
            f.Receive();
            OBL_CHECK(f.table.m_WaiterPos == position + 1);
            OBL_REQUIRE(f.env.posts.size() == 1);
            const auto* expected = position == 0 ? expectedWalk[rotation] : expectedServe[rotation];
            OBL_CHECK(f.env.posts[0].flags == expected[0]);
            OBL_CHECK(f.env.posts[0].data == expected[1]);
            OBL_CHECK(f.env.calls == std::vector<std::string>{"post"});
        }
    }
}

OBL_TEST_CASE("Arrival at the chair turns and hides the waiter before requesting the serving animation")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        ArrivalFixture f;
        f.table.m_WaiterServing = 1;
        f.table.m_rotation = rotation;
        f.table.m_WaiterPos = 2;
        f.chairs[1].m_waitingForWaiter = 1;
        f.Receive();
        OBL_CHECK(f.table.m_WaiterPos == 3);
        OBL_CHECK(f.table.m_drawWaiter == 1);
        OBL_CHECK(f.env.waiterDirection == (2 + (4 - rotation) * 2) % 8);
        OBL_CHECK(f.env.movedWaiter == 88);
        OBL_CHECK(std::memcmp(f.env.waiterPoint, f.table.m_WorldXYZ, sizeof(f.env.waiterPoint)) == 0);
        OBL_REQUIRE(f.env.animationMessages.size() == 1);
        OBL_CHECK(f.env.animationMessages[0].first == "serve");
        OBL_CHECK(f.env.animationMessages[0].second == 0);
        OBL_CHECK((f.env.calls == std::vector<std::string>{"waiter direction", "waiter position", "waiter hidden", "animation message"}));
        OBL_CHECK(f.env.posts.empty());
    }
}

OBL_TEST_CASE("The returning waiter traverses the route backwards and then goes home")
{
    for (int position : {1, 2, 3}) {
        ArrivalFixture f;
        f.table.m_WaiterPos = position;
        f.Receive();
        OBL_CHECK(f.table.m_WaiterPos == position - 1);
        OBL_REQUIRE(f.env.posts.size() == 1);
        OBL_CHECK(f.env.posts[0].flags == obl::FloatToIntWrap(100.5f + f.walk[position - 1][0]));
        OBL_CHECK(f.env.posts[0].data == obl::FloatToIntWrap(200.25f + f.walk[position - 1][1]));
    }
    ArrivalFixture f;
    f.table.m_waiterReady = 1;
    f.Receive();
    OBL_CHECK(f.table.m_WaiterPos == -1);
    OBL_CHECK(f.table.m_WaiterServing == -1);
    OBL_CHECK(f.table.m_waiterReady == 0);
    OBL_REQUIRE(f.env.posts.size() == 1);
    OBL_CHECK(f.env.posts[0].flags == 100);
    OBL_CHECK(f.env.posts[0].data == 200);
    OBL_CHECK(f.env.spawnBuilding == 99);
}

OBL_TEST_CASE("A pending chair receives a move before its second move home")
{
    ArrivalFixture f;
    f.chairs[1].m_waitingForWaiter = f.chairs[2].m_waitingForWaiter = 1;
    f.Receive();
    OBL_REQUIRE(f.env.posts.size() == 2);
    OBL_CHECK(f.env.posts[0].flags == 109); // First waiting chair (1), first route point (2).
    OBL_CHECK(f.env.posts[0].data == 206);
    OBL_CHECK(f.env.posts[1].flags == 100);
    OBL_CHECK(f.env.posts[1].data == 200);
    OBL_CHECK((f.env.calls == std::vector<std::string>{"waiter anim", "post", "post"}));
    OBL_CHECK(f.env.waiterAnim == "walk");
    OBL_CHECK(f.table.m_WaiterPos == 0);
    OBL_CHECK(f.table.m_WaiterServing == -1);
    OBL_CHECK(f.table.m_waiterReady == 0);
}

OBL_TEST_CASE("Arrival home requests the standing animation and makes the waiter ready")
{
    ArrivalFixture f;
    f.table.m_WaiterServing = -1;
    f.table.m_WaiterPos = 123;
    f.Receive();
    OBL_REQUIRE(f.env.animationMessages.size() == 1);
    OBL_CHECK(f.env.animationMessages[0].first == "d_stand_snooty");
    OBL_CHECK(f.env.animationMessages[0].second == 1);
    OBL_CHECK(f.table.m_waiterReady == 1);
    OBL_CHECK(f.table.m_WaiterPos == 123);
    OBL_CHECK(f.env.posts.empty());
}

OBL_TEST_CASE("A served guest's plate shows and the waiter walks off from the serve point")
{
    FakeEnv env;
    env.definition.m_hasPlates = 1;
    int walkLists[4][3] = {{2, 0, 1}, {1, 3, 0}, {2, 1, 2}, {1, 0, 0}};
    env.definition.m_chairWalkPoints = Lists(walkLists);
    float serve[4][2] = {{10.5f, 0}, {0, 0}, {30.75f, -8.25f}, {0, 0}};
    env.definition.m_servePoints = Points(serve);
    char walkAnim[] = "d_walk_snooty";
    env.definition.m_waiterWalkAnim = walkAnim;
    Made made(env);
    Furniture& t = made.object;
    t.m_WorldXYZ[0] = 1000.0f;
    t.m_WorldXYZ[1] = 2000.0f;
    t.m_rotation = 1;
    t.m_WaiterID = 700;
    t.m_WaiterServing = 2;
    t.m_drawWaiter = 1;
    t.m_chairs[2].m_waitingForWaiter = 1;
    t.m_chairs[2].m_ChairOccupier = 900;
    env.calls.clear();
    Message message{};
    message.type = 83;
    OBL_CHECK(table::ReceiveMessage(t, message, env) == std::optional<bool>(true));
    const std::vector<std::string> expected = {"waiter position", "waiter visible", "post", "waiter anim", "post delayed"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK(t.m_chairs[2].m_waitingForWaiter == 0 && t.m_pbDrawPlate[2] == 1 && t.m_pbDrawPlate[0] == 0);
    OBL_CHECK(t.m_WaiterPos == 1 && t.m_drawWaiter == 0);
    // Rotation 1: x + offset.y, y - offset.x.
    OBL_CHECK(env.movedWaiter == 700 && env.waiterPoint[0] == 991.75f && env.waiterPoint[1] == 1969.25f && env.waiterPoint[2] == 0.0f);
    OBL_CHECK(env.shownWaiter == 700 && env.animWaiter == 700 && env.waiterAnim == "d_walk_snooty");
    OBL_REQUIRE(env.posts.size() == 1 && env.delayed.size() == 1);
    const Message& move = env.posts[0];
    OBL_CHECK(move.sender == 77 && move.receiver == 700 && move.type == 10 && move.flags == 991 && move.data == 1969);
    const Message& finish = env.delayed[0].first;
    OBL_CHECK(finish.sender == 77 && finish.receiver == 900 && finish.type == 56 && finish.flags == 0 && finish.data == 0);
    OBL_CHECK(finish.extra == 0x2e && finish.floatData == 0.0f && env.delayed[0].second == 20.0f);
}

namespace {
// A table of two chairs whose guest 500 sits at chair 1 with a walk list of three points (position 4 = at the chair).
struct GuestFixture {
    FakeEnv env;
    Furniture table{};
    TableChair chairs[2]{};
    int routes[2][4] = {{3, 0, 1, 2}, {3, 2, 1, 0}};
    float walk[3][2] = {{-3.75f, 2.25f}, {10.75f, -4.5f}, {8.5f, 6.25f}};
    float seat[2][2] = {{1, 2}, {20.5f, -7.25f}};
    int dirs[2] = {1, 6};
    char sit[8] = "sit", getUp[8] = "getup", key[8] = "TABLE";
    FakeSprite chairBank{8};
    SpriteInstance chairSprites[2]{};
    SpriteInstance* chairPointers[2] = {&chairSprites[0], &chairSprites[1]};
    uint8_t plates[2] = {1, 1};
    GuestFixture()
    {
        env.definition.m_NumChairs = 2;
        env.definition.m_chairWalkPoints = Lists(routes);
        env.definition.m_walkPoints = Points(walk);
        env.definition.m_chairPositions = Points(seat);
        env.definition.m_chairDirections = Vec(dirs);
        env.definition.m_SitDownAnim = sit;
        env.definition.m_GetUpAnim = getUp;
        env.definition.m_key = key;
        table.m_definition = &env.definition;
        table.m_chairs = chairs;
        table.m_pbDrawPlate = plates;
        table.m_id = 77;
        table.m_ownerId = 99;
        table.m_WorldXYZ[0] = 100.5f;
        table.m_WorldXYZ[1] = 200.25f;
        chairs[0].m_ChairOccupier = -1;
        chairs[1].m_ChairOccupier = 500;
    }
    void Receive(int32_t sender = 500)
    {
        Message message{};
        message.type = obl::messages::MT_NEXT_MOVE_POINT;
        message.sender = sender;
        OBL_CHECK(table::ReceiveMessage(table, message, env) == std::optional<bool>(true));
    }
};
} // namespace

OBL_TEST_CASE("A walking guest is sent to the next point of its chair's list, rotated with the table")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        GuestFixture f;
        f.table.m_rotation = rotation;
        f.chairs[1].m_position = 1; // the list of chair 1 holds point 2 at index 1
        float point[2];
        OBL_REQUIRE(table::AddRotatedOffset(point, f.table.m_WorldXYZ, f.walk[2], rotation));
        const int32_t expected[2] = {obl::FloatToIntWrap(point[0]), obl::FloatToIntWrap(point[1])};
        f.Receive();
        OBL_REQUIRE(f.env.posts.size() == 1);
        const Message& move = f.env.posts[0];
        OBL_CHECK(move.sender == 77 && move.receiver == 500 && move.type == obl::messages::MT_MOVE2SEAPOINT);
        OBL_CHECK(move.flags == expected[0] && move.data == expected[1]);
        OBL_CHECK(move.extra == 0x2e && move.floatData == 0.0f);
        OBL_CHECK(f.chairs[1].m_position == 2);
        OBL_CHECK(f.env.calls == std::vector<std::string>{"post"});
    }
}

OBL_TEST_CASE("A returning guest walks the list backwards and leaves the chair at its first point")
{
    GuestFixture f;
    f.env.guestState = 4;
    f.chairs[1].m_position = 3;
    f.Receive();
    OBL_CHECK(f.chairs[1].m_position == 2);
    OBL_REQUIRE(f.env.posts.size() == 1);
    // The point before position 3 is entry 2 of the list, walk point 1.
    OBL_CHECK(f.env.posts[0].flags == obl::FloatToIntWrap(100.5f + f.walk[1][0]));
    OBL_CHECK(f.env.posts[0].data == obl::FloatToIntWrap(200.25f + f.walk[1][1]));
    OBL_CHECK(f.env.calls == std::vector<std::string>{"post"});

    GuestFixture g;
    g.env.guestState = 4;
    g.chairs[1].m_position = 1;
    g.chairs[1].m_ChairSeated = 1;
    g.Receive();
    OBL_CHECK(g.chairs[1].m_ChairOccupier == -1 && g.chairs[1].m_ChairSeated == 0 && g.chairs[1].m_position == 0);
    OBL_CHECK(g.table.m_GuestsRemoved == 1);
    OBL_CHECK(g.env.shownGuest == 500);
    OBL_REQUIRE(g.env.posts.size() == 2);
    OBL_CHECK(g.env.posts[0].type == obl::messages::MT_COMMOD_QUALITY && g.env.posts[0].receiver == 99 && g.env.posts[0].data == 0);
    OBL_CHECK(g.env.posts[1].type == obl::messages::MT_STATECHANGE && g.env.posts[1].receiver == 500 && g.env.posts[1].data == 0x1c);
    OBL_REQUIRE(g.env.actionStates.size() == 1);
    OBL_CHECK(g.env.actionStates[0] == std::make_pair(500, 0));
    OBL_CHECK((g.env.calls == std::vector<std::string>{"guest visible", "post", "action state", "post"}));

    GuestFixture h; // a serve table does not tell the building
    h.table.m_serveTable = 1;
    h.env.guestState = 4;
    h.chairs[1].m_position = 0;
    h.Receive();
    OBL_CHECK((h.env.calls == std::vector<std::string>{"guest visible", "action state", "post"}));
}

OBL_TEST_CASE("A guest at the chair sits down: position, direction, the sit animation, the chair's state and sprite")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        GuestFixture f;
        f.env.definition.m_hasPlates = 1;
        f.table.m_rotation = rotation;
        f.table.m_chairSprites = f.chairPointers;
        f.chairs[1].m_position = 4;
        f.Receive();
        OBL_CHECK(f.env.movedGuest == 500);
        // Rotation r: the chair's offset turned like the waiter's points.
        const float x = 100.5f, y = 200.25f, ox = 20.5f, oy = -7.25f;
        const float point[4][2] = {{x + ox, y + oy}, {x + oy, y - ox}, {x - ox, y - oy}, {x - oy, y + ox}};
        OBL_CHECK(f.env.guestPoint[0] == point[rotation][0] && f.env.guestPoint[1] == point[rotation][1] && f.env.guestPoint[2] == 0.0f);
        OBL_CHECK(f.env.guestDirection == (6 + (4 - rotation) * 2) % 8);
        OBL_REQUIRE(f.env.guestAnimations.size() == 1);
        OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string("sit"), 500));
        OBL_CHECK(f.chairs[1].m_ChairSeated == 1 && f.chairs[1].m_waitingForWaiter == 1 && f.chairs[1].m_position == 4);
        OBL_REQUIRE(f.env.chairAnims.size() == 1);
        OBL_CHECK(f.env.chairAnims[0] == std::make_pair(1u, std::string("chair_stand_sit")));
        OBL_CHECK(f.env.chairAnimTypes[0] == 2);
        OBL_CHECK((f.env.calls == std::vector<std::string>{"guest position", "guest direction", "guest animation", "chair anim"}));
        OBL_CHECK(f.env.posts.empty() && f.env.delayed.empty());
    }
}

OBL_TEST_CASE("A table without plates finishes the guest's action 30 seconds after the guest sits down; a serve table boards it instead")
{
    GuestFixture f; // no plates, no chair sprites
    f.chairs[1].m_position = 4;
    f.Receive();
    OBL_REQUIRE(f.env.delayed.size() == 1);
    const Message& finish = f.env.delayed[0].first;
    OBL_CHECK(finish.sender == 77 && finish.receiver == 500 && finish.type == obl::messages::MT_FINISH_ACTION && finish.extra == 0x2e);
    OBL_CHECK(f.env.delayed[0].second == 30.0f);
    OBL_CHECK(f.env.chairAnims.empty());
    OBL_CHECK((f.env.calls == std::vector<std::string>{"post delayed", "guest position", "guest direction", "guest animation"}));

    GuestFixture g;
    g.env.definition.m_serveTable = 1;
    g.chairs[1].m_position = 4;
    g.Receive();
    OBL_REQUIRE(g.env.posts.size() == 1);
    OBL_CHECK(g.env.posts[0].type == obl::messages::MT_BOARD_VEHICLE && g.env.posts[0].receiver == 99 && g.env.posts[0].data == 500);
    OBL_CHECK(g.env.calls == std::vector<std::string>{"post"});
    OBL_CHECK(g.chairs[1].m_ChairSeated == 0);
}

OBL_TEST_CASE("A guest already sitting at the chair gets up: the plate goes and the get-up animation is requested")
{
    GuestFixture f;
    f.env.definition.m_hasPlates = 1;
    f.env.guestState = 2;
    f.chairs[1].m_position = 4;
    f.Receive();
    OBL_CHECK(f.plates[1] == 0 && f.plates[0] == 1);
    OBL_REQUIRE(f.env.guestAnimations.size() == 1);
    OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string("getup"), 500));
    OBL_CHECK(f.env.calls == std::vector<std::string>{"guest animation"});

    GuestFixture g;
    g.env.definition.m_serveTable = 1;
    g.env.guestState = 2;
    g.chairs[1].m_position = 4;
    g.Receive();
    OBL_CHECK(g.env.calls.empty());
    OBL_CHECK(g.plates[1] == 1);
}

OBL_TEST_CASE("The fish boat's guests are placed from the building's position by fixed offsets")
{
    const float offsets[4][2] = {{263, -408}, {-148, -522}, {-107, -265}, {-5, -154}};
    for (int rotation = 0; rotation < 4; ++rotation) {
        GuestFixture f;
        f.env.definition.m_key = "FISHBOAT1";
        f.table.m_rotation = rotation;
        f.chairs[1].m_position = 4;
        f.Receive();
        OBL_CHECK(f.env.askedBuilding == 99);
        OBL_CHECK(f.env.guestPoint[0] == 1000.5f + offsets[rotation][0] && f.env.guestPoint[1] == 2000.25f + offsets[rotation][1]);
    }
    GuestFixture g;
    g.env.definition.m_key = "FISHBOAT1";
    g.chairs[1].m_position = 1;
    g.table.m_rotation = 2;
    g.Receive();
    OBL_REQUIRE(g.env.posts.size() == 1);
    OBL_CHECK(g.env.posts[0].flags == obl::FloatToIntWrap(1000.5f - 107) && g.env.posts[0].data == obl::FloatToIntWrap(2000.25f - 265));
}

OBL_TEST_CASE("A guest on two chairs is handled for each in turn, with its action state read again")
{
    GuestFixture f;
    f.chairs[0].m_ChairOccupier = 500;
    f.env.guestState = 4;
    f.chairs[0].m_position = 1;
    f.chairs[1].m_position = 1;
    f.Receive();
    // Chair 0: leaves (clears itself, the action state drops to 0). Chair 1 then sees state 0 and walks to its next point instead.
    OBL_CHECK(f.chairs[0].m_ChairOccupier == -1 && f.chairs[0].m_position == 0);
    OBL_CHECK(f.chairs[1].m_ChairOccupier == 500 && f.chairs[1].m_position == 2);
    OBL_CHECK(f.env.posts.size() == 3);
    OBL_CHECK(f.env.posts[2].type == obl::messages::MT_MOVE2SEAPOINT);

    GuestFixture g; // another sender sits nowhere
    g.Receive(501);
    OBL_CHECK(g.env.calls.empty());
}

namespace {
// GuestFixture plus what the idle and waiter branches read; guest 500 sits at chair 1 (list {3, 2, 1, 0}), position 4 = at the chair.
struct ActionFixture : GuestFixture {
    char sat[2][8] = {"sat0", "sat1"};
    char call[8] = "call", wait[8] = "wait", walkAnim[8] = "wwalk";
    ActionFixture()
    {
        env.definition.m_satDownAnims = {sat[0], sat[1]};
        env.definition.m_NumSatDownAnims = 2;
        env.definition.m_callWaiterAnim = call;
        env.definition.m_waitWaiterAnim = wait;
        env.definition.m_waiterWalkAnim = walkAnim;
        table.m_WaiterID = 88;
        table.m_WaiterServing = -1;
        table.m_waiterReady = 1;
        chairs[1].m_position = 4;
    }
    void Act(int32_t sender = 500)
    {
        Message message{};
        message.type = obl::messages::MT_DO_NEXT_ACTION;
        message.sender = sender;
        OBL_CHECK(table::ReceiveMessage(table, message, env));
    }
    // A walk point turned by the table's rotation.
    void Turned(const float offset[2], float out[2])
    {
        OBL_REQUIRE(table::AddRotatedOffset(out, table.m_WorldXYZ, offset, table.m_rotation));
    }
};
} // namespace

OBL_TEST_CASE("A guest done eating gets up: the plate goes, the guest turns, the get-up animation and the chair's own run")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        ActionFixture f;
        f.env.definition.m_hasPlates = 1;
        f.table.m_rotation = rotation;
        f.table.m_chairSprites = f.chairPointers;
        f.env.guestState = 2;
        f.Act();
        OBL_CHECK(f.plates[1] == 0 && f.plates[0] == 1);
        OBL_CHECK(f.env.guestDirection == (6 + (4 - rotation) * 2) % 8);
        OBL_REQUIRE(f.env.guestAnimations.size() == 1);
        OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string("getup"), 500));
        OBL_REQUIRE(f.env.chairAnims.size() == 1);
        OBL_CHECK(f.env.chairAnims[0] == std::make_pair(1u, std::string("chair_sit_stand")));
        OBL_CHECK(f.env.chairAnimTypes[0] == 2);
        OBL_CHECK((f.env.calls == std::vector<std::string>{"guest direction", "guest animation", "chair anim"}));
    }
    ActionFixture g; // a serve table only moves the guest on to state 3
    g.env.definition.m_serveTable = 1;
    g.env.guestState = 2;
    g.Act();
    OBL_CHECK(g.env.guestState == 3);
    OBL_CHECK((g.env.calls == std::vector<std::string>{"action state"}));
}

OBL_TEST_CASE("A guest leaving its chair is put at the walk list's point, sent the walk text and asks the table for the next move")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        ActionFixture f;
        f.table.m_rotation = rotation;
        f.table.m_chairSprites = f.chairPointers;
        f.chairs[1].m_ChairSeated = 1;
        f.env.guestState = 3;
        f.Act();
        float point[2];
        f.Turned(f.walk[0], point); // position 4 takes entry 3 of the list {3, 2, 1, 0}: walk point 0
        OBL_CHECK(f.chairs[1].m_position == 3 && f.chairs[1].m_ChairSeated == 0);
        OBL_CHECK(f.env.movedGuest == 500);
        OBL_CHECK(f.env.guestPoint[0] == point[0] && f.env.guestPoint[1] == point[1] && f.env.guestPoint[2] == 0.0f);
        OBL_REQUIRE(f.env.guestAnimations.size() == 1);
        OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string("WalkAnimation"), 500));
        OBL_REQUIRE(f.env.chairAnims.size() == 1);
        OBL_CHECK(f.env.chairAnims[0] == std::make_pair(1u, std::string("chair")));
        OBL_CHECK(f.env.chairAnimTypes[0] == 0);
        OBL_REQUIRE(f.env.posts.size() == 1);
        const Message& next = f.env.posts[0];
        OBL_CHECK(next.sender == 500 && next.receiver == 77 && next.type == obl::messages::MT_NEXT_MOVE_POINT);
        OBL_CHECK(next.flags == 0 && next.data == 0 && next.extra == 0x2e && next.floatData == 0.0f);
        OBL_CHECK((f.env.calls == std::vector<std::string>{"guest position", "guest animation", "chair anim", "post"}));
    }
    ActionFixture h; // the fish boat takes the point from the building
    h.env.definition.m_key = "FISHBOAT1";
    h.table.m_rotation = 3;
    h.env.guestState = 3;
    h.Act();
    OBL_CHECK(h.env.askedBuilding == 99);
    OBL_CHECK(h.env.guestPoint[0] == 1000.5f - 5 && h.env.guestPoint[1] == 2000.25f - 154);
}

OBL_TEST_CASE("State 4, serve tables and other chairs' guests are left alone")
{
    ActionFixture f;
    f.env.guestState = 4;
    f.Act();
    OBL_CHECK(f.env.calls.empty());
    ActionFixture g;
    g.env.definition.m_serveTable = 1;
    g.env.guestState = 0;
    g.Act();
    OBL_CHECK(g.env.calls.empty());
    ActionFixture h; // another sender sits nowhere
    h.env.guestState = 3;
    h.Act(501);
    OBL_CHECK(h.env.calls.empty());
}

OBL_TEST_CASE("An idle guest plays one of the sit-down animations drawn by rand()")
{
    for (int pick = 0; pick < 4; ++pick) {
        ActionFixture f;
        f.env.randomValue = pick;
        f.table.m_chairSprites = f.chairPointers;
        f.Act();
        OBL_REQUIRE(f.env.guestAnimations.size() == 1);
        OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string(pick % 2 ? "sat1" : "sat0"), 500));
        OBL_CHECK(f.env.guestDirection == (6 + 4 * 2) % 8);
        OBL_REQUIRE(f.env.chairAnims.size() == 1);
        OBL_CHECK(f.env.chairAnims[0] == std::make_pair(1u, std::string("chair_eat")));
        OBL_CHECK(f.env.chairAnimTypes[0] == 0);
        OBL_CHECK((f.env.calls == std::vector<std::string>{"rand", "guest direction", "guest animation", "chair anim"}));
    }
    ActionFixture g; // plates but the chair does not wait: the same
    g.env.definition.m_hasPlates = 1;
    g.env.randomValue = 1;
    g.Act();
    OBL_REQUIRE(g.env.guestAnimations.size() == 1);
    OBL_CHECK(g.env.guestAnimations[0].first == "sat1");
}

OBL_TEST_CASE("A waiting guest calls the idle waiter, or waits for a busy one")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        ActionFixture f;
        f.env.definition.m_hasPlates = 1;
        f.table.m_rotation = rotation;
        f.table.m_chairSprites = f.chairPointers;
        f.chairs[1].m_waitingForWaiter = 1;
        f.Act();
        OBL_CHECK(f.table.m_WaiterServing == 1 && f.table.m_WaiterPos == 0);
        OBL_REQUIRE(f.env.guestAnimations.size() == 1);
        OBL_CHECK(f.env.guestAnimations[0] == std::make_pair(std::string("call"), 500));
        OBL_CHECK(f.env.waiterAnim == "wwalk");
        float point[2];
        f.Turned(f.walk[2], point); // entry 1 of the list {3, 2, 1, 0}
        OBL_REQUIRE(f.env.posts.size() == 1);
        const Message& move = f.env.posts[0];
        OBL_CHECK(move.type == obl::messages::MT_MOVE2SEAPOINT && move.sender == 77 && move.receiver == 88);
        OBL_CHECK(move.flags == obl::FloatToIntWrap(point[0]) && move.data == obl::FloatToIntWrap(point[1]));
        OBL_REQUIRE(f.env.chairAnims.size() == 1);
        OBL_CHECK(f.env.chairAnims[0] == std::make_pair(1u, std::string("chair_eat")));
        OBL_CHECK((f.env.calls == std::vector<std::string>{"guest direction", "guest animation", "waiter anim", "post", "chair anim"}));
    }
    ActionFixture busy;
    busy.env.definition.m_hasPlates = 1;
    busy.chairs[1].m_waitingForWaiter = 1;
    busy.table.m_WaiterServing = 0;
    busy.Act();
    OBL_REQUIRE(busy.env.guestAnimations.size() == 1);
    OBL_CHECK(busy.env.guestAnimations[0] == std::make_pair(std::string("wait"), 500));
    OBL_CHECK(busy.table.m_WaiterServing == 0 && busy.env.posts.empty());
    ActionFixture notReady;
    notReady.env.definition.m_hasPlates = 1;
    notReady.chairs[1].m_waitingForWaiter = 1;
    notReady.table.m_waiterReady = 0;
    notReady.Act();
    OBL_REQUIRE(notReady.env.guestAnimations.size() == 1);
    OBL_CHECK(notReady.env.guestAnimations[0].first == "wait");
}

OBL_TEST_CASE("A guest on two chairs is acted on for each of them")
{
    ActionFixture f;
    f.chairs[0].m_ChairOccupier = 500;
    f.chairs[0].m_position = 4;
    f.env.guestState = 3;
    f.Act();
    OBL_CHECK(f.chairs[0].m_position == 3 && f.chairs[1].m_position == 3);
    OBL_CHECK(f.env.posts.size() == 2);
}

namespace {
// Checks every field table entry's obl.save key against the converter's bridge table: each is one the converter maps an old
// label onto.
struct BridgeKeys {
    int checked = 0;
    static bool Known(const std::string& key)
    {
        return obl::convert::OldObjectFieldKeyKnown(static_cast<uint8_t>(obl::object::ObjectType::Table), key);
    }
    void Check(const char* key)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
    }
    template<class T> void Bool(const char* key, T&, obl::object::Hashed) { Check(key); }
    template<class T> void Int(const char* key, T&, obl::object::Hashed) { Check(key); }
    template<class T> void UInt(const char* key, T&, obl::object::Hashed) { Check(key); }
    void Sprite(const char* key, SpriteInstance*)
    {
        OBL_CHECK(Known(std::string(key) + ".dir"));
        ++checked;
    }
};
} // namespace

OBL_TEST_CASE("table.field_keys_match_the_save_bridge")
{
    TableDef definition{};
    definition.m_NumChairs = 1;
    definition.m_hasPlates = 1;
    TableChair chair{};
    SpriteInstance* sprites[1] = {nullptr};
    uint8_t drawPlate[1] = {0};
    Furniture table{};
    table.m_definition = &definition;
    table.m_chairs = &chair;
    table.m_chairSprites = sprites;
    table.m_plateSprites = sprites;
    table.m_pbDrawPlate = drawPlate;
    BridgeKeys keys;
    obl::object::VisitFields(table, keys);
    OBL_CHECK_EQ(keys.checked, 18);
}
