#include "game/objects/object_base.h"
#include "unit_test.h"
#include "object_storage.h"
#include "importer/src/readers/save/save_writer.h"
#include "game/objects/object_hit_test.h"
#include "game/objects/prop.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace obl;
namespace {
using object::Prop;
using object::Scenery;
using ui::SpriteAnim;
using ui::SpriteInstance;
namespace so = prop;
namespace hit = object_hit_test;

template<class T> struct Holder {
    alignas(T) unsigned char bytes[sizeof(T)];
    Holder()
    {
        std::memset(bytes, 0xa5, sizeof(bytes));
        obl::unit::StampVtable<T>(bytes);
    }
    T& Object() { return *reinterpret_cast<T*>(bytes); }
};

struct LifeEnv {
    std::vector<std::string> calls;
    bool BaseReceive(Prop&, const obl::messages::Message&)
    {
        calls.push_back("base");
        return baseAnswer;
    }
    void Post(const obl::messages::Message& message)
    {
        calls.push_back("post");
        posted = message;
    }
    bool baseAnswer = true;
    obl::messages::Message posted{};
};
} // namespace

OBL_TEST_CASE("prop.setters_store_the_low_byte_only")
{
    Holder<Prop> h;
    auto& s = h.Object();
    so::SetGridWidth(s, 0x1ff);
    so::SetGridHeight(s, 7);
    so::SetCleanerClaimed(s, 0x100);
    so::SetMechanicClaimed(s, 1);
    so::SetCalledRepairing(s, 2);
    so::SetUrgentCleaningCalled(s, 0x1234);
    so::SetUrgentRepairCalled(s, 0xff);
    OBL_CHECK_EQ(int(s.m_gridW), 0xff);
    OBL_CHECK_EQ(int(s.m_gridH), 7);
    OBL_CHECK_EQ(int(s.m_cleaning), 0);
    OBL_CHECK_EQ(int(s.m_repairing), 1);
    OBL_CHECK_EQ(int(s.m_calledRepairing), 2);
    OBL_CHECK_EQ(int(s.m_urgentCleaningCalled), 0x34);
    OBL_CHECK_EQ(int(s.m_urgentRepairCalled), 0xff);
    OBL_CHECK_EQ(int(s.m_calledCleaning), 0xa5); // nothing writes the first called-cleaning flag
}

OBL_TEST_CASE("prop.claims_are_the_flag_or_state_four")
{
    Holder<Prop> h;
    auto& s = h.Object();
    s.m_state = 3;
    s.m_cleaning = 0;
    s.m_repairing = 0;
    OBL_CHECK(!so::IsCleanerClaimed(s));
    OBL_CHECK(!so::IsMechanicClaimed(s));
    s.m_cleaning = 2;
    OBL_CHECK(so::IsCleanerClaimed(s));
    OBL_CHECK(!so::IsMechanicClaimed(s));
    s.m_cleaning = 0;
    s.m_state = so::kAlwaysClaimedState;
    OBL_CHECK(so::IsCleanerClaimed(s));
    OBL_CHECK(so::IsMechanicClaimed(s));
}

OBL_TEST_CASE("prop.update_clears_the_debug_byte_and_answers_true")
{
    Holder<Prop> h;
    auto& s = h.Object();
    s.m_debugBreak = 1;
    OBL_CHECK(so::Update(s));
    OBL_CHECK_EQ(int(s.m_debugBreak), 0);
}

OBL_TEST_CASE("prop.receive_drops_one_type_and_forwards_the_rest")
{
    Holder<Prop> h;
    LifeEnv env;
    obl::messages::Message message{};
    message.type = so::kDroppedMessageType;
    OBL_CHECK(!so::ReceiveMessage(h.Object(), message, env));
    OBL_CHECK(env.calls.empty());
    message.type = 3;
    OBL_CHECK(so::ReceiveMessage(h.Object(), message, env));
    env.baseAnswer = false;
    OBL_CHECK(!so::ReceiveMessage(h.Object(), message, env));
    OBL_CHECK_EQ(int(env.calls.size()), 2);
}

OBL_TEST_CASE("prop.construct_clears_the_eight_bytes")
{
    Holder<Prop> h;
    so::Construct(h.Object());
    const auto* bytes = h.bytes;
    for (size_t i = offsetof(Prop, m_gridW); i <= offsetof(Prop, m_urgentRepairCalled); ++i) OBL_CHECK_EQ(int(bytes[i]), 0);
}

