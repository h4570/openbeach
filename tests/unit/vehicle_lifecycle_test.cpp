#include "unit_test.h"
#include "game/objects/vehicle_lifecycle.h"
#include "game/ui/sprite.h"
#include "object_storage.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
using obl::map::GridCell;
using obl::object::Vehicle;
using obl::tables::VehicleDef;
using obl::ui::SpriteInstance;
namespace life = obl::vehicle::lifecycle;

// Records every call in order; sprites are small heap-free stand-ins.
struct FakeEnv {
    std::vector<std::string> calls;
    VehicleDef definition{};
    obl::ui::Sprite bankSprite{};
    SpriteInstance instances[16]{};
    int created = 0;
    float time = 12.5f;
    GridCell cells[3][3][3]{};
    GridCell around[8]{};
    GridCell own{};
    uint32_t tile[2] = {7, 9};
    struct Anim {
        void* sprite;
        std::string name;
        float start;
        const float* world;
    };
    std::vector<Anim> anims;
    int paletteSet = -1;
    std::string towedKey;
    int32_t towedOwner = 0;
    std::vector<int32_t> loops;
    std::vector<void*> released;
    std::vector<uint32_t> neighbourAsked;

    FakeEnv()
    {
        bankSprite.m_paletteCount = 60;
        definition.m_Towed = "NONE";
    }

    // What Mover's constructor (the base object's, in the game) leaves before Vehicle's runs.
    void BaseConstruct(Vehicle& o, const char* name)
    {
        calls.push_back(std::string("base ") + name);
        o.m_id = 77;
    }
    const VehicleDef* Definition(const char*)
    {
        calls.push_back("definition");
        return &definition;
    }
    void PlaceTowed(int32_t owner, const char* key)
    {
        calls.push_back("towed");
        towedKey = key;
        towedOwner = owner;
    }
    void* CreateSprite(const char* name)
    {
        calls.push_back(std::string("sprite ") + name);
        SpriteInstance* s = &instances[created++];
        s->m_sprite = &bankSprite;
        return s;
    }
    void StartAnim(void* sprite, const char* anim, float start, const float* world) { anims.push_back({sprite, anim, start, world}); }
    float WorldTime() { return time; }
    int32_t PaletteCount(void* sprite) { return static_cast<SpriteInstance*>(sprite)->m_sprite->m_paletteCount; }
    void SetPaletteIndex(void*, int32_t index) { paletteSet = index; }

    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8])
    {
        calls.push_back("neighbours");
        neighbourAsked = {x, y};
        for (int i = 0; i < 8; ++i) out[i] = &around[i];
    }
    GridCell* CellAt(uint32_t, uint32_t) { return &own; }
    void ObjectTile(const Vehicle&, uint32_t out[2])
    {
        out[0] = tile[0];
        out[1] = tile[1];
    }
    void StopLoop(int32_t id)
    {
        calls.push_back("loop");
        loops.push_back(id);
    }
    void ReleaseSprite(void* sprite)
    {
        calls.push_back("release");
        released.push_back(sprite);
    }
};

// Mover's constructor, then Vehicle's.
void Build(Vehicle& object, const char* name, FakeEnv& env)
{
    env.BaseConstruct(object, name);
    life::Construct(object, name, env);
}

void Made(Vehicle& object, FakeEnv& env, const char* name, uint8_t fill = 0xa5)
{
    obl::unit::FillObject(object, fill);
    Build(object, name, env);
}
} // namespace

OBL_TEST_CASE("Vehicle construct picks the type of the key and the tolerance that type sets")
{
    struct Row {
        const char* key;
        int32_t type;
        float tolerance;
    };
    // Vehicle keys match in any case.
    const Row rows[] = {{"PEDALOE", 4, 0}, {"pedaloe", 4, 0}, {"JETSKIVEHICLE", 0, 10}, {"jetskivehicle", 0, 10}, {"BANANA", 3, 0},
            {"banana", 3, 0}, {"SPDBOAT", 1, 12}, {"SpdBoat", 1, 12}, {"BOAT", 2, 12}, {"boat", 2, 12}, {"BUS", 5, 8}, {"bus", 5, 8},
            {"FISHBOAT", 6, 0}, {"FishBoat", 6, 0}};
    for (const Row& row : rows) {
        FakeEnv env;
        Vehicle object{};
        object.m_targetTolerance = 99.0f;
        Build(object, row.key, env);
        OBL_CHECK(object.m_vehicleType == row.type);
        if (row.tolerance)
            OBL_CHECK(object.m_targetTolerance == row.tolerance);
        else
            OBL_CHECK(object.m_targetTolerance == 99.0f);
    }
}

OBL_TEST_CASE("Vehicle construct leaves the type alone for a key it does not know")
{
    for (const char* key : {"UNLISTED", ""}) {
        FakeEnv env;
        Vehicle object;
        obl::unit::FillObject(object, 0xa5);
        Build(object, key, env);
        OBL_CHECK(object.m_vehicleType == static_cast<int32_t>(0xa5a5a5a5));
        OBL_CHECK(env.created == 0);
        OBL_CHECK(object.m_vehicleSprite == nullptr);
    }
}

