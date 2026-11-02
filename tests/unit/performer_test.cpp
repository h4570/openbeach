#include "byte_copy.h"
#include "unit_test.h"
#include "importer/src/readers/save/save_writer.h"
#include "game/objects/performer_attach.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/performer_serial.h"
#include "game/objects/performer_state.h"
#include "game/objects/performer_states.h"
#include "game/objects/performer_receive.h"
#include "game/objects/performer_variant.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace obl::layout;
using obl::messages::Message;
using obl::ui::Sprite;
using obl::ui::SpriteAnim;
using obl::ui::SpriteInstance;
using enum obl::messages::MessageType;
using obl::object::Building;
using namespace obl::object;
namespace perf = obl::performer;

struct Palette {
    Sprite sprite{};
    SpriteInstance instance{};
    explicit Palette(int count)
    {
        sprite.m_paletteCount = count;
        instance.m_sprite = &sprite;
        instance.m_paletteIndex = -77;
    }
};

// Serves the draws in order and the sprite by name; records both.
struct VariantEnv {
    std::vector<int> draws;
    size_t used = 0;
    Palette palette;
    std::vector<std::string> created;
    VariantEnv(std::vector<int> values, int count) : draws(std::move(values)), palette(count) {}
    int Rand() { return draws.at(used++); }
    SpriteInstance* CreateSprite(const char* name)
    {
        created.push_back(name);
        return &palette.instance;
    }
};

struct Record {
    std::string kind, name;
};
struct Probe {
    std::vector<Record> records;
    int attached = -1;
    void Int(const char* n, int32_t&, Hashed) { records.push_back({"i", n}); }
    void UInt(const char* n, uint32_t&, Hashed) { records.push_back({"u", n}); }
    void Float(const char* n, float&, Hashed) { records.push_back({"f", n}); }
    void Bool(const char* n, uint8_t&, Hashed) { records.push_back({"b", n}); }
    void Attached(const char*, void*&, perf::AttachedMode mode)
    {
        attached = static_cast<int>(mode);
        records.push_back({"s", "attached"});
    }
    template<class T> void Base(T&) { records.push_back({"base", ""}); }
};
std::string Order(const Probe& probe)
{
    std::string text;
    for (const Record& r : probe.records) text += r.kind + ":" + r.name + " ";
    return text;
}

struct AttachEnv {
    obl::object::Building building{};
    SpriteInstance stage{};
    std::vector<std::pair<int, int>> joined;
    std::vector<std::pair<float, float>> offsets;
    int asked = -1;
    explicit AttachEnv(int rotation)
    {
        stage.m_direction = rotation;
        building.m_sprite = &stage;
    }
    obl::object::Building& Building(int id)
    {
        asked = id;
        return building;
    }
    void AddActor(obl::object::Building&, int actor) { joined.push_back({asked, actor}); }
    void FootprintPoint(obl::object::Building&, float x, float y, float (&out)[2])
    {
        offsets.push_back({x, y});
        out[0] = 100.5f;
        out[1] = -3.25f;
    }
};
} // namespace

OBL_TEST_CASE("Performer HasState tables")
{
    for (uint32_t state = 0; state < 0x1000; ++state) {
        OBL_CHECK(perf::ShowHasState(state) == (state == 0 || state == 1 || state == 4 || state == 6));
        OBL_CHECK(perf::SingerHasState(state) == (state == 0 || state == 1 || state == 6));
    }
    OBL_CHECK(!perf::ShowHasState(0xffffffffu));
    OBL_CHECK(!perf::SingerHasState(0x80000000u));
}

OBL_TEST_CASE("Performer variants draw in a fixed order and clamp to the palette count")
{
    { // Aerobic: the first draw picks the tens (mod 6), the second the units (mod 7, plus 2)
        Aerobic performer{};
        VariantEnv env({13, 9}, 100);
        perf::SetVariant(performer, 0, env);
        OBL_CHECK(env.created == std::vector<std::string>{"Gail"});
        OBL_CHECK(performer.m_sprite == &env.palette.instance);
        OBL_CHECK(env.palette.instance.m_paletteIndex == 9 % 7 + 2 + (13 % 6) * 10);
        VariantEnv shaz({0, 0}, 100);
        perf::SetVariant(performer, 5, shaz);
        OBL_CHECK(shaz.created == std::vector<std::string>{"Shaz"});
        VariantEnv clamped({5, 6}, 20);
        perf::SetVariant(performer, 1, clamped);
        OBL_CHECK(clamped.palette.instance.m_paletteIndex == 19); // 6 % 7 + 2 + 5 * 10 = 58 >= 20
        OBL_CHECK(performer.m_variant == 1);
    }
    { // Cabaret: four acts, any other only stores the type
        Cabaret performer{};
        const char* names[] = {"GAIL", "SHAZ", "FAT_BLOKE", "FIT_BLOKE"};
        for (int variant = 0; variant < 4; ++variant) {
            VariantEnv env({7, 8}, 40);
            perf::SetVariant(performer, variant, env);
            OBL_CHECK(env.created == std::vector<std::string>{names[variant]});
            OBL_CHECK(env.used == (variant < 2 ? 2u : 1u));
            OBL_CHECK(env.palette.instance.m_paletteIndex == (variant < 2 ? 8 % 7 + 2 + (7 % 4) * 10 : 7 % 40));
        }
        VariantEnv none({}, 40);
        performer.m_sprite = nullptr;
        perf::SetVariant(performer, 9, none);
        OBL_CHECK(none.created.empty());
        OBL_CHECK(performer.m_sprite == nullptr);
        OBL_CHECK(performer.m_variant == 9);
    }
    { // Catwalk and Singer use the sprite the constructor made
        Palette palette(60);
        Catwalk catwalk{};
        catwalk.m_sprite = &palette.instance;
        VariantEnv env({3, 4}, 0);
        perf::SetVariant(catwalk, 2, env);
        OBL_CHECK(palette.instance.m_paletteIndex == 4 % 7 + 2 + (3 % 4) * 10);
        Singer singer{};
        singer.m_sprite = &palette.instance;
        VariantEnv env2({15, 20}, 0);
        perf::SetVariant(singer, 1, env2);
        OBL_CHECK(palette.instance.m_paletteIndex == 20 % 7 + 2 + (15 % 6) * 10);
    }
    { // WeightLifter: palette draw, parity draw, units draw
        Palette palette(60);
        WeightLifter performer{};
        performer.m_sprite = &palette.instance;
        VariantEnv even({10, 2, 5}, 0);
        perf::SetVariant(performer, 0, even);
        OBL_CHECK(even.used == 3);
        OBL_CHECK(palette.instance.m_paletteIndex == 5 % 20 + 41);
        VariantEnv odd({10, 3, 33}, 0);
        perf::SetVariant(performer, 0, odd);
        OBL_CHECK(palette.instance.m_paletteIndex == 33 % 30 + 11);
        Palette small(15);
        performer.m_sprite = &small.instance;
        VariantEnv tiny({4, 2, 19}, 0);
        perf::SetVariant(performer, 0, tiny);
        OBL_CHECK(small.instance.m_paletteIndex == 14);
    }
    { // WetTShirt: outfit 3 is the contestant; every other one burns a draw first
        WetTShirt performer{};
        VariantEnv contestant({11}, 50);
        perf::SetVariant(performer, 3, contestant);
        OBL_CHECK(contestant.created == std::vector<std::string>{"wtsc"});
        OBL_CHECK(contestant.used == 1);
        OBL_CHECK(contestant.palette.instance.m_paletteIndex == 11 % 50);
        VariantEnv other({99, 12}, 50);
        perf::SetVariant(performer, 0, other);
        OBL_CHECK(other.created == std::vector<std::string>{"wts"});
        OBL_CHECK(other.used == 2);
        OBL_CHECK(other.palette.instance.m_paletteIndex == 12 % 50);
        OBL_CHECK(performer.m_variant == 0);
    }
}

