#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/world/world_state.h"
#include "unit_test.h"

#include "game/objects/staff_select_draw.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::layout;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Rep;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
using namespace obl::tables;
using namespace obl::staff_draw;

namespace {

// The game as a script of answers; every call is written to `log` in order.
struct Env {
    object::Staff* member = nullptr;
    std::string log;
    uint32_t width = 800, height = 600;
    float zoom = 1.0f;
    int32_t worldX = 1000, worldY = 2000;
    int32_t cameraX = 0, cameraY = 0;
    int32_t screen[2] = {100, 200};
    int32_t offsetX = 5, pixelX = 3, pixelY = 4;
    float markerHappiness = -1.0f;
    int32_t marker[2] = {};
    object::Base* guest = nullptr;
    float time = 12.5f;

    void ForgetGuiSelection() { add("Forget"); }
    void WorldToInt(const float*, int32_t& x, int32_t& y)
    {
        add("ToInt");
        x = worldX;
        y = worldY;
    }
    uint32_t ViewWidth() { return width; }
    uint32_t ViewHeight() { return height; }
    float ZoomScale() { return zoom; }
    void SetCamera(int32_t x, int32_t y)
    {
        add("Camera");
        cameraX = x;
        cameraY = y;
    }
    void StopCamera() { add("Stop"); }
    void ClearStaffSelection() { add("Clear"); }
    void ShowDialog(const char* name) { add(std::string("Show ") + name); }
    void CloseDialog() { add("Close"); }
    void Base(bool clicked, bool center) { add(std::string("Base ") + (clicked ? "1" : "0") + (center ? "1" : "0")); }

    float Time() { return time; }
    void InWorld(SpriteInstance* sprite, const float* world, float t)
    {
        add("World d" + std::to_string(sprite->m_direction) + " x" + std::to_string(unit::TruncToInt32(world[0])) + " y" +
                std::to_string(unit::TruncToInt32(world[1])) + " t" + std::to_string(int(t)));
    }
    object::Base* Object(int32_t id) { return add("Object " + std::to_string(id)), guest; }
    float GuestIllness(object::Base*) { return add("Illness"), 0.25f; }
    float GuestTone(object::Base*) { return add("Tone"), 1.5f; }
    void Toned(SpriteInstance*, const float*, float, float tone, float illness)
    {
        add("Toned " + std::to_string(tone) + " " + std::to_string(illness));
    }

    void WorldToScreen(const float*, int32_t* out)
    {
        add("ToScreen");
        out[0] = screen[0];
        out[1] = screen[1];
    }
    int32_t DrawOffsetX() { return add("OffsetX"), offsetX; }
    void FirstOpaquePixel(int32_t& x, int32_t& y)
    {
        add("Pixel");
        x = pixelX;
        y = pixelY;
    }
    void HappinessMarker(int32_t* point, float happiness)
    {
        add("Marker");
        marker[0] = point[0];
        marker[1] = point[1];
        markerHappiness = happiness;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

// A sprite with one animation of `directions` directions and the hot spot `hotspotY`.
struct Art {
    SpriteAnim anim;
    SpriteAnim* anims[1] = {&anim};
    Sprite sprite;
    SpriteInstance instance;
    Art(int directions, int hotspotY)
    {
        std::memset(static_cast<void*>(&anim), 0, sizeof(anim));
        std::memset(static_cast<void*>(&sprite), 0, sizeof(sprite));
        std::memset(static_cast<void*>(&instance), 0, sizeof(instance));
        anim.m_directions = static_cast<uint8_t>(directions);
        anim.m_hotspotY = static_cast<int16_t>(hotspotY);
        sprite.m_anims = anims;
        instance.m_sprite = &sprite;
    }
};

template<class T> std::unique_ptr<T> Make()
{
    auto object = std::make_unique<T>();
    obl::unit::FillObject<T>(*object);
    return object;
}

} // namespace

OBL_TEST_CASE("staff_draw.base_select_centres_the_camera_only_on_request")
{
    auto member = Make<object::Staff>();
    Env env;
    OnSelect(*member, false, env);
    OBL_CHECK_EQ(env.log, std::string());
    env.zoom = 0.5f;
    OnSelect(*member, true, env);
    OBL_CHECK_EQ(env.log, std::string("Forget ToInt Camera Stop"));
    OBL_CHECK_EQ(env.cameraX, 1000 - 800);
    OBL_CHECK_EQ(env.cameraY, 2000 - 600);
}

OBL_TEST_CASE("staff_draw.profession_select_opens_closes_and_hovers")
{
    auto member = Make<Lifeguard>();
    Env env;
    ProfessionOnSelect(*member, false, true, kLifeguardDialog, env); // hover
    OBL_CHECK_EQ(member->m_SelectLevel, 1);
    OBL_CHECK_EQ(env.log, std::string("Base 01"));
    env.log.clear();
    ProfessionOnSelect(*member, false, false, kLifeguardDialog, env); // already hovered
    OBL_CHECK_EQ(member->m_SelectLevel, 1);
    OBL_CHECK_EQ(env.log, std::string("Base 00"));
    env.log.clear();
    ProfessionOnSelect(*member, true, true, kLifeguardDialog, env); // click opens
    OBL_CHECK_EQ(member->m_SelectLevel, 2);
    OBL_CHECK_EQ(env.log, std::string("Clear Show LifeguardDialog Base 11"));
    env.log.clear();
    ProfessionOnSelect(*member, true, false, kLifeguardDialog, env); // click closes
    OBL_CHECK_EQ(member->m_SelectLevel, 0);
    OBL_CHECK_EQ(env.log, std::string("Close Base 10"));
    env.log.clear();
    for (int32_t level : {3, -1, 258}) {
        member->m_SelectLevel = level;
        ProfessionOnSelect(*member, true, false, kRepDialog, env);
        OBL_CHECK_EQ(member->m_SelectLevel, level);
    }
    OBL_CHECK_EQ(env.log, std::string("Base 10 Base 10 Base 10"));
}

OBL_TEST_CASE("staff_draw.service_draw_skips_a_definition_without_a_sprite")
{
    auto member = Make<obl::object::ServiceStaff>();
    StaffDef definition{};
    Art art(8, 0);
    member->m_staffDefinition = &definition;
    member->m_sprite = &art.instance;
    member->m_WorldXYZ[0] = 10.0f;
    member->m_WorldXYZ[1] = 20.0f;
    Env env;
    definition.m_Sprite = "NONE";
    ServiceDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string());
    definition.m_Sprite = "BARMAN";
    ServiceDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string("World d0 x10 y20 t12"));
}

