#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_selection.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using obl::object::Guest;
using ui::SpriteInstance;
using namespace obl::guest_select;

namespace {

// The game as a script of answers; every call is written to `log` in order.
struct SelectEnv {
    std::string log;
    bool globalKeyline = false;
    uint32_t width = 800, height = 600;
    float zoom = 1.0f;
    int32_t worldX = 1000, worldY = 2000;
    int32_t cameraX = 0, cameraY = 0;
    int32_t screen[2] = {100, 200};
    int32_t offsetX = 5, hotspotY = -7, pixelX = 3, pixelY = 4;
    int32_t tested[2] = {};
    bool hit = true;

    bool GlobalKeyline() { return globalKeyline; }
    void ClearGuestSelection() { add("Clear"); }
    void ShowPanel() { add("Show"); }
    void ClosePanel() { add("Close"); }
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
    void WorldToScreen(const float*, int32_t* out)
    {
        add("ToScreen");
        out[0] = screen[0];
        out[1] = screen[1];
    }
    int32_t DrawOffsetX() { return add("OffsetX"), offsetX; }
    int32_t HotspotY() { return hotspotY; }
    void FirstOpaquePixel(int32_t& x, int32_t& y)
    {
        add("Pixel");
        x = pixelX;
        y = pixelY;
    }
    bool HitTest(const int32_t* point, int32_t*)
    {
        add("Hit");
        tested[0] = point[0];
        tested[1] = point[1];
        return hit;
    }
    void Select() { add("Select"); }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    SpriteInstance sprite;
    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        std::memset(static_cast<void*>(&sprite), 0, sizeof(sprite));
        guest->m_sprite = &sprite;
    }
    Guest& Visitor() { return *guest; }
};

} // namespace

OBL_TEST_CASE("guest_select.reduce_turns_only_a_selected_guest_into_a_hovered_one")
{
    Fixture f;
    for (int32_t level : {0, 1, 3, -1}) {
        f.Visitor().m_SelectLevel = level;
        ReduceSelectionToHover(f.Visitor());
        OBL_CHECK_EQ(f.Visitor().m_SelectLevel, level);
    }
    f.Visitor().m_SelectLevel = Selected;
    ReduceSelectionToHover(f.Visitor());
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, int32_t(Hovered));
}

OBL_TEST_CASE("guest_select.clear_drops_the_outline_unless_everything_is_outlined")
{
    Fixture f;
    f.Visitor().m_SelectLevel = Selected;
    f.sprite.m_keyline = 1;
    ClearSelection(f.Visitor(), true);
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, 0);
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 1);
    ClearSelection(f.Visitor(), false);
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 0);
}

OBL_TEST_CASE("guest_select.hovering_outlines_an_unselected_guest_and_nothing_else")
{
    Fixture f;
    SelectEnv env;
    OnSelect(f.Visitor(), false, true, env);
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, int32_t(Hovered));
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 1);
    OBL_CHECK_EQ(env.log, std::string());
    f.sprite.m_keyline = 0;
    OnSelect(f.Visitor(), false, true, env); // already hovered
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 0);
    f.Visitor().m_SelectLevel = NotSelected;
    env.globalKeyline = true;
    OnSelect(f.Visitor(), false, false, env);
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, int32_t(Hovered));
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 0);
}

OBL_TEST_CASE("guest_select.a_click_opens_the_panel_and_centres_the_camera_only_from_unselected")
{
    Fixture f;
    SelectEnv env;
    env.zoom = 0.5f;
    OnSelect(f.Visitor(), true, true, env);
    OBL_CHECK_EQ(env.log, std::string("Clear Show Forget ToInt Camera Stop"));
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, int32_t(Selected));
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 1);
    OBL_CHECK_EQ(env.cameraX, 1000 - 800); // half of 800 / 0.5
    OBL_CHECK_EQ(env.cameraY, 2000 - 600);

    Fixture plain;
    SelectEnv noCamera;
    OnSelect(plain.Visitor(), true, false, noCamera);
    OBL_CHECK_EQ(noCamera.log, std::string("Clear Show"));

    Fixture hovered;
    hovered.Visitor().m_SelectLevel = Hovered;
    SelectEnv fromHover;
    OnSelect(hovered.Visitor(), true, true, fromHover);
    OBL_CHECK_EQ(fromHover.log, std::string("Clear Show")); // no camera move from a hovered guest
    OBL_CHECK_EQ(hovered.Visitor().m_SelectLevel, int32_t(Selected));
}

OBL_TEST_CASE("guest_select.a_click_on_a_selected_guest_closes_the_panel_and_other_levels_do_nothing")
{
    Fixture f;
    f.Visitor().m_SelectLevel = Selected;
    f.sprite.m_keyline = 1;
    SelectEnv env;
    OnSelect(f.Visitor(), true, true, env);
    OBL_CHECK_EQ(env.log, std::string("Close"));
    OBL_CHECK_EQ(f.Visitor().m_SelectLevel, 0);
    OBL_CHECK_EQ(int(f.sprite.m_keyline), 0);

    for (int32_t level : {3, -1}) {
        Fixture odd;
        odd.Visitor().m_SelectLevel = level;
        SelectEnv none;
        OnSelect(odd.Visitor(), true, true, none);
        OnSelect(odd.Visitor(), false, true, none);
        OBL_CHECK_EQ(none.log, std::string());
        OBL_CHECK_EQ(odd.Visitor().m_SelectLevel, level);
    }
}

OBL_TEST_CASE("guest_select.half_view_divides_the_unsigned_size_by_twice_the_zoom")
{
    OBL_CHECK_EQ(HalfView(800, 1.0f), 400);
    OBL_CHECK_EQ(HalfView(801, 1.0f), 400); // truncated
    OBL_CHECK_EQ(HalfView(800, 2.0f), 200);
    OBL_CHECK_EQ(HalfView(0x80000000u, 1.0f), 0x40000000);
}

OBL_TEST_CASE("guest_select.try_select_tests_the_first_opaque_pixel_of_the_drawn_sprite")
{
    Fixture f;
    SelectEnv env;
    int32_t at[2] = {0, 0};
    OBL_CHECK(TrySelectAtScreenPoint(f.Visitor(), at, env));
    OBL_CHECK_EQ(env.log, std::string("ToScreen OffsetX Pixel Hit Select"));
    OBL_CHECK_EQ(env.tested[0], 100 + 5 + 3);
    OBL_CHECK_EQ(env.tested[1], 200 - 7 + 4);
    SelectEnv miss;
    miss.hit = false;
    OBL_CHECK(!TrySelectAtScreenPoint(f.Visitor(), at, miss));
    OBL_CHECK_EQ(miss.log, std::string("ToScreen OffsetX Pixel Hit"));
}