OBL_TEST_CASE("Performer Save and Load visit the records in a fixed order")
{
    {
        Aerobic performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) ==
                "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished f:performer.action_point.x "
                "f:performer.action_point.y f:performer.action_point.z base: ");
    }
    {
        Cabaret performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) == "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished base: ");
    }
    {
        Catwalk performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) ==
                "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished f:performer.action_point.x "
                "f:performer.action_point.y f:performer.action_point.z base: ");
    }
    {
        Singer performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) ==
                "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished f:performer.action_point.x "
                "f:performer.action_point.y f:performer.action_point.z s:attached "
                "base: ");
        OBL_CHECK(p.attached == static_cast<int>(perf::AttachedMode::Fill));
    }
    {
        WeightLifter performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) ==
                "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished f:performer.action_point.x "
                "f:performer.action_point.y f:performer.action_point.z s:attached "
                "base: ");
        OBL_CHECK(p.attached == static_cast<int>(perf::AttachedMode::Create));
    }
    {
        WetTShirt performer{};
        Probe p;
        perf::Fields(performer, p);
        OBL_CHECK(Order(p) ==
                "i:performer.action_state u:performer.performer_type i:performer.owner b:performer.finished i:performer.contestant "
                "f:performer.action_point.x f:performer.action_point.y "
                "f:performer.action_point.z base: ");
    }
}

OBL_TEST_CASE("Performers join the show and take the stage point")
{
    Aerobic aerobic{};
    aerobic.m_id = 41;
    aerobic.m_ActionPoint[2] = 9.0f;
    AttachEnv env(0);
    perf::AttachToBuilding(aerobic, 7, env);
    OBL_CHECK(aerobic.m_OwnerID == 7);
    OBL_CHECK((env.joined == std::vector<std::pair<int, int>>{{7, 41}}));
    OBL_CHECK((env.offsets == std::vector<std::pair<float, float>>{{6.0f, -286.0f}}));
    OBL_CHECK(aerobic.m_ActionPoint[0] == 100.5f);
    OBL_CHECK(aerobic.m_ActionPoint[1] == -3.25f);
    OBL_CHECK(aerobic.m_ActionPoint[2] == 0.0f);

    Cabaret cabaret{};
    AttachEnv plain(0);
    perf::AttachToBuilding(cabaret, 3, plain);
    OBL_CHECK(plain.offsets.empty());
    OBL_CHECK(cabaret.m_OwnerID == 3);

    WetTShirt wet{};
    AttachEnv contest(0);
    perf::AttachToBuilding(wet, 5, contest);
    OBL_CHECK((contest.offsets == std::vector<std::pair<float, float>>{{13.0f, -274.0f}}));
    OBL_CHECK(wet.m_OwnerID == 5);
}

OBL_TEST_CASE("A catwalk hides for rotations 1 and 2 only, a microphone turns to (11 - 2 * rotation) % 8")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        Catwalk catwalk{};
        catwalk.m_visible = 1;
        AttachEnv env(rotation);
        perf::AttachToBuilding(catwalk, 2, env);
        OBL_CHECK(catwalk.m_visible == (rotation == 1 || rotation == 2 ? 0 : 1));
    }
    SpriteAnim anim{};
    anim.m_directions = 8;
    SpriteAnim* anims[] = {nullptr, &anim};
    Sprite sprite{};
    sprite.m_anims = anims;
    SpriteInstance mic{};
    mic.m_sprite = &sprite;
    mic.m_animIndex = 1;
    Singer singer{};
    singer.m_attachedSprite = &mic;
    for (int rotation = 0; rotation < 4; ++rotation) {
        AttachEnv env(rotation);
        perf::AttachToBuilding(singer, 2, env);
        OBL_CHECK(mic.m_direction == (11 - 2 * rotation) % 8);
    }
    anim.m_directions = 4; // clamped to the last direction
    AttachEnv env(0);
    perf::AttachToBuilding(singer, 2, env);
    OBL_CHECK(mic.m_direction == 3);
}

namespace {
// Lists every call of the state methods in order; the footprint point is the offset plus (500.75, 900.5), an animation lasts 2.5 s.
struct StateEnv {
    obl::object::Building building{};
    SpriteInstance stage{};
    SpriteAnim anim{};
    SpriteAnim* anims[1] = {&anim};
    Sprite sheet{};
    SpriteInstance body{}, made[2]{};
    int createdCount = 0;
    bool updateOk = true;
    float length = 2.5f;
    std::vector<std::string> log;
    explicit StateEnv(int rotation, int directions = 8)
    {
        stage.m_direction = rotation;
        building.m_sprite = &stage;
        building.m_WorldXYZ[0] = 1000.0f;
        building.m_WorldXYZ[1] = 2000.0f;
        anim.m_directions = static_cast<decltype(anim.m_directions)>(directions);
        sheet.m_anims = anims;
        body.m_sprite = made[0].m_sprite = made[1].m_sprite = &sheet;
        body.m_animType = made[0].m_animType = made[1].m_animType = 77;
    }
    template<class T> void Dress(T& object, int id = 9, int owner = 7)
    {
        object.m_id = id;
        object.m_OwnerID = owner;
        object.m_sprite = &body;
        object.m_visible = 1;
    }
    std::string Which(const SpriteInstance* sprite) const
    {
        return sprite == &body ? "body" : sprite == &made[0] ? "made0" : sprite == &made[1] ? "made1" : sprite ? "?" : "null";
    }
    static std::string Num(double value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", value);
        return text;
    }
    SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("create ") + name);
        return &made[createdCount++];
    }
    void ReleaseSprite(SpriteInstance* sprite) { log.push_back("release " + Which(sprite)); }
    float SetAnim(SpriteInstance* sprite, const char* name, float*)
    {
        log.push_back("anim " + Which(sprite) + " " + name);
        return length;
    }
    bool HumanUpdate(Mover&) { return updateOk; }
    void Fatal(const char* format, const char* name) { log.push_back(std::string("fatal ") + format + "|" + name); }
    obl::object::Building& Building(int id)
    {
        log.push_back("building " + std::to_string(id));
        return building;
    }
    void FootprintPoint(obl::object::Building&, float x, float y, float (&out)[2])
    {
        log.push_back("point " + Num(x) + " " + Num(y));
        out[0] = x + 500.75f;
        out[1] = y + 900.5f;
    }
    std::string Text(const Message& m) const
    {
        return std::to_string(m.sender) + ">" + std::to_string(m.receiver) + " " + std::to_string(m.type) + " " + std::to_string(m.flags) +
                " " + std::to_string(m.data);
    }
    void Post(const Message& m) { log.push_back("post " + Text(m)); }
    void PostDelayed(const Message& m, float delay) { log.push_back("delayed " + Text(m) + " @" + Num(delay)); }
    void Teleport(Mover&, float x, float y, float z) { log.push_back("teleport " + Num(x) + " " + Num(y) + " " + Num(z)); }
    void SendDelayed(Mover&, const Message& m, float delay) { log.push_back("send " + Text(m) + " @" + Num(delay)); }
    void Draw(SpriteInstance* sprite, float* position) { log.push_back("draw " + Which(sprite) + " " + Num(position[0])); }
};
using Log = std::vector<std::string>;
} // namespace