OBL_TEST_CASE("staff_draw.rep_draw_turns_the_carried_sprite_and_clamps_its_direction")
{
    auto member = Make<Rep>();
    Art body(8, 0), carried(4, 0);
    member->m_sprite = &body.instance;
    member->m_attachedSprite = &carried.instance;
    member->m_WorldXYZ[0] = 100.0f;
    member->m_WorldXYZ[1] = 50.0f;
    member->m_AttachedOffsetX = 10;
    member->m_AttachedOffsetY = -1; // zero-extended: the carried sprite lands far above
    Env env;
    body.instance.m_direction = 2;
    RepDraw(*member, env);
    OBL_CHECK_EQ(carried.instance.m_direction, 2);
    env.log.clear();
    body.instance.m_direction = 6;
    member->m_AttachedOffsetY = 0;
    RepDraw(*member, env);
    OBL_CHECK_EQ(carried.instance.m_direction, 3);
    OBL_CHECK_EQ(env.log, std::string("World d3 x90 y50 t12 World d6 x100 y50 t12"));
    env.log.clear();
    member->m_attachedSprite = nullptr;
    RepDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string("World d6 x100 y50 t12"));
    env.log.clear();
    member->m_insideBuilding = 1;
    RepDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string());
}

OBL_TEST_CASE("staff_draw.lifeguard_draw_puts_the_rescued_guest_between_the_carried_sprite_and_itself")
{
    auto member = Make<Lifeguard>();
    auto guest = Make<object::Base>();
    Art body(8, 0), guestArt(8, 0);
    guest->m_sprite = &guestArt.instance;
    member->m_sprite = &body.instance;
    member->m_saving_id = 77;
    member->m_WorldXYZ[0] = 5.0f;
    member->m_WorldXYZ[1] = 6.0f;
    Env env;
    env.guest = guest.get();
    LifeguardDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string("World d0 x5 y6 t12"));
    env.log.clear();
    member->m_DrawGuest = 1;
    LifeguardDraw(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Object 77 Illness Tone Toned 1.500000 0.250000 World d0 x5 y6 t12"));
}

OBL_TEST_CASE("staff_draw.overlay_marks_a_hovered_or_selected_visible_member")
{
    auto member = Make<object::Staff>();
    Art art(8, -7);
    member->m_sprite = &art.instance;
    member->m_happiness = 0.75f;
    member->m_visible = 1;
    Env env;
    for (int32_t level : {0, 3, -1}) {
        member->m_SelectLevel = level;
        DrawOverlay(*member, env);
    }
    member->m_SelectLevel = 1;
    member->m_visible = 0;
    DrawOverlay(*member, env);
    OBL_CHECK_EQ(env.log, std::string());
    member->m_visible = 1;
    for (int32_t level : {1, 2}) {
        env.log.clear();
        member->m_SelectLevel = level;
        DrawOverlay(*member, env);
        OBL_CHECK_EQ(env.log, std::string("ToScreen OffsetX Pixel Marker"));
        OBL_CHECK_EQ(env.marker[0], 100 + 5 + 3);
        OBL_CHECK_EQ(env.marker[1], 200 - 7 + 4);
        OBL_CHECK(env.markerHappiness == 0.75f);
    }
}
