#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/guest_script_values.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using obl::object::Building;
using obl::object::Guest;
using obl::object::Scenery;
using obl::script::ScriptValue;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
using namespace obl::tables;
namespace gs = obl::guest_script;

namespace {

float Bits(int32_t bits)
{
    return script::BitsToFloat(bits);
}
int32_t FloatBits(float value)
{
    return script::FloatToBits(value);
}

// A guest with everything the variables dereference: a message, a definition, a sprite with one animation of three directions, and the building the
// guest is at (one commodity slot, a sprite name).
struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();
    std::unique_ptr<Building> building = std::make_unique<Building>();
    std::unique_ptr<BuildingDef> buildingDef = std::make_unique<BuildingDef>();
    std::unique_ptr<Scenery> other = std::make_unique<Scenery>();
    Message message{};
    SpriteInstance sprite{};
    SpriteInstance attached{};
    SpriteInstance otherSprite{};
    Sprite spriteData{};
    SpriteAnim anim{};
    SpriteAnim* anims[1] = {&anim};
    char otherName[16] = "SHAZ";
    char attachedName[16] = "BALL";

    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        obl::unit::FillObject(*building);
        obl::unit::FillObject<Scenery>(*other);
        anim.m_directions = 3;
        spriteData.m_anims = anims;
        sprite.m_sprite = &spriteData;
        attached.m_sprite = &spriteData;
        sprite.m_direction = 2;
        sprite.m_name = const_cast<char*>("GUEST");
        attached.m_name = attachedName;
        otherSprite.m_name = otherName;
        guest->m_definition = def.get();
        guest->m_currentMessage = &message;
        guest->m_sprite = &sprite;
        guest->m_DestObjectID = 7;
        guest->m_desired_commod = 2;
        guest->m_id = 99;
        building->m_id = 7;
        building->m_definition = buildingDef.get();
        building->m_sprite = &otherSprite;
        building->m_events = reinterpret_cast<BuildingEvents*>(0x1234);
        buildingDef->m_Commodity[2] = 11;
        other->m_sprite = &otherSprite;
        other->m_id = 5;
    }
};

// A fake of the Env of guest_script_values.h that records what it was asked.
struct FakeEnv {
    Fixture& f;
    std::vector<std::string> log;
    std::vector<int32_t> baseKeys;
    int nextRand = 0;
    float seaAt[3] = {};
    uint32_t cellFlags = 0;
    bool usable = true;
    bool clear = true;
    object::Base* wharf[1] = {nullptr};
    object::Base* bus[1] = {nullptr};
    object::Base* hotel[1] = {nullptr};
    object::Base* hut[1] = {nullptr};

    explicit FakeEnv(Fixture& fixture) : f(fixture)
    {
        wharf[0] = f.other.get();
        bus[0] = f.other.get();
        hotel[0] = f.other.get();
        hut[0] = f.other.get();
    }

    object::Base* Object(int32_t id)
    {
        log.push_back("object " + std::to_string(id));
        if (id == 7) return f.building.get();
        if (id == 5) return f.other.get();
        return nullptr;
    }
    object::Base* const* FindBuildings(BuildingBehavior behavior, uint32_t& count)
    {
        log.push_back(std::string("find ") + BuildingBehaviorName(behavior));
        count = behavior == BuildingBehavior::ShipStop && !wharfExists ? 0 : 1;
        if (behavior == BuildingBehavior::ShipStop) return wharf;
        if (behavior == BuildingBehavior::BusStop) return bus;
        if (behavior == BuildingBehavior::Hotel) return hotel;
        return hut;
    }
    bool wharfExists = true;
    float SlotPrice(object::Base*, int32_t slot) { return 10.0f + static_cast<float>(slot); }
    float SlotReferencePrice(object::Base*, int32_t slot) { return 4.0f + static_cast<float>(slot); }
    const char* CommodityName(int32_t commodity) { return commodity == 11 ? "COM_ICECREAM" : "COM_OTHER"; }
    int32_t CommodityFamilyOf(int32_t commodity) { return commodity + 100; }
    const char* FamilyKey(int32_t family) { return family == 111 ? "COMFAM_SNACKS" : "COMFAM_OTHER"; }
    int32_t ActiveEventId(BuildingEvents*) { return 17; }
    int32_t SeaArea(float x, float y, float z)
    {
        seaAt[0] = x, seaAt[1] = y, seaAt[2] = z;
        return 3;
    }
    uint32_t CellFlags(float, float, float) { return cellFlags; }
    bool SlotUsable(int32_t, int32_t) { return usable; }
    bool NeighboursClear(Guest&) { return clear; }
    float GameDeltaTime() { return 0.25f; }
    float GameTime() { return 12.5f; }
    ScriptValue Base(Guest&, int32_t key, intptr_t data)
    {
        baseKeys.push_back(key);
        return {key, static_cast<int32_t>(data)};
    }
    void Base(Guest&, int32_t key, intptr_t, int32_t, int32_t) { baseKeys.push_back(key); }
    int32_t FloatToIntWrap(int32_t floatBits) { return static_cast<int32_t>(Bits(floatBits)); }
    int Rand() { return nextRand; }
    void SetBinFill(object::Base*, int32_t fill) { log.push_back("fill " + std::to_string(fill)); }
    void ReleaseSprite(void*) { log.push_back("release"); }
    void* CreateSprite(const char* name)
    {
        log.push_back(std::string("create ") + name);
        return &f.attached;
    }
    void SetAnim(void*, const char* name, float time, float* position)
    {
        log.push_back(std::string("anim ") + name + " " + std::to_string(time) + " " + std::to_string(position[0]));
    }
};