OBL_TEST_CASE("Aerobic and Singer walk on stage in state 1 and off it in state 6")
{
    for (int rotation = 0; rotation < 4; ++rotation) {
        Aerobic aerobic{};
        StateEnv env(rotation);
        env.Dress(aerobic);
        OBL_CHECK(perf::EnterState(aerobic, 1, env) == 1);
        OBL_CHECK((env.log == Log{"post 9>9 3 0 0", "building 7", "anim body d_walk_normal", "point 6 -286", "post 9>9 5 506 614"}));
        OBL_CHECK(aerobic.m_visible == (rotation == 1 || rotation == 2 ? 0 : 1));
        OBL_CHECK(env.body.m_animType == 77);
    }
    Singer singer{};
    StateEnv env(0);
    env.Dress(singer);
    OBL_CHECK(perf::EnterState(singer, 6, env) == 1);
    OBL_CHECK((env.log == Log{"anim body SING_WALK", "building 7", "point -122 -286", "post 9>9 5 378 614"}));
    OBL_CHECK(env.body.m_animType == 0);
    env.log.clear();
    OBL_CHECK(perf::EnterState(singer, 0, env) == 1);
    OBL_CHECK(perf::EnterState(singer, 0x105, env) == 5); // the low byte of an unrecognized state
    OBL_CHECK(perf::EnterState(singer, 0x100, env) == 0);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("Cabaret appears at the stage exit and walks to the mark of the building's rotation")
{
    const char* moves[4] = {"post 9>9 10 979 1678", "post 9>9 10 475 618", "post 9>9 10 475 618", "post 9>9 10 1033 1692"};
    const int directions[4] = {4, 6, 4, 6};
    for (int rotation = 0; rotation < 4; ++rotation) {
        Cabaret cabaret{};
        cabaret.m_actionState = 5;
        StateEnv env(rotation);
        env.Dress(cabaret);
        OBL_CHECK(perf::EnterState(cabaret, 1, env) == 1);
        Log expected{"anim body D_CABARET_WALK_CYCLE", "building 7", "point -122 -286", "teleport 378.75 614.5 0"};
        const bool behind = rotation == 1 || rotation == 2;
        if (behind) expected.push_back("point -25 -282");
        expected.push_back(moves[rotation]);
        OBL_CHECK((env.log == expected));
        OBL_CHECK(cabaret.m_visible == (behind ? 0 : 1));
        OBL_CHECK(cabaret.m_actionState == 0);
        OBL_CHECK(env.body.m_animType == 0);
        OBL_CHECK(env.body.m_direction == directions[rotation]);
        StateEnv narrow(rotation, 4); // an animation with four directions: the last one
        narrow.Dress(cabaret);
        perf::EnterState(cabaret, 1, narrow);
        OBL_CHECK(narrow.body.m_direction == 3);
    }
    Cabaret cabaret{};
    StateEnv env(0);
    OBL_CHECK(perf::EnterState(cabaret, 0, env) == 1);
    OBL_CHECK(perf::EnterState(cabaret, 6, env) == 0);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("Catwalk states: the mark of the act, the next state, the show and the way out")
{
    Catwalk catwalk{};
    catwalk.m_variant = 1;
    catwalk.m_actionState = 3;
    catwalk.m_ActionPoint[0] = 40.9f;
    catwalk.m_ActionPoint[1] = -7.9f;
    StateEnv env(0);
    env.Dress(catwalk);
    OBL_CHECK(perf::EnterState(catwalk, 0, env) == 1);
    OBL_CHECK((env.log == Log{"building 7", "anim body D_CATWALK", "point -30 -320", "post 9>9 5 470 580"}));
    OBL_CHECK(env.body.m_animType == 0);
    OBL_CHECK(catwalk.m_actionState == 3);
    env.log.clear();
    OBL_CHECK(perf::EnterState(catwalk, 1, env) == 1);
    OBL_CHECK((env.log == Log{"post 9>9 3 0 0"}));
    OBL_CHECK(catwalk.m_actionState == 0);
    env.log.clear();
    catwalk.m_variant = 0;
    perf::EnterState(catwalk, 1, env);
    OBL_CHECK((env.log == Log{"post 9>9 3 0 4"}));
    env.log.clear();
    env.body.m_animType = 77;
    catwalk.m_actionState = 3;
    OBL_CHECK(perf::EnterState(catwalk, 4, env) == 1);
    OBL_CHECK((env.log == Log{"anim body D_CATWALK", "delayed 9>9 5 40 -7 @2.5"}));
    OBL_CHECK(env.body.m_animType == 0);
    OBL_CHECK(catwalk.m_actionState == 0);
    env.log.clear();
    OBL_CHECK(perf::EnterState(catwalk, 6, env) == 1);
    OBL_CHECK((env.log == Log{"anim body D_CATWALK", "building 7", "point -122 -286", "post 9>9 5 378 614"}));
    OBL_CHECK(perf::EnterState(catwalk, 0x207, env) == 7);
    OBL_CHECK(perf::ShowExitState(4));
    OBL_CHECK(!perf::ShowExitState(6));
    OBL_CHECK(perf::StageExitState(1));
    OBL_CHECK(!perf::StageExitState(4));
}

OBL_TEST_CASE("WeightLifter states: walk by act, the weights of the show and their release")
{
    WeightLifter lifter{};
    lifter.m_variant = 1;
    lifter.m_ActionPoint[0] = 12.5f;
    lifter.m_ActionPoint[1] = 30.0f;
    lifter.m_actionState = 3;
    StateEnv env(1);
    env.Dress(lifter);
    OBL_CHECK(perf::EnterState(lifter, 4, env) == 1);
    OBL_CHECK((env.log ==
            Log{"anim body D_WEIGHT_FAT_WALK", "create STAGE_EVENTS", "anim made0 D_WEIGHT_STILL", "building 7",
                    "delayed 9>9 5 12 30 @2.5"}));
    OBL_CHECK(lifter.m_attachedSprite == &env.made[0]);
    OBL_CHECK(env.made[0].m_animType == 2);
    OBL_CHECK(env.made[0].m_direction == 1); // (11 - 2 * 1) % 8
    OBL_CHECK(env.body.m_animType == 0);
    OBL_CHECK(lifter.m_actionState == 0);

    env.log.clear();
    lifter.m_variant = 2;
    OBL_CHECK(perf::EnterState(lifter, 0, env) == 1);
    OBL_CHECK((env.log == Log{"building 7", "anim body D_WEIGHT_BIG_WALK", "point -3 -348", "post 9>9 5 497 552"}));
    env.log.clear();
    lifter.m_variant = 5; // no walk animation for an unrecognized act
    perf::EnterState(lifter, 6, env);
    OBL_CHECK((env.log == Log{"building 7", "point -122 -286", "post 9>9 5 378 614"}));
    env.log.clear();
    lifter.m_variant = 0;
    lifter.m_actionState = 3;
    OBL_CHECK(perf::EnterState(lifter, 1, env) == 1);
    OBL_CHECK((env.log == Log{"building 7", "post 9>9 3 0 4"}));
    OBL_CHECK(lifter.m_visible == 0);
    OBL_CHECK(lifter.m_actionState == 0);
    OBL_CHECK(perf::EnterState(lifter, 2, env) == 0);

    env.log.clear();
    lifter.m_state = 2;
    OBL_CHECK(!perf::ExitState(lifter, env));
    lifter.m_state = 1;
    OBL_CHECK(perf::ExitState(lifter, env));
    OBL_CHECK(env.log.empty());
    lifter.m_state = 4;
    OBL_CHECK(perf::ExitState(lifter, env));
    OBL_CHECK((env.log == Log{"release made0"}));
    OBL_CHECK(lifter.m_attachedSprite == nullptr);
}

OBL_TEST_CASE("WetTShirt states: contestants and the compere")
{
    WetTShirt wet{};
    wet.m_variant = 3;
    wet.m_ActionPoint[0] = 5.5f;
    wet.m_ActionPoint[1] = 6.5f;
    StateEnv env(2);
    env.Dress(wet);
    OBL_CHECK(perf::EnterState(wet, 0, env) == 1);
    OBL_CHECK((env.log == Log{"building 7", "anim body WTSC_WALK_NB", "point -28 -266", "post 9>9 5 472 634"}));
    OBL_CHECK(wet.m_actionState == 5);
    env.log.clear();
    perf::EnterState(wet, 6, env);
    OBL_CHECK((env.log == Log{"anim body WTSC_WALK_NB", "building 7", "point -122 -286", "post 9>9 5 378 614"}));

    env.log.clear();
    wet.m_variant = 0;
    wet.m_actionState = 6;
    perf::EnterState(wet, 0, env);
    OBL_CHECK((env.log == Log{"building 7", "anim body WTS_WALK_WET", "point -58 -290", "post 9>9 5 442 610"}));
    OBL_CHECK(wet.m_actionState == 0);
    env.log.clear();
    perf::EnterState(wet, 0, env);
    OBL_CHECK(env.log[1] == "anim body WTS_WALK_DRY");
    env.log.clear();
    perf::EnterState(wet, 6, env);
    OBL_CHECK(env.log[0] == "anim body WTS_WALK_DRY");

    env.log.clear();
    wet.m_actionState = 3;
    OBL_CHECK(perf::EnterState(wet, 4, env) == 1);
    OBL_CHECK((env.log == Log{"anim body WTS_WAIT_TO_WALK_DRY", "post 9>9 5 5 6", "delayed 9>9 60 0 0 @2.5"}));
    OBL_CHECK(env.body.m_animType == 2);
    OBL_CHECK(wet.m_actionState == 0);
    env.log.clear();
    wet.m_visible = 1;
    OBL_CHECK(perf::EnterState(wet, 1, env) == 1);
    OBL_CHECK((env.log == Log{"building 7", "post 9>9 3 0 4"}));
    OBL_CHECK(wet.m_visible == 0);
    OBL_CHECK(perf::EnterState(wet, 0x303, env) == 3);
}

OBL_TEST_CASE("Performer constructors, destructors, Update and Draw")
{
    Singer singer{};
    singer.m_finish = 9;
    StateEnv env(0);
    perf::Construct(singer, env);
    OBL_CHECK((env.log == Log{"create Stage_Events", "create Stage_Mic", "anim made1 SINGMIC_STILL"}));
    OBL_CHECK(singer.m_sprite == &env.made[0]);
    OBL_CHECK(singer.m_attachedSprite == &env.made[1]);
    OBL_CHECK(singer.m_finish == 0);
    env.log.clear();
    singer.m_WorldXYZ[0] = 3.0f;
    singer.m_ActionPoint[0] = 8.0f;
    perf::Draw(singer, env);
    OBL_CHECK((env.log == Log{"draw made1 8", "draw made0 3"}));
    env.log.clear();
    perf::Destruct(singer, env);
    OBL_CHECK((env.log == Log{"release made0", "release made1"}));
    OBL_CHECK(singer.m_sprite == nullptr);
    OBL_CHECK(singer.m_attachedSprite == nullptr);

    Cabaret cabaret{};
    cabaret.m_finish = 9;
    StateEnv plain(0);
    perf::Construct(cabaret, plain);
    OBL_CHECK((plain.log == Log{}));
    OBL_CHECK(cabaret.m_maxVel == 60.0f);
    OBL_CHECK(cabaret.m_maxForce == 20.0f);
    OBL_CHECK(cabaret.m_finish == 0);
    plain.log.clear();
    perf::Destruct(cabaret, plain);
    OBL_CHECK((plain.log == Log{"release null"}));

    Catwalk catwalk{};
    StateEnv gail(0);
    perf::Construct(catwalk, gail);
    OBL_CHECK((gail.log == Log{"create Gail"}));

    WeightLifter lifter{};
    lifter.m_attachedSprite = &lifter;
    lifter.m_finished = 9;
    StateEnv gym(0);
    perf::Construct(lifter, gym);
    OBL_CHECK((gym.log == Log{"create Stage_Events"}));
    OBL_CHECK(lifter.m_attachedSprite == nullptr);
    OBL_CHECK(lifter.m_finished == 0);
    gym.log.clear();
    lifter.m_WorldXYZ[0] = 4.0f;
    perf::Draw(lifter, gym);
    OBL_CHECK((gym.log == Log{"draw made0 4"}));
    gym.log.clear();
    perf::Destruct(lifter, gym); // no weights: one release
    OBL_CHECK((gym.log == Log{"release made0"}));
    lifter.m_attachedSprite = &gym.made[1];
    lifter.m_ActionPoint[0] = 6.0f;
    lifter.m_sprite = &gym.made[0];
    gym.log.clear();
    perf::Draw(lifter, gym);
    OBL_CHECK((gym.log == Log{"draw made1 6", "draw made0 4"}));
    gym.log.clear();
    perf::Destruct(lifter, gym);
    OBL_CHECK((gym.log == Log{"release made0", "release made1"}));

    Aerobic aerobic{};
    std::strcpy(aerobic.m_Name, "Ann");
    StateEnv update(0);
    OBL_CHECK(perf::Update(aerobic, perf::Kind::Aerobic, update));
    OBL_CHECK(update.log.empty());
    update.updateOk = false;
    OBL_CHECK(perf::Update(aerobic, perf::Kind::WetTShirt, update));
    OBL_CHECK((update.log == Log{"fatal Update of wet T-shirt performer \"%s\" failed|Ann"}));
}

namespace {
struct ReceiveEnv : StateEnv {
    using StateEnv::StateEnv;
    int draw = 0, draws = 0, human = 0;
    Mover* delegatedSelf = nullptr;
    const Message* delegatedMessage = nullptr;
    Message broadcast{};
    std::vector<Catwalk*> actors;
    std::vector<Catwalk*> Catwalks()
    {
        log.push_back("catwalks");
        return actors;
    }
    std::vector<WeightLifter*> lifters;
    std::vector<WeightLifter*> WeightLifters()
    {
        log.push_back("lifters");
        return lifters;
    }
    std::vector<WetTShirt*> wetActors;
    std::vector<WetTShirt*> WetTShirts()
    {
        log.push_back("wetshirts");
        return wetActors;
    }
    int Rand()
    {
        ++draws;
        return draw;
    }
    uint8_t HumanReceive(Mover& performer, const Message& message)
    {
        ++human;
        delegatedSelf = &performer;
        delegatedMessage = &message;
        return 0;
    }
    void PostGuests(obl::object::Building&, const Message& message, float delay)
    {
        broadcast = message;
        log.push_back("guests " + Text(message) + " @" + Num(delay));
    }
};
} // namespace

OBL_TEST_CASE("Aerobic ReceiveMessage orders animation, self timer and guest payload")
{
    for (int draw = 0; draw < 6; ++draw) {
        Aerobic performer{};
        ReceiveEnv env(0);
        env.Dress(performer);
        env.draw = draw;
        Message message{};
        message.type = MT_DO_NEXT_ACTION;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
        OBL_CHECK(env.draws == 1);
        OBL_CHECK(env.body.m_animType == 1);
        OBL_CHECK(env.log ==
                Log({"anim body Aerobic" + std::to_string(draw % 3 + 1), "delayed 9>9 57 0 0 @2.5", "building 7",
                        "guests 9>9 60 0 " + std::to_string(draw % 3) + " @0"}));
        OBL_CHECK(env.broadcast.extra == 64);
        OBL_CHECK(env.broadcast.floatData == 0.0f);
    }
    Aerobic performer{};
    ReceiveEnv env(0);
    env.Dress(performer);
    Message message{};
    message.type = MT_EVENT_FINISHED;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(performer.m_finish == 1);
    OBL_CHECK(env.log.empty());
    message.type = MT_DO_NEXT_ACTION;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(performer.m_actionState == 1);
    OBL_CHECK(env.draws == 0);
    OBL_CHECK(env.log == Log({"anim body Aerobic_To_Stand", "delayed 9>9 57 0 0 @2.5", "building 7", "guests 9>9 60 0 4 @0"}));
    env.log.clear();
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log == Log({"post 9>9 3 0 6"}));
    message.type = MT_MOVEIMHERE;
    performer.m_state = 6;
    env.log.clear();
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(performer.m_waitingForDeath == 1);
    OBL_CHECK(env.log == Log({"post 9>7 59 0 0"}));
    message.type = 12345;
    OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 0);
    OBL_CHECK(env.human == 1);
    for (int rotation = 0; rotation < 4; ++rotation) {
        ReceiveEnv entry(rotation, 4);
        entry.Dress(performer);
        performer.m_state = 0;
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, entry);
        OBL_CHECK(performer.m_actionState == 0);
        OBL_CHECK(entry.body.m_animType == 2);
        OBL_CHECK(entry.body.m_direction == ((11 - 2 * rotation) % 8 < 4 ? (11 - 2 * rotation) % 8 : 3));
        OBL_CHECK(entry.log == Log({"anim body stand_to_aerobic", "building 7", "delayed 9>9 57 0 0 @2.5"}));
    }
}