OBL_TEST_CASE("Vehicle construct sets the type specific fields and the zeroed state")
{
    FakeEnv env;
    Vehicle object;
    Made(object, env, "BUS");
    OBL_CHECK(object.m_unloadDir == 5);
    OBL_CHECK(object.m_controlDirection == 0);
    OBL_CHECK(object.m_DestroyPoint == 0xff);
    OBL_CHECK(object.m_CurrGridXY[0] == 0xffffffffu && object.m_CurrGridXY[1] == 0xffffffffu);
    OBL_CHECK(object.m_numWayPts == 0 && object.m_currentWayPoint == 0 && object.m_currCap == 0 && object.m_guestsToWaitFor == 0);
    OBL_CHECK(object.m_circuitPoints == nullptr && object.m_circuitPointCount == 0);
    OBL_CHECK(object.m_gotGrid == 0 && object.m_fishing == 0 && object.m_moveToTurnPoint == 0 && object.m_waitForGuestToBoard == 0);
    // The fields it never writes keep the allocation's contents.
    OBL_CHECK(object.m_ownerId == static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK(object.m_UnloadPoint == 0xa5);
    OBL_CHECK(object.m_playingDoorAnim == 0xa5);
    OBL_CHECK(object.m_Guests[299] == static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK(object.m_waitingForDeath == 0);
}

OBL_TEST_CASE("Vehicle construct takes the physics from the definition as raw bits")
{
    FakeEnv env;
    const uint32_t bits[3] = {0x7f812345u, 0x80000000u, 0x42f68000u};
    std::memcpy(&env.definition.m_Force, &bits[0], 4);
    std::memcpy(&env.definition.m_MaxSpeed, &bits[1], 4);
    std::memcpy(&env.definition.m_Mass, &bits[2], 4);
    Vehicle object;
    Made(object, env, "PEDALOE");
    OBL_CHECK(std::memcmp(&object.m_maxForce, &bits[0], 4) == 0);
    OBL_CHECK(std::memcmp(&object.m_maxVel, &bits[1], 4) == 0);
    OBL_CHECK(std::memcmp(&object.m_mass, &bits[2], 4) == 0);
}

OBL_TEST_CASE("Vehicle construct creates the sprites of the type only")
{
    {
        FakeEnv env;
        Vehicle object;
        Made(object, env, "JETSKIVEHICLE");
        OBL_CHECK(env.created == 1 && object.m_vehicleSprite == &env.instances[0]);
        OBL_CHECK(env.anims.size() == 1 && env.anims[0].name == "d_wake_normal" && env.anims[0].start == 0.0f &&
                env.anims[0].world == object.m_WorldXYZ);
        OBL_CHECK(object.m_auxSprite1 == nullptr && object.m_auxSprite2 == nullptr && object.m_auxSprite3 == nullptr &&
                object.m_doorSprite == nullptr);
    }
    {
        FakeEnv env;
        Vehicle object;
        Made(object, env, "FISHBOAT");
        OBL_CHECK(env.created == 3 && object.m_vehicleSprite == nullptr);
        OBL_CHECK(object.m_auxSprite1 == &env.instances[0] && object.m_auxSprite2 == &env.instances[1] &&
                object.m_auxSprite3 == &env.instances[2]);
        OBL_CHECK(env.calls[2] == "sprite fishboat" && env.calls[3] == "sprite fishboat" && env.calls[4] == "sprite fishman");
        OBL_CHECK(env.anims.size() == 3 && env.anims[0].name == "rod01_bob" && env.anims[1].name == "rod02_bob" &&
                env.anims[2].name == "fish_driver");
        OBL_CHECK(env.paletteSet == 0x2a);
        env.bankSprite.m_paletteCount = 5;
        Build(object, "FISHBOAT", env);
        OBL_CHECK(env.paletteSet == 4);
        env.bankSprite.m_paletteCount = 0x2b;
        Build(object, "FISHBOAT", env);
        OBL_CHECK(env.paletteSet == 0x2a);
    }
    {
        FakeEnv env;
        Vehicle object;
        Made(object, env, "BUS");
        OBL_CHECK(env.created == 1 && object.m_doorSprite == &env.instances[0]);
        OBL_CHECK(env.anims[0].name == "coach_doors_closed" && env.anims[0].start == 12.5f);
        OBL_CHECK(object.m_vehicleSprite == nullptr && object.m_auxSprite1 == nullptr);
    }
    for (const char* key : {"PEDALOE", "BOAT", "SPDBOAT", "BANANA"}) {
        FakeEnv env;
        Vehicle object;
        Made(object, env, key);
        OBL_CHECK(
                env.created == 0 && object.m_vehicleSprite == nullptr && object.m_doorSprite == nullptr && object.m_auxSprite1 == nullptr);
    }
}

OBL_TEST_CASE("Vehicle construct posts the towed vehicle between the route counters and the sprites")
{
    FakeEnv env;
    Vehicle object;
    Made(object, env, "JETSKIVEHICLE");
    OBL_CHECK(object.m_vehicleType == 0);
    OBL_CHECK(env.towedKey.empty());
    env.definition.m_Towed = "BANANA";
    env.calls.clear();
    Made(object, env, "JETSKIVEHICLE");
    OBL_CHECK(env.towedKey == "BANANA" && env.towedOwner == 77);
    OBL_CHECK(env.calls.size() == 4 && env.calls[0] == "base JETSKIVEHICLE" && env.calls[1] == "definition" && env.calls[2] == "towed" &&
            env.calls[3] == "sprite jetski_sprite");
    // NONE in any case tows nothing; anything else is a towed key.
    env.definition.m_Towed = "none";
    env.towedKey.clear();
    Made(object, env, "PEDALOE");
    OBL_CHECK(env.towedKey.empty());
    env.definition.m_Towed = "NONES";
    Made(object, env, "PEDALOE");
    OBL_CHECK(env.towedKey == "NONES");
}

OBL_TEST_CASE("Vehicle destruct of a plain vehicle releases only its sprites")
{
    FakeEnv env;
    Vehicle object;
    Made(object, env, "PEDALOE");
    env.calls.clear();
    life::Destruct(object, env);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("Vehicle destruct of a fishing boat frees the eight cells around its tile")
{
    FakeEnv env;
    Vehicle object;
    Made(object, env, "FISHBOAT");
    object.m_CurrGridXY[0] = 3;
    object.m_CurrGridXY[1] = 4;
    for (GridCell& cell : env.around) cell.m_flags = 0xffffffffu;
    env.own.m_flags = 0xffffffffu;
    life::Destruct(object, env);
    OBL_CHECK(env.neighbourAsked == (std::vector<uint32_t>{3, 4}));
    for (const GridCell& cell : env.around) OBL_CHECK(cell.m_flags == 0xffffffefu);
    OBL_CHECK(env.own.m_flags == 0xffffffffu);
    OBL_CHECK(env.loops.empty());
    OBL_CHECK(env.released.size() == 3);
}

OBL_TEST_CASE("Vehicle destruct of a jetski or speed boat frees its tile and the neighbours of its unload direction")
{
    for (const char* key : {"JETSKIVEHICLE", "SPDBOAT"}) {
        for (uint32_t direction = 0; direction < 10; ++direction) {
            FakeEnv env;
            Vehicle object;
            Made(object, env, key);
            object.m_unloadDir = direction;
            for (GridCell& cell : env.around) cell.m_flags = 0x1f;
            env.own.m_flags = 0x1f;
            life::Destruct(object, env);
            OBL_CHECK(env.neighbourAsked == (std::vector<uint32_t>{7, 9}));
            OBL_CHECK(env.own.m_flags == 0xf);
            for (int i = 0; i < 8; ++i) {
                const bool freed = (direction == 1 || direction == 5) ? (i == 0 || i == 4)
                        : (direction == 3 || direction == 7)          ? (i == 2 || i == 6)
                                                                      : false;
                OBL_CHECK(env.around[i].m_flags == (freed ? 0xfu : 0x1fu));
            }
            OBL_CHECK(env.loops.size() == 1 && env.loops[0] == 77);
        }
    }
}

OBL_TEST_CASE("Vehicle destruct stops the loop sound of types 0, 1 and 5 only")
{
    for (const char* key : {"JETSKIVEHICLE", "SPDBOAT", "BUS", "PEDALOE", "BOAT", "BANANA", "FISHBOAT"}) {
        FakeEnv env;
        Vehicle object;
        Made(object, env, key);
        life::Destruct(object, env);
        const bool loops = std::strcmp(key, "JETSKIVEHICLE") == 0 || std::strcmp(key, "SPDBOAT") == 0 || std::strcmp(key, "BUS") == 0;
        OBL_CHECK(env.loops.size() == (loops ? 1u : 0u));
    }
}

OBL_TEST_CASE("Vehicle destruct releases the sprites in the fixed order")
{
    FakeEnv env;
    Vehicle object{};
    void* const a = &env.instances[0];
    void* const b = &env.instances[1];
    void* const c = &env.instances[2];
    void* const d = &env.instances[3];
    void* const e = &env.instances[4];
    object.m_vehicleType = 4;
    object.m_vehicleSprite = a;
    object.m_doorSprite = b;
    object.m_auxSprite1 = c;
    object.m_auxSprite2 = d;
    object.m_auxSprite3 = e;
    life::Destruct(object, env);
    OBL_CHECK(env.released == (std::vector<void*>{a, b, c, d, e}));
    // An empty slot is skipped.
    FakeEnv none;
    object.m_vehicleSprite = nullptr;
    object.m_auxSprite2 = nullptr;
    life::Destruct(object, none);
    OBL_CHECK(none.released.size() == 3);
}