OBL_TEST_CASE("prop.vandal_claim_changes_state_and_posts_once")
{
    Holder<Scenery> h;
    auto& s = h.Object();
    SpriteInstance sprite{};
    sprite.m_animIndex = 6;
    s.m_sprite = &sprite;
    s.m_id = 77;
    s.m_vandalised = 0;
    s.m_RepairState = 9;
    s.m_PreVandlismAnim = 5;
    LifeEnv env;
    so::SetVandalClaim(s, 0, env); // no change
    OBL_CHECK(env.calls.empty());
    so::SetVandalClaim(s, 0x101, env);
    OBL_CHECK_EQ(int(s.m_vandalised), 1);
    OBL_CHECK_EQ(s.m_RepairState, 1u);
    OBL_CHECK_EQ(s.m_PreVandlismAnim, 6u);
    OBL_CHECK_EQ(int(env.calls.size()), 1);
    OBL_CHECK_EQ(env.posted.sender, 77);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.type, int(obl::messages::MT_LOOK_AROUND));
    s.m_RepairState = 4;
    s.m_PreVandlismAnim = 8;
    so::SetVandalClaim(s, 0x100, env); // low byte 0: released; the repair state and the remembered animation stay
    OBL_CHECK_EQ(int(s.m_vandalised), 0);
    OBL_CHECK_EQ(s.m_RepairState, 4u);
    OBL_CHECK_EQ(s.m_PreVandlismAnim, 8u);
    OBL_CHECK_EQ(int(env.calls.size()), 2);
}

namespace {
// Every entry of the field table against the converter's Prop bridge table: each key is one the converter maps an old
// label onto.
struct BridgeKeys {
    std::vector<std::string> seen;
    int hashed = 0;
    void Check(const char* key, object::Hashed h)
    {
        OBL_CHECK_EQ(std::string(convert::OldStaticFieldKeyKnown(key) ? "" : key), std::string()); // names the unrecognized key
        seen.emplace_back(key);
        hashed += h == object::Hashed::Yes;
    }
    void Byte(const char* key, uint8_t&, object::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, object::Hashed h) { Check(key, h); }
};
} // namespace

OBL_TEST_CASE("prop.field_table_keys_match_the_converter")
{
    Prop s{};
    BridgeKeys keys;
    object::VisitFields(s, keys);
    OBL_CHECK_EQ(int(keys.seen.size()), 8);
    OBL_CHECK_EQ(keys.hashed, 8); // the snapshot hashes all of them
    // The urgent calls reuse the labels of the two before them; the converter tells them apart by occurrence.
    OBL_CHECK_EQ(convert::OldStaticFieldKey("m_bCalledRepairing", 1), std::string("static.urgent_repairing"));
    OBL_CHECK_EQ(convert::OldStaticFieldKey("m_bCalledRepairing", 2), std::string());
}

namespace {
// A sprite with one 10x20 animation (hotspot y -4) hit-tested by a fake that says yes when the point is the frame's middle.
struct Frame {
    SpriteAnim anims[3];
    SpriteAnim* table[3];
    ui::Sprite sprite{};
    SpriteInstance instance{};
    char names[3][8] = {"idle", "pick", "pick"};
    Frame()
    {
        std::memset(anims, 0, sizeof(anims));
        for (int i = 0; i < 3; ++i) {
            table[i] = &anims[i];
            anims[i].m_name = names[i];
            anims[i].m_width = 10;
            anims[i].m_height = 20;
            anims[i].m_hotspotY = -4;
        }
        anims[2].m_width = 99;
        sprite.m_animCount = 3;
        sprite.m_anims = table;
        instance.m_sprite = &sprite;
        instance.m_animIndex = 0;
        instance.m_frameIndex = 5;
    }
};

struct HitEnv {
    bool selectable = true, clip = true;
    int32_t screen[2] = {100, 200};
    int32_t drawOffset = -5;
    int32_t cursor[2] = {0, 0};
    float zoom = 1.0f;
    int hitX = -1, hitY = -1;
    bool hitAnswer = true;
    bool IsSelectable(const object::Base&) { return selectable; }
    bool ClipObjectRect(const object::Base&) { return clip; }
    void WorldToScreen(const float*, int32_t out[2])
    {
        out[0] = screen[0];
        out[1] = screen[1];
    }
    int32_t DrawOffsetX(const SpriteInstance&) { return drawOffset; }
    void Cursor(int32_t& x, int32_t& y)
    {
        x = cursor[0];
        y = cursor[1];
    }
    float Zoom() { return zoom; }
    bool SpriteHitTest(SpriteInstance&, int x, int y)
    {
        hitX = x;
        hitY = y;
        return hitAnswer;
    }
};

// Frame origin on screen: (100 - 5, 200 - 4) = (95, 196).
struct Fixture {
    Frame frame;
    Holder<object::Base> object;
    HitEnv env;
    Fixture()
    {
        object.Object().m_visible = 1;
        object.Object().m_sprite = &frame.instance;
        object.Object().m_type = object::ObjectType::Guest;
    }
};
} // namespace