OBL_TEST_CASE("Catwalk ReceiveMessage pose choices and the next actor preserve send order")
{
    for (int draw = 0; draw < 7; ++draw) {
        ReceiveEnv env(3);
        Catwalk performer{};
        env.Dress(performer);
        performer.m_actionState = 1;
        env.draw = draw;
        Message message{};
        message.type = MT_DO_NEXT_ACTION;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_actionState == (draw == 6 ? 3 : 2));
        OBL_CHECK(env.log ==
                Log({draw < 3              ? "anim body D_CATWALK_POSE1"
                                : draw < 6 ? "anim body D_CATWALK_POSE2"
                                           : "anim body D_CATWALK_TRIP1",
                        "delayed 9>9 57 0 0 @2.5"}));
    }
    ReceiveEnv env(0);
    Catwalk performer{}, next{}, other{};
    env.Dress(performer);
    env.Dress(next, 10);
    next.m_variant = 1;
    env.Dress(other, 11, 8);
    other.m_variant = 1;
    env.actors = {&other, &performer, &next};
    performer.m_actionState = 3;
    Message message{};
    message.type = MT_DO_NEXT_ACTION;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log == Log({"post 9>9 3 0 0", "catwalks", "post 9>10 3 0 4"}));
    env.log.clear();
    env.actors = {&other};
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log == Log({"post 9>9 3 0 0", "catwalks", "post 9>9 3 0 0"}));
    env.log.clear();
    env.actors = {&other, &performer, &next};
    performer.m_finished = 1;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log == Log({"catwalks", "post 9>9 3 0 6", "post 9>10 3 0 6"}));
    env.log.clear();
    performer.m_state = 6;
    message.type = MT_MOVEIMHERE;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log == Log({"post 9>7 59 0 0"}));
    OBL_CHECK(performer.m_waitingForDeath == 1);
    env.log.clear();
    performer.m_variant = 1;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(env.log.empty());
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int draw = 0; draw < 3; ++draw)
            for (int type : {MT_MOVEIMHERE, MT_PLAYANIMATION}) {
                ReceiveEnv waiting(rotation, 4);
                waiting.draw = draw;
                waiting.Dress(performer);
                performer.m_state = 0;
                message.type = type;
                perf::ReceiveMessage(performer, message, waiting);
                OBL_CHECK(waiting.body.m_animType == 2);
                OBL_CHECK(waiting.body.m_direction == ((11 - 2 * rotation) % 8 < 4 ? (11 - 2 * rotation) % 8 : 3));
                OBL_CHECK(waiting.log ==
                        Log({"anim body D_CATWALK_WAIT" + std::to_string(draw + 1), "building 7", "delayed 9>9 60 0 0 @2.5"}));
            }
    for (int draw = 0; draw < 2; ++draw) {
        ReceiveEnv pose(0);
        pose.draw = draw;
        pose.Dress(performer);
        performer.m_actionState = 2;
        message.type = MT_DO_NEXT_ACTION;
        perf::ReceiveMessage(performer, message, pose);
        OBL_CHECK(performer.m_actionState == 3);
        OBL_CHECK(pose.log == Log({"anim body D_CATWALK_POSE" + std::to_string(draw + 1), "delayed 9>9 57 0 0 @2.5"}));
    }
    message.type = MT_EVENT_FINISHED;
    performer.m_finished = 0;
    perf::ReceiveMessage(performer, message, env);
    OBL_CHECK(performer.m_finished == 1);
    message.type = 12345;
    OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 0);
    OBL_CHECK(env.human == 1);
    for (int objectState : {1, 4}) {
        ReceiveEnv arriving(2);
        arriving.Dress(performer);
        performer.m_state = objectState;
        performer.m_actionState = 0;
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, arriving);
        OBL_CHECK(performer.m_actionState == 1);
        OBL_CHECK(arriving.body.m_animType == 2);
        OBL_CHECK(arriving.body.m_direction == 7);
        OBL_CHECK(arriving.log == Log({"anim body D_CATWALK_POSE", "building 7", "delayed 9>9 57 0 0 @2.5"}));
        arriving.log.clear();
        perf::ReceiveMessage(performer, message, arriving);
        OBL_CHECK(arriving.log.empty());
        message.type = MT_PLAYANIMATION;
        perf::ReceiveMessage(performer, message, arriving);
        OBL_CHECK(arriving.log.empty());
    }
}