ScriptValue Get(Fixture& f, FakeEnv& env, int32_t key)
{
    return gs::GetValue(*f.guest, key, 0, env);
}
void Set(Fixture& f, FakeEnv& env, int32_t key, int32_t type, int32_t bits, int32_t data = 0)
{
    gs::SetValue(*f.guest, key, data, type, bits, env);
}

} // namespace

OBL_TEST_CASE("guest_script.get_reads_the_fields_the_script_names")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    f.message.sender = 41, f.message.data = 42, f.message.floatData = 0.5f, f.message.flags = 43, f.message.extra = 44;
    g.m_VehicleID = 3, g.m_walkpoint = 1, g.m_opinion[0] = 0.25f, g.m_opinion[3] = 0.75f, g.m_drunk = 2.0f, g.m_cash = 30.0f;
    g.m_happiness = 0.125f, g.m_oxygen = 100, g.m_Fidget = 0xa5, g.m_walkpoints[1][0] = 8.0f, g.m_walkpoints[1][1] = 9.0f;
    for (int i = 0; i < 26; ++i) g.m_desires[i] = static_cast<float>(i);
    g.m_Tolerance[7] = 2.0f;
    std::strcpy(g.m_WalkAnimation, "D_WALK");

    OBL_CHECK_EQ(Get(f, env, 0).bits, 41);
    OBL_CHECK_EQ(Get(f, env, 1).bits, 3);
    OBL_CHECK_EQ(Get(f, env, 2).type, gs::kFloat);
    OBL_CHECK_EQ(Bits(Get(f, env, 2).bits), 0.25f);
    OBL_CHECK_EQ(Get(f, env, 3).bits, 1);
    OBL_CHECK_EQ(Get(f, env, 4).type, gs::kText);
    OBL_CHECK(std::strcmp(script_program::exec::AsText(Get(f, env, 4).bits), "D_WALK") == 0);
    OBL_CHECK_EQ(Get(f, env, 5).bits, 42);
    OBL_CHECK_EQ(Bits(Get(f, env, 6).bits), 0.5f);
    OBL_CHECK_EQ(Get(f, env, 7).type, gs::kByte);
    OBL_CHECK_EQ(Get(f, env, 7).bits, 0xa5);
    OBL_CHECK_EQ(Bits(Get(f, env, 9).bits), 2.0f);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x0a).bits), 30.0f);
    OBL_CHECK_EQ(Get(f, env, 0x2b).bits, 43);
    OBL_CHECK_EQ(Get(f, env, 0x5d).bits, 44);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x2c).bits), 0.125f);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x26).bits), 100.0f);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x29).bits), 0.75f);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x33).bits), 8.0f); // walk point 1
    OBL_CHECK_EQ(Bits(Get(f, env, 0x34).bits), 9.0f);
    // The desires are numbered in the script's own order, not the field order.
    OBL_CHECK_EQ(Bits(Get(f, env, 0x0b).bits), 16.0f); // Stroll
    OBL_CHECK_EQ(Bits(Get(f, env, 0x0c).bits), 12.0f); // Sunbathing
    OBL_CHECK_EQ(Bits(Get(f, env, 0x0d).bits), 13.0f); // Swimming
    OBL_CHECK_EQ(Bits(Get(f, env, 0x0e).bits), 0.0f); // Accomodation
    OBL_CHECK_EQ(Bits(Get(f, env, 0x19).bits), 11.0f); // SportsFun
    OBL_CHECK_EQ(Bits(Get(f, env, 0x1a).bits), 14.0f); // Toilet
    OBL_CHECK_EQ(Bits(Get(f, env, 0x1b).bits), 15.0f); // WaterSportsFun
    OBL_CHECK_EQ(Bits(Get(f, env, 0x1c).bits), 17.0f); // LeaveResort
    OBL_CHECK_EQ(Bits(Get(f, env, 0x24).bits), 25.0f); // Scared
    // Prices come from the building at the destination and the slot the guest wants.
    OBL_CHECK_EQ(Bits(Get(f, env, 0x25).bits), 12.0f);
    OBL_CHECK_EQ(Bits(Get(f, env, 0x2d).bits), 12.0f); // reference price 6 x tolerance 2
    OBL_CHECK(std::strcmp(script_program::exec::AsText(Get(f, env, 0x27).bits), "COM_ICECREAM") == 0);
    OBL_CHECK(std::strcmp(script_program::exec::AsText(Get(f, env, 0x08).bits), "COMFAM_SNACKS") == 0);
    OBL_CHECK_EQ(Get(f, env, 0x61).bits, 17);
}