OBL_TEST_CASE("hit_test.cursor_is_taken_relative_to_the_frame_rectangle")
{
    Fixture f;
    f.env.cursor[0] = 95 + 3;
    f.env.cursor[1] = 196 + 9;
    OBL_CHECK(hit::HitTestCursor(f.object.Object(), f.env));
    OBL_CHECK_EQ(f.env.hitX, 3);
    OBL_CHECK_EQ(f.env.hitY, 9);
    f.env.hitAnswer = false;
    OBL_CHECK(!hit::HitTestCursor(f.object.Object(), f.env));
}

OBL_TEST_CASE("hit_test.frame_bounds_are_half_open")
{
    Fixture f;
    f.env.cursor[0] = 95 + 9;
    f.env.cursor[1] = 196 + 19;
    OBL_CHECK(hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.env.cursor[0] = 95 + 10;
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.env.cursor[0] = 95 - 1;
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.env.cursor[0] = 95;
    f.env.cursor[1] = 196 - 1;
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.env.cursor[1] = 196 + 20;
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
}

OBL_TEST_CASE("hit_test.cursor_is_divided_by_the_zoom_and_truncated_toward_zero")
{
    Fixture f;
    f.env.zoom = 2.0f;
    f.env.cursor[0] = 2 * (95 + 4) + 1; // 99.5 -> 99
    f.env.cursor[1] = 2 * (196 + 6);
    OBL_CHECK(hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    OBL_CHECK_EQ(f.env.hitX, 4);
    f.env.zoom = 0.5f;
    f.env.cursor[0] = (95 + 2) / 2;
    f.env.cursor[1] = (196 + 2) / 2;
    f.env.screen[0] = 0;
    f.env.screen[1] = 0;
    f.env.drawOffset = 0;
    // The frame origin is now (0, -4): a cursor of 48 maps to 96, far outside.
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.env.cursor[0] = 3; // 6 pixels
    f.env.cursor[1] = 0; // 0: row 4 of the frame
    OBL_CHECK(hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    OBL_CHECK_EQ(f.env.hitX, 6);
    OBL_CHECK_EQ(f.env.hitY, 4);
    f.env.cursor[0] = -1; // -1 / 0.5 = -2: left of the frame
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
}

OBL_TEST_CASE("hit_test.early_outs_precede_any_sprite_access")
{
    Fixture f;
    f.env.cursor[0] = 100;
    f.env.cursor[1] = 200;
    f.env.clip = false;
    OBL_CHECK(!hit::HitTestCursor(f.object.Object(), f.env));
    f.env.clip = true;
    f.object.Object().m_visible = 0;
    OBL_CHECK(!hit::HitTestCursorUnselectable(f.object.Object(), f.env));
    f.object.Object().m_visible = 1;
    f.env.selectable = false;
    OBL_CHECK(!hit::HitTestCursor(f.object.Object(), f.env));
    OBL_CHECK(hit::HitTestCursorUnselectable(f.object.Object(), f.env)); // no selectable check
    OBL_CHECK_EQ(f.env.hitX, 5);
}

OBL_TEST_CASE("hit_test.a_building_switches_to_its_first_pick_animation")
{
    Fixture f;
    f.object.Object().m_type = object::ObjectType::Building;
    f.env.cursor[0] = 95 + 1;
    f.env.cursor[1] = 196 + 1;
    OBL_CHECK(hit::HitTestCursor(f.object.Object(), f.env));
    OBL_CHECK_EQ(f.frame.instance.m_animIndex, 1); // the first "pick", not the second
    OBL_CHECK_EQ(f.frame.instance.m_frameIndex, 0);
    f.frame.instance.m_frameIndex = 7;
    OBL_CHECK(hit::HitTestCursor(f.object.Object(), f.env));
    OBL_CHECK_EQ(f.frame.instance.m_frameIndex, 7); // already on it: the frame is left alone
    // Another class keeps its animation.
    Fixture g;
    g.env.cursor[0] = 96;
    g.env.cursor[1] = 197;
    hit::HitTestCursor(g.object.Object(), g.env);
    OBL_CHECK_EQ(g.frame.instance.m_animIndex, 0);
    // No pick animation: nothing changes.
    Fixture h;
    h.object.Object().m_type = object::ObjectType::Building;
    std::strcpy(h.frame.names[1], "walk");
    std::strcpy(h.frame.names[2], "walk");
    h.env.cursor[0] = 96;
    h.env.cursor[1] = 197;
    hit::HitTestCursor(h.object.Object(), h.env);
    OBL_CHECK_EQ(h.frame.instance.m_animIndex, 0);
    OBL_CHECK_EQ(h.frame.instance.m_frameIndex, 5);
    // The unselectable test never touches the animation.
    Fixture u;
    u.object.Object().m_type = object::ObjectType::Building;
    u.env.cursor[0] = 96;
    u.env.cursor[1] = 197;
    hit::HitTestCursorUnselectable(u.object.Object(), u.env);
    OBL_CHECK_EQ(u.frame.instance.m_animIndex, 0);
}

OBL_TEST_CASE("hit_test.selection_region_rewrites_both_points_and_boxes_the_sprite_point")
{
    HitEnv env;
    env.zoom = 1.0f;
    int32_t sprite[2] = {100, 100};
    int32_t screen[2] = {100, 100};
    OBL_CHECK(hit::HitTestSelectionRegion(sprite, screen, env));
    // Half-width 25: x strictly inside (75, 125); y strictly above 100 + 62.5 and below 100 - 25.
    screen[0] = 125;
    screen[1] = 100;
    OBL_CHECK(!hit::HitTestSelectionRegion(sprite, screen, env));
    screen[0] = 124;
    screen[1] = 162;
    OBL_CHECK(hit::HitTestSelectionRegion(sprite, screen, env));
    screen[0] = 124;
    screen[1] = 163;
    OBL_CHECK(!hit::HitTestSelectionRegion(sprite, screen, env));
    screen[0] = 76;
    screen[1] = 76;
    OBL_CHECK(hit::HitTestSelectionRegion(sprite, screen, env));
    screen[0] = 76;
    screen[1] = 75;
    OBL_CHECK(!hit::HitTestSelectionRegion(sprite, screen, env));
    screen[0] = 75;
    screen[1] = 100;
    OBL_CHECK(!hit::HitTestSelectionRegion(sprite, screen, env));
}

OBL_TEST_CASE("hit_test.selection_region_scales_with_the_zoom")
{
    HitEnv env;
    env.zoom = 2.0f;
    int32_t sprite[2] = {10, 10};
    int32_t screen[2] = {11, 9};
    OBL_CHECK(hit::HitTestSelectionRegion(sprite, screen, env));
    OBL_CHECK_EQ(sprite[0], 20);
    OBL_CHECK_EQ(sprite[1], 20);
    OBL_CHECK_EQ(screen[0], 22);
    OBL_CHECK_EQ(screen[1], 18);
    env.zoom = 0.5f;
    int32_t sprite2[2] = {-9, 9};
    int32_t screen2[2] = {-9, 9};
    OBL_CHECK(hit::HitTestSelectionRegion(sprite2, screen2, env)); // truncated toward zero: (-4, 4), half-width 12
    OBL_CHECK_EQ(sprite2[0], -4);
    OBL_CHECK_EQ(screen2[0], -4);
    // Half-width 12 at zoom 0.5: the lower edge is 12 * 2.5 = 30 below the centre, exclusive.
    int32_t sprite3[2] = {0, 0};
    int32_t screen3[2] = {0, 59}; // 29 after the scaling
    OBL_CHECK(hit::HitTestSelectionRegion(sprite3, screen3, env));
    int32_t screen4[2] = {0, 60}; // 30
    OBL_CHECK(!hit::HitTestSelectionRegion(sprite3, screen4, env));
}