OBL_TEST_CASE("WeightLifter ReceiveMessage coordinates the actor, bar and guest animations")
{
    const char* names[] = {"THIN", "FAT", "BIG"};
    for (uint32_t variant = 0; variant < 3; ++variant) {
        ReceiveEnv env(1);
        WeightLifter performer{};
        env.Dress(performer);
        performer.m_variant = variant;
        performer.m_attachedSprite = &env.made[0];
        Message message{};
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(env.body.m_animType == 0);
        OBL_CHECK(env.body.m_direction == 1);
        OBL_CHECK(env.log == Log({std::string("anim body D_WEIGHT_") + names[variant] + "_WAIT", "building 7"}));
        env.log.clear();
        performer.m_state = 4;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_actionState == 1);
        OBL_CHECK(env.log == Log({std::string("anim body D_WEIGHT_") + names[variant] + "_POSE", "building 7", "delayed 9>9 57 0 0 @2.5"}));
        env.log.clear();
        message.type = MT_DO_NEXT_ACTION;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_actionState == (variant == 2 ? 0 : 2));
        OBL_CHECK(env.made[0].m_animType == 77);
        Log expected{std::string("anim body D_WEIGHT_") + names[variant] + "_WEIGHT",
                std::string("anim made0 D_WEIGHT_") + names[variant] + "BAR_WEIGHT"};
        if (variant != 2) {
            expected.push_back("building 7");
            expected.push_back("guests 9>9 60 0 1 @2.5");
            OBL_CHECK(env.broadcast.extra == 62);
            OBL_CHECK(env.broadcast.floatData == 0.0f);
        }
        expected.push_back("delayed 9>9 57 0 0 @2.5");
        OBL_CHECK(env.log == expected);
        env.log.clear();
        if (variant == 2) {
            env.body.m_animType = 77;
            perf::ReceiveMessage(performer, message, env);
            OBL_CHECK(performer.m_actionState == 2);
            OBL_CHECK(env.body.m_animType == 77);
            OBL_CHECK(env.broadcast.extra == 62);
            OBL_CHECK(env.log == Log({"anim body D_WEIGHT_BIG_JOY", "building 7", "guests 9>9 60 0 2 @0", "delayed 9>9 57 0 0 @2.5"}));
            env.log.clear();
        }
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_actionState == 3);
        OBL_CHECK(env.log == Log({std::string("anim body D_WEIGHT_") + names[variant] + "_TOWALK", "delayed 9>9 57 0 0 @2.5"}));
        env.log.clear();
        env.lifters = {&performer};
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(env.log == Log({"post 9>9 3 0 0", "lifters", "post 9>9 3 0 0"}));
        env.log.clear();
        performer.m_finished = 1;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(env.log == Log({"lifters", "post 9>9 3 0 6"}));
        env.log.clear();
        performer.m_state = 6;
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_waitingForDeath == 1);
        OBL_CHECK(env.log == (variant == 0 ? Log{"post 9>7 59 0 0"} : Log{}));
        performer.m_finished = 0;
        message.type = MT_EVENT_FINISHED;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_finished == 1);
        message.type = 12345;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 0);
        OBL_CHECK(env.human == 1);
        OBL_CHECK(env.delegatedSelf == &performer);
        OBL_CHECK(env.delegatedMessage == &message);
    }
    ReceiveEnv handoff(0);
    WeightLifter performer{}, wrong{}, next{};
    handoff.Dress(performer);
    handoff.Dress(wrong, 11, 8);
    handoff.Dress(next, 12);
    performer.m_actionState = 3;
    wrong.m_variant = next.m_variant = 1;
    handoff.lifters = {&wrong, &performer, &next};
    Message message{};
    message.type = MT_DO_NEXT_ACTION;
    perf::ReceiveMessage(performer, message, handoff);
    OBL_CHECK(handoff.log == Log({"post 9>9 3 0 0", "lifters", "post 9>12 3 0 4"}));
    handoff.log.clear();
    performer.m_finished = 1;
    perf::ReceiveMessage(performer, message, handoff);
    OBL_CHECK(handoff.log == Log({"lifters", "post 9>9 3 0 6", "post 9>12 3 0 6"}));
}