// A script `Message` whose data is a text puts the text's id into data (the obl_save_objects crash read that id as an address):
// MessData (0x4f) hands the id on unchanged and it reads back as the text.
OBL_TEST_CASE("guest_script.get_message_text_is_the_id_the_sender_put_in")
{
    Fixture f;
    FakeEnv env(f);
    static const char kAnim[] = "D_WAVE";
    f.message.data = gs::TextBits(kAnim);
    const ScriptValue value = Get(f, env, 0x4f);
    OBL_CHECK_EQ(value.type, gs::kText);
    OBL_CHECK_EQ(value.bits, f.message.data);
    OBL_CHECK_EQ(script_program::exec::AsText(value.bits), kAnim);
    f.message.data = 0x7e7fffff; // an id the table never handed out reads as no text
    OBL_CHECK(script_program::exec::AsText(Get(f, env, 0x4f).bits) == nullptr);
}

OBL_TEST_CASE("guest_script.get_rest_of_the_values")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    f.def->m_Gender = 0;
    OBL_CHECK_EQ(Get(f, env, 0x50).bits, 1); // ImMale
    f.def->m_Gender = 1;
    OBL_CHECK_EQ(Get(f, env, 0x50).bits, 0);
    f.def->m_Litter = 2.57f;
    OBL_CHECK_EQ(Get(f, env, 0x4c).bits, 25); // IceDropScore: truncated x10
    f.def->m_Litter = 536870912.0f; // x10 = 5368709120 = 0x1'40000000: the float-to-int conversion keeps the low word of the 64-bit result
    OBL_CHECK_EQ(static_cast<uint32_t>(Get(f, env, 0x4c).bits), 0x40000000u);
    f.def->m_Litter = -536870912.0f;
    OBL_CHECK_EQ(static_cast<uint32_t>(Get(f, env, 0x4c).bits), 0xc0000000u);
    f.def->m_Litter = 1e30f; // past int64: the integer indefinite
    OBL_CHECK_EQ(Get(f, env, 0x4c).bits, 0);
    f.def->m_Litter = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(Get(f, env, 0x4c).bits, 0);
    f.def->m_IllDecay = 8.0f;
    OBL_CHECK_EQ(Bits(Get(f, env, 0x57).bits), 2.0f); // delta 0.25 x decay
    OBL_CHECK_EQ(Get(f, env, 0x62).bits, 7); // StageDirection: the building's sprite faces 0
    f.otherSprite.m_direction = 3;
    OBL_CHECK_EQ(Get(f, env, 0x62).bits, 1);
    OBL_CHECK_EQ(Get(f, env, 0x38).bits, 2); // MyDir
    OBL_CHECK_EQ(Get(f, env, 0x66).bits, 0);
    OBL_CHECK_EQ(Get(f, env, 0x66).type, gs::kInt);
    g.m_attachedSprite = nullptr;
    OBL_CHECK(std::strcmp(script_program::exec::AsText(Get(f, env, 0x39).bits), "NONE") == 0);
    g.m_attachedSprite = &f.attached;
    OBL_CHECK(std::strcmp(script_program::exec::AsText(Get(f, env, 0x39).bits), "BALL") == 0);
    g.m_ActivityPerson = 5;
    OBL_CHECK_EQ(Get(f, env, 0x49).bits, 1); // ActionFat: the other one is SHAZ
    std::strcpy(f.otherName, "fat_bloke");
    OBL_CHECK_EQ(Get(f, env, 0x49).bits, 1);
    std::strcpy(f.otherName, "NOBODY");
    OBL_CHECK_EQ(Get(f, env, 0x49).bits, 0);
    g.m_ActivityPerson = 77; // no such object: the bin is empty
    OBL_CHECK_EQ(Get(f, env, 0x51).bits, 0);
    g.m_ActivityPerson = 5;
    // Wharf, else bus stop, else the first object of the hotel / the hut.
    OBL_CHECK_EQ(Get(f, env, 0x4b).bits, 5);
    env.wharfExists = false;
    env.log.clear();
    OBL_CHECK_EQ(Get(f, env, 0x4b).bits, 5);
    OBL_CHECK_EQ(env.log.size(), 2u);
    OBL_CHECK_EQ(Get(f, env, 0x4d).bits, 5);
    OBL_CHECK_EQ(Get(f, env, 0x5b).bits, 5);
    // The sea variable looks under the second walk point while the guest swims out (state 0x0d).
    g.m_WorldXYZ[0] = 1.0f, g.m_walkpoints[1][0] = 2.0f;
    g.m_state = 0x0d;
    OBL_CHECK_EQ(Get(f, env, 0x63).bits, 3);
    OBL_CHECK_EQ(env.seaAt[0], 2.0f);
    g.m_state = 1;
    Get(f, env, 0x63);
    OBL_CHECK_EQ(env.seaAt[0], 1.0f);
    // NotOnPath: blocked cells (0x10000080) and land cells without bit 0x10 do not count.
    env.cellFlags = 0;
    OBL_CHECK_EQ(Get(f, env, 0x60).bits & 0xff, 1);
    env.cellFlags = 0x80;
    OBL_CHECK_EQ(Get(f, env, 0x60).bits & 0xff, 0);
    env.cellFlags = 0x10000000;
    OBL_CHECK_EQ(Get(f, env, 0x60).bits & 0xff, 0);
    env.cellFlags = 2;
    OBL_CHECK_EQ(Get(f, env, 0x60).bits & 0xff, 0);
    env.cellFlags = 2 | 0x10;
    OBL_CHECK_EQ(Get(f, env, 0x60).bits & 0xff, 1);
    env.usable = false;
    OBL_CHECK_EQ(Get(f, env, 0x67).bits & 0xff, 1); // DesiredCommodSeaClosed
    g.m_oxygen = -1;
    OBL_CHECK_EQ(Bits(Get(f, env, 0x26).bits), 4294967296.0f); // unsigned
}