OBL_TEST_CASE("Singer ReceiveMessage keeps body and microphone choreography synchronized")
{
    const char* body[] = {"SING_NORM", "SING_NORM", "SING_CROUCH", "SING_CROUCH_NORM", "SING_LIFT", "SING_LIFT_NORM", "SING_KNEE_POINT",
            "SING_KNEE_NORM"};
    const char* mic[] = {"SINGMIC_STILL", "SINGMIC_STILL", "SINGMIC_CROUCH", "SINGMIC_CROUCH_NORM", "SINGMIC_LIFT", "SINGMIC_LIFT_NORM",
            "SINGMIC_KNEE_POINT", "SINGMIC_KNEE_NORM"};
    const int next[] = {1, 1, 3, 0, 5, 0, 7, 0};
    for (int action = 0; action <= 9; ++action)
        for (int draw = 0; draw < 4; ++draw)
            for (uint8_t finish : {uint8_t{0}, uint8_t{1}}) {
                ReceiveEnv env(0);
                Singer performer{};
                env.Dress(performer);
                performer.m_attachedSprite = &env.made[0];
                performer.m_actionState = action;
                performer.m_finish = finish;
                env.draw = draw;
                Message message{};
                message.type = MT_DO_NEXT_ACTION;
                OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
                OBL_CHECK(env.body.m_animType == 77);
                OBL_CHECK(env.made[0].m_animType == 77);
                OBL_CHECK(env.draws == (action == 1 || action == 6 ? 1 : 0));
                if (action < 8) {
                    std::string b = body[action], m = mic[action];
                    int state = next[action];
                    if (action == 0 && finish) {
                        b = "SING_NORM_BOW";
                        state = 8;
                    }
                    if (action == 1) {
                        const char* choices[] = {"SING_NORM", "SING_NORM_CROUCH", "SING_NORM_LIFT", "SING_NORM_KNEE"};
                        const int states[] = {1, 2, 4, 6};
                        b = choices[draw];
                        state = states[draw];
                        if (draw == 1) m = "SINGMIC_NORM_CROUCH";
                    }
                    if (action == 6 && draw % 2) {
                        b = "SING_KNEE_LEAN";
                        m = "SINGMIC_KNEE_LEAN";
                    }
                    OBL_CHECK(performer.m_actionState == state);
                    OBL_CHECK(env.log == Log({"anim body " + b, "anim made0 " + m, "delayed 9>9 57 0 0 @2.5"}));
                } else if (action == 8) {
                    OBL_CHECK(performer.m_actionState == 0);
                    OBL_CHECK(env.log == Log({"post 9>9 3 0 6"}));
                } else
                    OBL_CHECK(env.log.empty());
            }
    for (int rotation = 0; rotation < 4; ++rotation) {
        ReceiveEnv env(rotation, 4);
        Singer performer{};
        env.Dress(performer);
        performer.m_attachedSprite = &env.made[0];
        Message message{};
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(env.body.m_animType == 2);
        OBL_CHECK(env.made[0].m_animType == 2);
        OBL_CHECK(env.body.m_direction == ((11 - 2 * rotation) % 8 < 4 ? (11 - 2 * rotation) % 8 : 3));
        OBL_CHECK(env.log == Log({"anim body SING_WAVE", "building 7", "delayed 9>9 57 0 0 @2.5"}));
        env.log.clear();
        performer.m_state = 4;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(env.log.empty());
        performer.m_state = 6;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_waitingForDeath == 1);
        OBL_CHECK(env.log == Log({"post 9>7 59 0 0"}));
        message.type = MT_EVENT_FINISHED;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_finish == 1);
        message.type = 12345;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 0);
        OBL_CHECK(env.delegatedSelf == &performer);
        OBL_CHECK(env.delegatedMessage == &message);
    }
}

OBL_TEST_CASE("Cabaret ReceiveMessage preserves dance, teleport, exit and costume ordering")
{
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int directions : {4, 8})
            for (int action = 0; action <= 4; ++action) {
                ReceiveEnv env(rotation, directions);
                Cabaret performer{};
                env.Dress(performer);
                performer.m_actionState = action;
                env.body.m_direction = 2;
                Message message{};
                message.type = MT_DO_NEXT_ACTION;
                perf::ReceiveMessage(performer, message, env);
                if (action > 2) {
                    OBL_CHECK(env.log.empty());
                    continue;
                }
                const int desired = action == 2 ? (rotation == 0 ? 1 : 3)
                        : rotation == 0         ? 4
                        : rotation == 3         ? 6
                        : action == 0           ? 1
                                                : ((4 - rotation) * 2) % 8;
                OBL_CHECK(env.body.m_direction == (desired < directions ? desired : directions - 1));
                OBL_CHECK(performer.m_actionState == action + 1);
                OBL_CHECK(env.body.m_animType == (action == 2 ? 0 : 2));
                const double x = rotation == 0 ? (action == 2 ? 1005 : 979)
                        : rotation == 3        ? (action == 2 ? 1102 : 1033)
                                               : (action == 2 ? 505.75 : 475.75);
                const double y = rotation == 0 ? (action == 2 ? 1636 : 1678)
                        : rotation == 3        ? (action == 2 ? 1714 : 1692)
                                               : (action == 2 ? 536.5 : 618.5);
                Log expected{"building 7"};
                if (rotation == 1 || rotation == 2) expected.push_back(action == 2 ? "point 5 -364" : "point -25 -282");
                expected.push_back(action == 0 ? "anim body D_CABARET"
                                : action == 1  ? "anim body D_CABARET_TO_WALK"
                                               : "anim body D_CABARET_WALK_CYCLE");
                if (action == 2)
                    expected.push_back("post 9>9 10 " + std::to_string(static_cast<int>(x)) + " " + std::to_string(static_cast<int>(y)));
                else {
                    expected.push_back("teleport " + StateEnv::Num(x) + " " + StateEnv::Num(y) + " 0");
                    expected.push_back("delayed 9>9 57 0 0 @2.5");
                }
                OBL_CHECK(env.log == expected);
                env.log.clear();
                performer.m_actionState = action;
                message.type = MT_MOVEIMHERE;
                if (action != 3) {
                    perf::ReceiveMessage(performer, message, env);
                    if (action == 0) {
                        OBL_CHECK(env.body.m_animType == 1);
                        const int d = rotation == 0 ? 4 : rotation == 3 ? 6 : ((6 - rotation) * 2) % 8;
                        OBL_CHECK(env.body.m_direction == (d < directions ? d : directions - 1));
                        OBL_CHECK(env.log == Log({"building 7", "anim body D_CABARET_TO_WALK", "delayed 9>9 57 0 0 @2.5"}));
                    } else
                        OBL_CHECK(env.log.empty());
                }
            }
    for (int draw = 0; draw < 2; ++draw) {
        ReceiveEnv env(0);
        env.sheet.m_paletteCount = 50;
        env.draw = draw;
        Cabaret performer{};
        env.Dress(performer);
        performer.m_actionState = 3;
        Message message{};
        message.type = MT_MOVEIMHERE;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_sprite == &env.made[0]);
        OBL_CHECK(performer.m_variant == static_cast<uint32_t>(draw));
        OBL_CHECK(env.draws == 3);
        OBL_CHECK(env.made[0].m_paletteIndex == 2 + draw * 11);
        OBL_CHECK(env.log == Log({"release body", draw == 0 ? "create GAIL" : "create SHAZ", "post 9>9 3 0 1"}));
        env.log.clear();
        performer.m_finish = 1;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_waitingForDeath == 1);
        OBL_CHECK(env.log == Log({"post 9>7 59 0 0"}));
        performer.m_finish = 0;
        message.type = MT_EVENT_FINISHED;
        perf::ReceiveMessage(performer, message, env);
        OBL_CHECK(performer.m_finish == 1);
        message.type = 12345;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 0);
        OBL_CHECK(env.delegatedSelf == &performer);
        OBL_CHECK(env.delegatedMessage == &message);
    }
}