OBL_TEST_CASE("guest_script.get_opinion_variables_follow_the_unordered_compares")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float& o : g.m_opinion) o = 0.7f;
    g.m_opinion[4] = 0.2f;
    g.m_opinion[1] = 0.2f; // the first of equal lows wins (strictly less)
    OBL_CHECK_EQ(Get(f, env, 0x42).bits, 1);
    OBL_CHECK_EQ(Get(f, env, 0x43).bits, 1); // 0.2 is in (0.1, 0.5)
    g.m_opinion[1] = 0.9f, g.m_opinion[5] = 0.95f;
    OBL_CHECK_EQ(Get(f, env, 0x44).bits, 5);
    OBL_CHECK_EQ(Get(f, env, 0x45).bits, 3); // above 0.5
    for (float& o : g.m_opinion) o = 3.0f; // none below 2.0: no worst
    OBL_CHECK_EQ(Get(f, env, 0x42).bits, 7);
    for (float& o : g.m_opinion) o = -3.0f; // none above -1.0: no best
    OBL_CHECK_EQ(Get(f, env, 0x44).bits, 7);
    g.m_opinion[2] = 0.05f;
    OBL_CHECK_EQ(gs::OpinionBand(0.05f), 0);
    OBL_CHECK_EQ(gs::OpinionBand(0.1f), 0);
    OBL_CHECK_EQ(gs::OpinionBand(0.3f), 1);
    OBL_CHECK_EQ(gs::OpinionBand(0.5f), 2);
    OBL_CHECK_EQ(gs::OpinionBand(0.50000006f), 3);
    OBL_CHECK_EQ(gs::OpinionBand(nan), 0);
    // A NaN opinion is never the lowest, but it replaces the highest (unordered compares as "less").
    for (float& o : g.m_opinion) o = 0.5f;
    g.m_opinion[3] = nan;
    OBL_CHECK_EQ(Get(f, env, 0x42).bits, 0); // 0.5 < 2.0 at index 0, nothing lower
    OBL_CHECK_EQ(Get(f, env, 0x44).bits, 4); // index 3 (NaN) replaces the 0.5, then index 4 replaces the NaN again
}

OBL_TEST_CASE("guest_script.get_lotion_is_used_up_once")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    g.m_AmountLoition = 2;
    OBL_CHECK_EQ(Get(f, env, 0x52).bits, 1);
    OBL_CHECK_EQ(g.m_AmountLoition, 1);
    OBL_CHECK_EQ(g.m_AppliedLotion, 1);
    OBL_CHECK_EQ(g.m_desires[22], 0.0f);
    OBL_CHECK_EQ(Get(f, env, 0x52).bits, 0); // already applied: the desire is satisfied instead
    OBL_CHECK_EQ(g.m_desires[22], 1.0f);
    OBL_CHECK_EQ(g.m_AmountLoition, 1);
    g.m_AppliedLotion = 0, g.m_desires[22] = 0.0f;
    OBL_CHECK_EQ(Get(f, env, 0x52).bits, 1); // the last bottle
    OBL_CHECK_EQ(g.m_AmountLoition, 0);
    OBL_CHECK_EQ(g.m_desires[22], 1.0f);
    g.m_AppliedLotion = 0, g.m_desires[22] = 0.0f;
    OBL_CHECK_EQ(Get(f, env, 0x52).bits, 0); // none left
    OBL_CHECK_EQ(g.m_desires[22], 1.0f);
}

OBL_TEST_CASE("guest_script.get_dispatch_matches_the_handled_numbers")
{
    for (int32_t key = -3; key < 0x130; ++key) {
        Fixture f;
        FakeEnv env(f);
        f.guest->m_walkpoint = 0;
        f.guest->m_ActivityPerson = 5;
        const ScriptValue value = gs::GetValue(*f.guest, key, 1234, env);
        const bool toBase = env.baseKeys.size() == 1 && env.baseKeys[0] == key;
        OBL_CHECK_EQ(toBase, !gs::HandlesGet(key));
        if (toBase) OBL_CHECK_EQ(value.bits, 1234); // the base class's answer is returned untouched
    }
}