OBL_TEST_CASE("PlayAnimation sets the animation type and hands the end state to slot 9")
{
    Aerobic aerobic{};
    StateEnv env(0);
    env.Dress(aerobic);
    OBL_CHECK(perf::PlayAnimation(aerobic, "A", false, 4, false, env) == 2.5f);
    OBL_CHECK((env.log == Log{"anim body A", "send 9>9 3 0 4 @2.5"}));
    OBL_CHECK(env.body.m_animType == 2);
    env.log.clear();
    OBL_CHECK(perf::PlayAnimation(aerobic, "B", false, 4, true, env) == 2.5f); // a loop never ends
    OBL_CHECK((env.log == Log{"anim body B"}));
    OBL_CHECK(env.body.m_animType == 0);
    env.log.clear();
    OBL_CHECK(perf::PlayAnimation(aerobic, "C", true, -1, false, env) == 2.5f); // no end state
    OBL_CHECK((env.log == Log{"anim body C"}));
    OBL_CHECK(env.body.m_animType == 1);
    env.log.clear();
    env.length = 0.0f; // an empty animation lasts a second
    OBL_CHECK(perf::PlayAnimation(aerobic, "D", true, 0, false, env) == 1.0f);
    OBL_CHECK((env.log == Log{"anim body D", "send 9>9 3 0 0 @1"}));
}

OBL_TEST_CASE("WetTShirt ReceiveMessage MT_MOVEIMHERE: the show walk, the wts and wtsc waits, the way out")
{
    Message message{};
    message.type = MT_MOVEIMHERE;
    for (int rotation = 0; rotation < 4; ++rotation) {
        ReceiveEnv env(rotation);
        WetTShirt performer{};
        env.Dress(performer);
        performer.m_state = 4;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
        OBL_CHECK(env.body.m_direction == (13 - 2 * rotation) % 8);
        OBL_CHECK(env.body.m_animType == 2);
        OBL_CHECK(env.log == Log({"anim body WTS_WALK_TO_WAIT_DRY", "building 7", "delayed 9>9 57 0 0 @2.5"}));
        env.log.clear();
        performer.m_state = 0; // "wts": faces first, then the wait animation, the action becomes 1
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
        OBL_CHECK(performer.m_actionState == 1);
        OBL_CHECK(env.body.m_direction == (11 - 2 * rotation) % 8);
        OBL_CHECK(env.log == Log({"building 7", "anim body WTS_WALK_TO_WAIT_DRY", "delayed 9>9 57 0 0 @2.5"}));
    }
    // A narrow animation clamps the facing: before SetAnim for "wts", after it for "wtsc".
    ReceiveEnv narrow(0, 2);
    WetTShirt performer{};
    narrow.Dress(performer);
    perf::ReceiveMessage(performer, message, narrow);
    OBL_CHECK(narrow.body.m_direction == 1);

    for (int draw = 0; draw < 2; ++draw) {
        ReceiveEnv env(1);
        WetTShirt contestant{};
        env.Dress(contestant);
        contestant.m_variant = 3;
        contestant.m_actionState = 5;
        env.draw = draw;
        perf::ReceiveMessage(contestant, message, env);
        OBL_CHECK(env.draws == 1);
        OBL_CHECK(env.body.m_direction == 0);
        OBL_CHECK(env.log == Log({draw == 0 ? "anim body WTSC_WAIT" : "anim body WTSC_EXCITE", "building 7", "delayed 9>9 57 0 0 @2.5"}));
    }
    ReceiveEnv env(2);
    WetTShirt contestant{};
    env.Dress(contestant, 9, 7);
    contestant.m_variant = 3;
    contestant.m_Contestent = 31;
    contestant.m_actionState = 6;
    perf::ReceiveMessage(contestant, message, env);
    OBL_CHECK(contestant.m_actionState == 7);
    OBL_CHECK(env.body.m_animType == 2);
    OBL_CHECK(env.broadcast.extra == 62);
    OBL_CHECK(env.broadcast.floatData == 0.0f);
    OBL_CHECK(env.log == Log({"anim body WTSC_PICKUP", "building 7", "guests 9>9 60 0 1 @2.5", "delayed 9>9 57 0 0 @2.5"}));
    env.log.clear();
    contestant.m_actionState = 8;
    perf::ReceiveMessage(contestant, message, env);
    OBL_CHECK(env.body.m_direction == 6);
    OBL_CHECK(env.log == Log({"anim body WTSC_WALK_TO_POSE", "building 7", "delayed 9>31 60 0 0 @2.5"}));
    env.log.clear();
    for (int idle : {0, 1, 2, 3, 4, 7, 9}) {
        contestant.m_actionState = idle;
        perf::ReceiveMessage(contestant, message, env);
    }
    OBL_CHECK(env.log.empty());

    // State 6: the way out; only a "wtsc" contestant tells the owner.
    contestant.m_state = 6;
    perf::ReceiveMessage(contestant, message, env);
    OBL_CHECK(contestant.m_waitingForDeath == 1);
    OBL_CHECK(env.log == Log({"post 9>7 59 0 0"}));
    env.log.clear();
    WetTShirt plain{};
    env.Dress(plain);
    plain.m_state = 6;
    perf::ReceiveMessage(plain, message, env);
    OBL_CHECK(plain.m_waitingForDeath == 1);
    plain.m_state = 1;
    plain.m_waitingForDeath = 0;
    perf::ReceiveMessage(plain, message, env);
    OBL_CHECK(plain.m_waitingForDeath == 0);
    OBL_CHECK(env.log.empty());

    message.type = MT_EVENT_FINISHED;
    perf::ReceiveMessage(plain, message, env);
    OBL_CHECK(plain.m_finished == 1);
    message.type = 12345;
    OBL_CHECK(perf::ReceiveMessage(plain, message, env) == 0);
    OBL_CHECK(env.human == 1);
}