OBL_TEST_CASE("guest_script.set_stores_what_the_script_hands_over")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    Set(f, env, 1, gs::kInt, 12);
    OBL_CHECK_EQ(g.m_VehicleID, 12);
    Set(f, env, 1, gs::kFloat, FloatBits(7.9f)); // a float becomes an int through the float-to-int conversion
    OBL_CHECK_EQ(g.m_VehicleID, 7);
    Set(f, env, 1, gs::kByte, 0x1234);
    OBL_CHECK_EQ(g.m_VehicleID, 0x34);
    Set(f, env, 1, gs::kText, 99);
    OBL_CHECK_EQ(g.m_VehicleID, 0);
    Set(f, env, 9, gs::kInt, -1); // an int is an unsigned number for a float variable
    OBL_CHECK_EQ(g.m_drunk, 4294967296.0f);
    Set(f, env, 0x0a, gs::kFloat, FloatBits(12.5f));
    OBL_CHECK_EQ(g.m_cash, 12.5f);
    Set(f, env, 0x2c, gs::kByte, 0x1ff);
    OBL_CHECK_EQ(g.m_happiness, 255.0f);
    Set(f, env, 0x2e, gs::kInt, 0x1234567);
    OBL_CHECK_EQ(g.m_activity, 0x67); // byte variables keep the low byte of the bits, whatever the kind
    Set(f, env, 0x53, gs::kFloat, 0x3f800000);
    OBL_CHECK_EQ(g.m_AppliedLotion, 0);
    Set(f, env, 0x28, gs::kInt, 3);
    OBL_CHECK_EQ(g.m_desired_commod, 3);
    Set(f, env, 0x69, gs::kFloat, FloatBits(-0.5f));
    OBL_CHECK_EQ(g.m_sunBurnLevel, -0.5f);
    Set(f, env, 0x29, gs::kFloat, FloatBits(0.25f));
    OBL_CHECK_EQ(g.m_opinion[3], 0.25f);
    Set(f, env, 2, gs::kFloat, FloatBits(0.5f));
    OBL_CHECK_EQ(g.m_opinion[0], 0.5f);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("guest_script.set_desires_and_the_one_clamp")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    Set(f, env, 0x0b, gs::kFloat, FloatBits(3.0f));
    OBL_CHECK_EQ(g.m_desires[16], 3.0f); // Stroll
    Set(f, env, 0x1d, gs::kFloat, FloatBits(4.0f));
    OBL_CHECK_EQ(g.m_desires[18], 4.0f); // Vandalise
    Set(f, env, 0x1a, gs::kFloat, FloatBits(3.0f));
    OBL_CHECK_EQ(g.m_desires[14], 1.0f); // Toilet is capped at 1
    Set(f, env, 0x1a, gs::kFloat, FloatBits(0.5f));
    OBL_CHECK_EQ(g.m_desires[14], 0.5f);
    Set(f, env, 0x1a, gs::kFloat, FloatBits(std::numeric_limits<float>::quiet_NaN()));
    OBL_CHECK(std::isnan(g.m_desires[14]));
    // Desires the script cannot set go to the base class.
    Set(f, env, 0x0e, gs::kFloat, FloatBits(1.0f));
    OBL_CHECK_EQ(env.baseKeys.size(), 1u);
}

OBL_TEST_CASE("guest_script.set_walk_points_and_texts")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    g.m_walkpoint = 2;
    Set(f, env, 0x33, gs::kFloat, FloatBits(5.0f));
    Set(f, env, 0x34, gs::kFloat, FloatBits(6.0f));
    OBL_CHECK_EQ(g.m_walkpoints[2][0], 5.0f);
    OBL_CHECK_EQ(g.m_walkpoints[2][1], 6.0f);
    const char* walk = "D_WALK_SAD";
    Set(f, env, 4, gs::kText, gs::TextBits(walk));
    OBL_CHECK(std::strcmp(g.m_WalkAnimation, "D_WALK_SAD") == 0);
    const char* stand = "STAND";
    Set(f, env, 0x37, gs::kText, gs::TextBits(stand));
    OBL_CHECK(std::strcmp(g.m_StandAnimation, "STAND") == 0);
}

OBL_TEST_CASE("guest_script.set_direction_wraps_and_stays_inside_the_animation")
{
    Fixture f;
    FakeEnv env(f);
    Set(f, env, 0x38, gs::kInt, 1);
    OBL_CHECK_EQ(f.sprite.m_direction, 1);
    Set(f, env, 0x38, gs::kInt, 10); // (10 + 8) % 8 = 2
    OBL_CHECK_EQ(f.sprite.m_direction, 2);
    Set(f, env, 0x38, gs::kInt, 4); // 4 % 8 = 4 is past the three directions: the last one
    OBL_CHECK_EQ(f.sprite.m_direction, 2);
    Set(f, env, 0x38, gs::kInt, -1); // 7 % 8 = 7: the last one
    OBL_CHECK_EQ(f.sprite.m_direction, 2);
    Set(f, env, 0x38, gs::kInt, -9);
    OBL_CHECK_EQ(f.sprite.m_direction, -1);
    Set(f, env, 0x38, gs::kInt, -8); // 0
    OBL_CHECK_EQ(f.sprite.m_direction, 0);
}

OBL_TEST_CASE("guest_script.set_attached_sprite")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    const char* ball = "BALL";
    const char* none = "none";
    Set(f, env, 0x39, gs::kText, gs::TextBits(ball));
    OBL_CHECK(g.m_attachedSprite == &f.attached);
    OBL_CHECK_EQ(env.log.size(), 1u);
    OBL_CHECK(env.log[0] == "create BALL");
    env.log.clear();
    Set(f, env, 0x39, gs::kText, gs::TextBits(ball)); // replaced: the old one goes first
    OBL_CHECK(env.log == std::vector<std::string>({"release", "create BALL"}));
    env.log.clear();
    Set(f, env, 0x39, gs::kText, gs::TextBits(none)); // `NONE` in any case
    OBL_CHECK(g.m_attachedSprite == nullptr);
    OBL_CHECK(env.log == std::vector<std::string>({"release"}));
    env.log.clear();
    Set(f, env, 0x39, gs::kText, gs::TextBits(none));
    OBL_CHECK(env.log.empty());

    g.m_attachedSprite = &f.attached;
    const char* reverse = "Reverse_SPIN";
    const char* clamp = "Clamp_STOP";
    const char* plain = "WAVE";
    f.sprite.m_direction = 1;
    g.m_WorldXYZ[0] = 3.0f;
    Set(f, env, 0x3a, gs::kText, gs::TextBits(reverse));
    OBL_CHECK_EQ(f.attached.m_animType, 1);
    Set(f, env, 0x3a, gs::kText, gs::TextBits(clamp));
    OBL_CHECK_EQ(f.attached.m_animType, 2);
    Set(f, env, 0x3a, gs::kText, gs::TextBits(plain));
    OBL_CHECK_EQ(f.attached.m_animType, 0);
    OBL_CHECK_EQ(env.log.size(), 3u);
    OBL_CHECK(env.log[0].rfind("anim SPIN 12.5", 0) == 0);
    OBL_CHECK(env.log[1].rfind("anim STOP", 0) == 0);
    OBL_CHECK(env.log[2].rfind("anim WAVE", 0) == 0);
    const char* myDir = "mydir";
    Set(f, env, 0x3b, gs::kText, gs::TextBits(myDir));
    OBL_CHECK_EQ(f.attached.m_direction, 1);
    Set(f, env, 0x3b, gs::kText, gs::TextBits(plain)); // anything but MyDir is ignored
    g.m_attachedSprite = nullptr;
    env.log.clear();
    Set(f, env, 0x3a, gs::kText, gs::TextBits(plain)); // no attachment: nothing to animate
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("guest_script.set_walk_speed_is_varied_by_a_quarter")
{
    Fixture f;
    FakeEnv env(f);
    Guest& g = *f.guest;
    env.nextRand = 5;
    Set(f, env, 0x47, gs::kInt, 40); // spread 10: jitter 5 % 10 - 5 = 0
    OBL_CHECK_EQ(g.m_maxVel, 40.0f);
    OBL_CHECK_EQ(g.m_maxForce, static_cast<float>(40.0 * static_cast<double>(0.33333334f)));
    env.nextRand = 7;
    Set(f, env, 0x47, gs::kFloat, FloatBits(40.0f)); // 7 % 10 - 5 = 2
    OBL_CHECK_EQ(g.m_maxVel, 42.0f);
    g.m_maxVel = 1.0f;
    Set(f, env, 0x47, gs::kInt, 3);
    OBL_CHECK_EQ(g.m_maxVel, 1.0f);
}

OBL_TEST_CASE("guest_script.set_bin_fill_needs_the_object")
{
    Fixture f;
    FakeEnv env(f);
    f.guest->m_ActivityPerson = 77;
    Set(f, env, 0x51, gs::kInt, 4);
    OBL_CHECK(env.log == std::vector<std::string>({"object 77"}));
    env.log.clear();
    f.guest->m_ActivityPerson = 5;
    Set(f, env, 0x51, gs::kInt, 4);
    OBL_CHECK(env.log == std::vector<std::string>({"object 5", "object 5", "fill 4"}));
}

OBL_TEST_CASE("guest_script.set_dispatch_matches_the_handled_numbers")
{
    const char* text = "ANIM";
    for (int32_t key = -3; key < 0x130; ++key) {
        Fixture f;
        FakeEnv env(f);
        f.guest->m_walkpoint = 1;
        f.guest->m_ActivityPerson = 5;
        f.guest->m_attachedSprite = &f.attached;
        const bool isText = key == 4 || key == 0x37 || key == 0x39 || key == 0x3a || key == 0x3b;
        gs::SetValue(*f.guest, key, 55, isText ? gs::kText : gs::kInt, isText ? gs::TextBits(text) : 8, env);
        const bool toBase = env.baseKeys.size() == 1 && env.baseKeys[0] == key;
        OBL_CHECK_EQ(toBase, !gs::HandlesSet(key));
    }
}