OBL_TEST_CASE("WetTShirt dry dance transitions and water carrier movement")
{
    for (int action : {0, 1, 5, 6}) {
        WetTShirt performer{};
        ReceiveEnv env(0);
        env.Dress(performer);
        performer.m_state = 4;
        performer.m_actionState = action;
        Message message{};
        message.type = MT_PLAYANIMATION;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
        OBL_CHECK(performer.m_actionState == (action == 1 ? 2 : action));
        OBL_CHECK(env.log == (action == 0 ? Log{"anim body WTS_WALK_DRY"} : action == 5 ? Log{"anim body WTS_WALK_WET"} : Log{}));
    }
    for (int action : {1, 2, 5, 7, 9})
        for (int draw = 0; draw < 7; ++draw) {
            WetTShirt performer{};
            ReceiveEnv env(0);
            env.Dress(performer);
            performer.m_actionState = action;
            env.draw = draw;
            Message message{};
            message.type = MT_DO_NEXT_ACTION;
            OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
            OBL_CHECK(env.human == 0);
            OBL_CHECK(env.draws == (action == 7 || action == 9 ? 0 : 1));
            OBL_CHECK(performer.m_actionState ==
                    (action == 1 && draw == 6                      ? 2
                                    : action == 2 && draw % 4 == 3 ? 1
                                    : action == 7                  ? 8
                                    : action == 9                  ? 5
                                                                   : action));
            OBL_CHECK(env.body.m_animType == (action == 7 ? 0 : 2));
            OBL_CHECK(env.log.back() == (action == 7 ? "post 9>9 5 472 634" : "delayed 9>9 57 0 0 @2.5"));
        }
    for (int rotation = 0; rotation < 4; ++rotation) {
        WetTShirt performer{};
        ReceiveEnv env(rotation);
        env.Dress(performer);
        performer.m_state = 6;
        performer.m_actionState = 5;
        Message message{};
        message.type = MT_PLAYANIMATION;
        message.sender = 123;
        OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
        OBL_CHECK(performer.m_Contestent == 123);
        OBL_CHECK(performer.m_actionState == 6);
        OBL_CHECK(env.body.m_animType == 0);
        OBL_CHECK(env.log.front() == "anim body WTSC_WALK_NB");
        OBL_CHECK(env.log.back() ==
                (rotation == 0                  ? "post 9>9 5 1078 1646"
                                : rotation == 3 ? "post 9>9 5 1064 1766"
                                                : "post 9>9 5 578 546"));
    }
}

OBL_TEST_CASE("WetTShirt show requests the carrier and cycles wet animations")
{
    Message message{};
    message.type = MT_DO_NEXT_ACTION;
    for (int action = 0; action <= 5; ++action)
        for (int draw = 0; draw < 10; ++draw) {
            WetTShirt performer{}, carrier{}, other{}, dancer{};
            ReceiveEnv env(0);
            env.Dress(performer);
            performer.m_state = 4;
            performer.m_actionState = action;
            carrier.m_id = 12;
            carrier.m_OwnerID = 7;
            carrier.m_variant = 3;
            obl::test::ByteCopy(other, carrier);
            other.m_id = 13;
            other.m_OwnerID = 8;
            obl::test::ByteCopy(dancer, carrier);
            dancer.m_id = 14;
            dancer.m_variant = 1;
            env.wetActors = {&other, &carrier, &dancer};
            env.draw = draw;
            OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
            OBL_CHECK(performer.m_actionState == (action == 0 ? 1 : action == 1 ? 1 : action + 1));
            OBL_CHECK(env.body.m_animType == 2);
            const int draws = action == 0 || action == 1 || action == 4 ? 1 : action == 2 ? (draw == 0 ? 3 : 2) : 0;
            OBL_CHECK(env.draws == draws);
            if (action == 0 || action == 2) {
                OBL_CHECK(env.log[0] == "wetshirts");
                OBL_CHECK(env.log[1] == "post 9>12 60 0 0");
            }
            if (action == 2 && draw == 0) {
                OBL_CHECK(env.broadcast.type == MT_SPOT_OPINION);
                OBL_CHECK(env.broadcast.data == 5);
                OBL_CHECK(env.broadcast.extra == 0x3e);
                OBL_CHECK(env.broadcast.floatData == 0.0f);
                OBL_CHECK(env.log.back() == "guests 9>9 73 0 5 @2");
            }
        }
    WetTShirt moving{};
    ReceiveEnv env(0);
    env.Dress(moving);
    moving.m_state = 4;
    moving.m_moving = 1;
    OBL_CHECK(perf::ReceiveMessage(moving, message, env) == 1);
    OBL_CHECK(env.log.empty());
    OBL_CHECK(env.draws == 0);
}

OBL_TEST_CASE("WetTShirt show hands off to the first next actor or finishes every owner member")
{
    for (bool finish : {false, true})
        for (bool found : {false, true}) {
            WetTShirt performer{}, next{}, duplicate{}, foreign{}, carrier{};
            ReceiveEnv env(0);
            env.Dress(performer);
            performer.m_state = 4;
            performer.m_actionState = 6;
            performer.m_variant = 2;
            performer.m_finished = finish;
            next.m_id = 10;
            next.m_OwnerID = 7;
            next.m_variant = 0;
            obl::test::ByteCopy(duplicate, next);
            duplicate.m_id = 11;
            obl::test::ByteCopy(foreign, next);
            foreign.m_OwnerID = 8;
            foreign.m_id = 12;
            obl::test::ByteCopy(carrier, next);
            carrier.m_id = 13;
            carrier.m_variant = 3;
            env.wetActors = found ? std::vector<WetTShirt*>{&foreign, &next, &duplicate, &carrier} : std::vector<WetTShirt*>{};
            Message message{};
            message.type = MT_DO_NEXT_ACTION;
            OBL_CHECK(perf::ReceiveMessage(performer, message, env) == 1);
            const Log expected = finish
                    ? (found ? Log{"wetshirts", "post 9>10 3 0 6", "post 9>11 3 0 6", "post 9>13 3 0 6"} : Log{"wetshirts"})
                    : found ? Log{"post 9>9 3 0 0", "wetshirts", "post 9>10 3 0 4"}
                            : Log{"post 9>9 3 0 0", "wetshirts", "post 9>9 3 0 0"};
            OBL_CHECK(env.log == expected);
            OBL_CHECK(env.draws == 0);
        }
}

namespace {
// Checks every field table entry's obl.save key against the converter's bridge table of the class.
struct BridgeKeys {
    ObjectType type;
    int checked = 0;
    bool Known(const std::string& key) const { return obl::convert::OldObjectFieldKeyKnown(static_cast<uint8_t>(type), key); }
    void Check(const char* key)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
    }
    template<class T> void Int(const char* key, T&, Hashed) { Check(key); }
    template<class T> void UInt(const char* key, T&, Hashed) { Check(key); }
    template<class T> void Float(const char* key, T&, Hashed) { Check(key); }
    template<class T> void Bool(const char* key, T&, Hashed) { Check(key); }
    void Attached(const char* key, void*&, perf::AttachedMode)
    {
        OBL_CHECK(Known(std::string(key) + ".sprite"));
        ++checked;
    }
};
template<class T> int CheckKeys(ObjectType type)
{
    T performer{};
    BridgeKeys keys{type};
    VisitFields(performer, keys);
    return keys.checked;
}
} // namespace

OBL_TEST_CASE("performer.field_keys_match_the_save_bridge")
{
    OBL_CHECK_EQ(CheckKeys<Aerobic>(ObjectType::Aerobic), 7);
    OBL_CHECK_EQ(CheckKeys<Cabaret>(ObjectType::Cabaret), 4);
    OBL_CHECK_EQ(CheckKeys<Catwalk>(ObjectType::Catwalk), 7);
    OBL_CHECK_EQ(CheckKeys<Singer>(ObjectType::Singer), 8);
    OBL_CHECK_EQ(CheckKeys<WeightLifter>(ObjectType::WeightLifter), 8);
    OBL_CHECK_EQ(CheckKeys<WetTShirt>(ObjectType::WetTShirt), 8);
}
