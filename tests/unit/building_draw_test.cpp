#include "byte_copy.h"
#include "game/objects/object_base.h"
#include "unit_test.h"

#include "game/objects/building_draw.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::building_draw;

namespace {

std::string F(double v)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", v);
    return text;
}

// Every boundary call is logged in order; the queries answer from the fields.
struct FakeEnv {
    std::vector<std::string> log;
    bool building = false, demolishing = false, lightEffects = false, renderLights = false, clip = true;
    float buildProgress = 0, demolishProgress = 0, time = 10, dayTime = 12, lastDayTime = 11, zoomScale = 1;
    int32_t zoomLevel = 1, width = 800, height = 600, cameraX = 100, cameraY = 200, screenX = 50, screenY = 70, drawOffset = 7,
            cameraSetX = 0;
    uint32_t depth = 99;
    bool areaOpen[2] = {false, false};
    SpriteInstance* smoke = nullptr;
    uint8_t buffer[4] = {};
    // The light mask: the screen point follows the world point, the object list and the block size are the test's.
    bool screenFromWorld = false;
    int32_t blockSize = 16;
    std::vector<object::Base*> list;
    std::vector<int32_t> offsetAnims;

    void L(const std::string& s) { log.push_back(s); }
    std::string Joined() const
    {
        std::string all;
        for (const std::string& line : log) all += line + "\n";
        return all;
    }
    std::string Name(SpriteInstance* s) const
    {
        return s == smoke ? "smoke" : "S" + std::to_string(s->m_animIndex) + "." + std::to_string(s->m_direction);
    }

    bool Building(int32_t id)
    {
        L("building " + std::to_string(id));
        return building;
    }
    bool Demolishing(int32_t id)
    {
        L("demolishing " + std::to_string(id));
        return demolishing;
    }
    float BuildProgress(int32_t)
    {
        L("build-progress");
        return buildProgress;
    }
    float DemolishProgress(int32_t)
    {
        L("demolish-progress");
        return demolishProgress;
    }
    float Time() { return time; }
    float DayTime() { return dayTime; }
    float LastDayTime() { return lastDayTime; }
    bool LightEffects() { return lightEffects; }
    void WorldToScreen(int32_t* out, const float* world)
    {
        L("to-screen " + F(world[0]));
        out[0] = screenFromWorld ? static_cast<int32_t>(world[0]) : screenX;
        out[1] = screenFromWorld ? static_cast<int32_t>(world[1]) : screenY;
    }
    uint32_t Depth(const float*)
    {
        L("depth");
        return depth;
    }
    int32_t DrawOffsetX(SpriteInstance* s)
    {
        L("offset");
        offsetAnims.push_back(s->m_animIndex);
        return drawOffset;
    }
    int32_t BlockSize() { return blockSize; }
    // The rect intersect routine: edges that touch overlap; the result is written only on overlap.
    bool Intersect(const screen::Rect& a, const screen::Rect& b, screen::Rect& out)
    {
        if (a.left > b.right || b.left > a.right || a.top > b.bottom || b.top > a.bottom) return false;
        out = {a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top, a.right < b.right ? a.right : b.right,
                a.bottom < b.bottom ? a.bottom : b.bottom};
        return true;
    }
    int32_t BuildingCount() { return static_cast<int32_t>(list.size()); }
    object::Base* BuildingAt(int32_t i) { return i < static_cast<int32_t>(list.size()) ? list[i] : nullptr; }
    void CopyRle(uint8_t*, uint8_t*, int pitch, int x, int y, int w, int h, screen::Rect* clip)
    {
        L("copy " + std::to_string(pitch) + " " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x" +
                std::to_string(h) + " " + std::to_string(clip->left) + "," + std::to_string(clip->top) + "," + std::to_string(clip->right) +
                "," + std::to_string(clip->bottom));
    }
    void ScreenCells(object::Building&) { L("cells"); }
    void LightMask(object::Building&, uint8_t* b) { L(std::string("mask ") + (b == buffer ? "buffer" : "other")); }
    void Bake(SpriteInstance* s, const float*, int a, int c) { L("bake " + Name(s) + " " + std::to_string(a) + "," + std::to_string(c)); }
    void Preview(SpriteInstance* s, const float*, int a, int c, char g)
    {
        L("preview " + Name(s) + " " + std::to_string(a) + "," + std::to_string(c) + " " + std::to_string(g));
    }
    void PreviewAlt(SpriteInstance* s, const float*, int a, int c, float p, char lower)
    {
        L("alt " + Name(s) + " " + std::to_string(a) + "," + std::to_string(c) + " " + F(p) + " " + std::to_string(lower));
    }
    void InWorld(SpriteInstance* s, const float*, float t) { L("in-world " + Name(s) + " " + F(t)); }
    void Clipped(SpriteInstance* s, int x, int y, uint32_t d, float t)
    {
        L("clipped " + Name(s) + " " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(d) + " " + F(t));
    }
    bool ClipRect(int* x, int* y, int* l, int* t, int* w, int* h)
    {
        L("clip " + std::to_string(*x) + "," + std::to_string(*y) + " " + std::to_string(*l) + "," + std::to_string(*t) + " " +
                std::to_string(*w) + "," + std::to_string(*h));
        return clip;
    }
    uint8_t* Frame(ui::SpriteAnim*, int frame, int direction)
    {
        L("frame " + std::to_string(frame) + "," + std::to_string(direction));
        return buffer + 1;
    }
    void Blit(uint8_t*, uint8_t*, uint32_t, int pitch, int x, int y, int w, int h, int l, int t, int cw, int ch)
    {
        L("blit " + std::to_string(pitch) + " " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x" +
                std::to_string(h) + " " + std::to_string(l) + "," + std::to_string(t) + " " + std::to_string(cw) + "x" +
                std::to_string(ch));
    }
    void BlitZoomOut(uint8_t*, uint8_t*, uint32_t, int, int, int, int, int, int, int, int, int) { L("blit-zoom-out"); }
    SpriteInstance* CreateSprite(const char* name)
    {
        L(std::string("create ") + name);
        return smoke;
    }
    void ReleaseSprite(SpriteInstance*) { L("release"); }
    void LightsDirty() { L("dirty"); }
    bool RenderLights() { return renderLights; }
    int32_t ZoomLevel() { return zoomLevel; }
    float ZoomScale() { return zoomScale; }
    int32_t Width() { return width; }
    int32_t Height() { return height; }
    uint8_t* LightBuffer() { return buffer; }
    uint32_t Palette() { return 0x1234; }
    void ForgetKey() { L("forget-key"); }
    void WorldToInt(float*, int32_t* x, int32_t* y)
    {
        L("to-int");
        *x = 1000;
        *y = 500;
    }
    void SetCameraPos(int32_t x, int32_t y) { L("camera " + std::to_string(x) + "," + std::to_string(y)); }
    void StopCamera() { L("stop"); }
    int32_t CameraX() { return cameraX; }
    int32_t CameraY() { return cameraY; }
    void ShowDialog(const char* name, object::Building*) { L(std::string("dialog ") + name); }
    bool AreaOpen(int kind, int area)
    {
        L("open? " + std::to_string(kind) + "," + std::to_string(area));
        return areaOpen[kind];
    }
    void SetAreaOpen(int kind, int area) { L("set-open " + std::to_string(kind) + "," + std::to_string(area)); }
    void SetAreaClosed(int kind, int area) { L("set-closed " + std::to_string(kind) + "," + std::to_string(area)); }
    void PostClass(messages::Message& m, int cls)
    {
        L("class " + std::to_string(cls) + " " + std::to_string(m.sender) + ">" + std::to_string(m.receiver) + " t" +
                std::to_string(m.type) + " f" + std::to_string(m.flags) + " d" + std::to_string(m.data));
    }
    void PostBuildings(messages::Message& m) { L("buildings t" + std::to_string(m.type) + " d" + std::to_string(m.data)); }
    void ClearQueue(void*) { L("clear-queue"); }
    void HideStaff(object::Building&) { L("hide-staff"); }
    void Send(object::Building&, const messages::Message& m)
    {
        L("send t" + std::to_string(m.type) + " d" + std::to_string(m.data) + " f" + std::to_string(m.flags));
    }
};

// A building with sprites: the main one 40x30 (hotspot 3,4), eight anims; the other sprites are made by the cases.
struct Rig {
    object::Building b{};
    tables::BuildingDef def{};
    ui::SpriteAnim mainAnim{}, ghostAnim{}, partAnim{}, signAnim{}, smokeAnim{};
    ui::SpriteAnim *mainAnims[1], *ghostAnims[1], *partAnims[1], *signAnims[1], *smokeAnims[1];
    ui::Sprite mainSprite{}, ghostSprite{}, partSprite{}, signSprite{}, smokeSprite{};
    ui::SpriteInstance main{}, ghost{}, part{}, shortage{}, spanner{}, smoke{};
    FakeEnv env;

    Rig()
    {
        auto setup = [](ui::SpriteAnim& a, ui::SpriteAnim** table, ui::Sprite& s, ui::SpriteInstance& i, uint16_t w, uint16_t h) {
            a.m_width = w;
            a.m_height = h;
            a.m_directions = 8;
            table[0] = &a;
            s.m_anims = table;
            s.m_animCount = 1;
            i.m_sprite = &s;
        };
        setup(mainAnim, mainAnims, mainSprite, main, 40, 30);
        mainAnim.m_hotspotX = 3;
        mainAnim.m_hotspotY = 4;
        setup(ghostAnim, ghostAnims, ghostSprite, ghost, 40, 30);
        setup(partAnim, partAnims, partSprite, part, 10, 8);
        partAnim.m_hotspotX = 5;
        partAnim.m_hotspotY = -2;
        setup(signAnim, signAnims, signSprite, shortage, 16, 12);
        spanner = shortage;
        setup(smokeAnim, smokeAnims, smokeSprite, smoke, 20, 30);
        b.m_definition = &def;
        b.m_sprite = &main;
        b.m_constructionSprite = &ghost;
        b.m_shortageSprite = &shortage;
        b.m_spannerSprite = &spanner;
        b.m_animSprites[0] = &part;
        b.m_id = 7;
        b.m_WorldXYZ[0] = 1;
        b.m_WorldXYZ[1] = 2;
        b.m_WorldXYZ[2] = 3;
        b.m_TopWorldPos[0] = 4;
        b.m_lightMapOffset = -1;
        b.m_shadowOffset = -1;
        def.m_GridWidth = 3;
        def.m_GridHeight = 5;
        env.smoke = &smoke;
        std::strcpy(b.m_Name, "BAR");
        def.m_Behavior = BuildingBehavior::Bar;
    }
    std::string Log() const
    {
        std::string all;
        for (const std::string& line : env.log) all += line + "\n";
        return all;
    }
};

bool Has(const std::string& log, const std::string& what)
{
    return log.find(what) != std::string::npos;
}

} // namespace

OBL_TEST_CASE("building_draw.is_beach_open_mode_truth_table")
{
    FakeEnv env;
    for (int first = 0; first < 2; ++first)
        for (int second = 0; second < 2; ++second) {
            env.areaOpen[0] = first;
            env.areaOpen[1] = second;
            OBL_CHECK_EQ(IsBeachOpenMode(env, 3, 0), uint32_t(first && second));
            OBL_CHECK_EQ(IsBeachOpenMode(env, 3, 1), uint32_t(first && !second));
            OBL_CHECK_EQ(IsBeachOpenMode(env, 3, 2), uint32_t(!first && !second));
        }
    // Any other mode answers (mode - 2) & ~0xff and asks the grid nothing.
    env.log.clear();
    OBL_CHECK_EQ(IsBeachOpenMode(env, 3, 5), 0u);
    OBL_CHECK_EQ(IsBeachOpenMode(env, 3, 258), 0x100u);
    OBL_CHECK_EQ(IsBeachOpenMode(env, 3, -1), 0xffffff00u);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("building_draw.is_beach_open_mode_asks_in_order")
{
    FakeEnv env;
    env.areaOpen[0] = false;
    IsBeachOpenMode(env, 9, 0);
    OBL_CHECK_EQ(env.log.size(), size_t(1)); // the second area is not asked when the first is closed
    env.log.clear();
    env.areaOpen[0] = true;
    IsBeachOpenMode(env, 9, 2);
    OBL_CHECK_EQ(env.log.size(), size_t(1)); // mode 2 stops at an open first area
}

OBL_TEST_CASE("building_draw.set_beach_open_mode_grid_and_messages")
{
    Rig rig;
    rig.b.m_InfluenceArea = 12;
    SetBeachOpenMode(rig.env, rig.b, 0);
    OBL_CHECK_EQ(rig.Log(), "set-open 0,12\nset-open 1,12\nclass 3 7>7 t75 f1 d12\nbuildings t47 d0\n");
    rig.env.log.clear();
    SetBeachOpenMode(rig.env, rig.b, 1);
    OBL_CHECK_EQ(rig.Log(), "set-open 0,12\nset-closed 1,12\nclass 3 7>7 t75 f0 d12\nbuildings t47 d0\n");
    rig.env.log.clear();
    SetBeachOpenMode(rig.env, rig.b, 2);
    OBL_CHECK_EQ(rig.Log(), "set-closed 0,12\nset-closed 1,12\nclass 3 7>7 t75 f0 d12\nbuildings t47 d0\n");
    rig.env.log.clear();
    SetBeachOpenMode(rig.env, rig.b, 9); // no grid change, the messages still go
    OBL_CHECK_EQ(rig.Log(), "class 3 7>7 t75 f0 d12\nbuildings t47 d0\n");
}

OBL_TEST_CASE("building_draw.begin_demolish_closes_clears_hides_and_sends_state_2")
{
    Rig rig;
    rig.b.m_OpenStatus = 0;
    BeginDemolish(rig.env, rig.b);
    OBL_CHECK_EQ(rig.b.m_OpenStatus, 1u);
    OBL_CHECK_EQ(rig.Log(), "clear-queue\nhide-staff\nsend t3 d2 f0\n");
}

OBL_TEST_CASE("building_draw.on_select_dialogs_and_levels")
{
    Rig rig;
    rig.def.m_Menu = "HutMenu";
    rig.b.m_SelectLevel = 0;
    OnSelect(rig.env, rig.b, true, false);
    OBL_CHECK_EQ(rig.Log(), "building 7\ndemolishing 7\ndialog HutMenu\n");
    OBL_CHECK_EQ(rig.b.m_SelectLevel, 2);
    rig.env.log.clear();
    rig.env.demolishing = true;
    OnSelect(rig.env, rig.b, true, false);
    OBL_CHECK_EQ(rig.Log(), "building 7\ndemolishing 7\ndialog DemolishBuilding\n");
    OBL_CHECK_EQ(rig.b.m_SelectLevel, 1);
    rig.env.log.clear();
    rig.env.building = true;
    rig.b.m_SelectLevel = 0;
    OnSelect(rig.env, rig.b, true, false);
    OBL_CHECK_EQ(rig.Log(), "building 7\ndialog ConstructBuilding\n"); // demolition is not asked once it is a construction site
    OBL_CHECK_EQ(rig.b.m_SelectLevel, 1);
    rig.env.log.clear();
    rig.b.m_SelectLevel = 5;
    OnSelect(rig.env, rig.b, false, false); // neither: nothing happens
    OBL_CHECK(rig.env.log.empty());
    OBL_CHECK_EQ(rig.b.m_SelectLevel, 5);
}

OBL_TEST_CASE("building_draw.on_select_centers_the_camera")
{
    Rig rig;
    OnSelect(rig.env, rig.b, false, true);
    // The view is 800x600 at zoom 1: half is 400 x 300 (truncated), the world point maps to (1000, 500).
    OBL_CHECK_EQ(rig.Log(), "forget-key\nto-int\ncamera 600,200\nstop\n");
    rig.env.log.clear();
    rig.env.zoomScale = 0.5f;
    rig.env.width = 801;
    OnSelect(rig.env, rig.b, false, true);
    OBL_CHECK_EQ(rig.Log(), "forget-key\nto-int\ncamera 199,-100\nstop\n"); // 801 / 1.0 = 801, 600 / 1.0 = 600
}

OBL_TEST_CASE("building_draw.update_screen_rect")
{
    Rig rig;
    screen::Rect* rect = UpdateScreenRect(rig.env, rig.b);
    OBL_CHECK(rect == &rig.b.m_rScrRect);
    // screen (50, 70) + draw offset 7 + camera (100, 200); the hotspot Y is 4
    OBL_CHECK_EQ(rect->left, 157);
    OBL_CHECK_EQ(rect->top, 274);
    OBL_CHECK_EQ(rect->right, 197);
    OBL_CHECK_EQ(rect->bottom, 304);
    OBL_CHECK_EQ(rig.Log(), "to-screen 1\noffset\n");
}

OBL_TEST_CASE("building_draw.overlay_shortage_spanner_and_nothing")
{
    Rig rig;
    DrawOverlay(rig.env, rig.b);
    OBL_CHECK_EQ(rig.Log(), "demolishing 7\n");
    rig.env.log.clear();
    rig.b.m_closedForPower = 1;
    rig.env.zoomScale = 2;
    DrawOverlay(rig.env, rig.b);
    // the point (50, 70) at zoom 2 is (100, 140); the shortage sprite is 16x12: x - 8, y - 24
    OBL_CHECK_EQ(rig.Log(), "demolishing 7\nto-screen 4\nclipped S0.0 92,116 0 10\n");
    rig.env.log.clear();
    rig.b.m_closedForPower = 0;
    rig.b.m_RepairState = 3;
    rig.spanner.m_direction = 5;
    DrawOverlay(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "clipped S0.5 92,116 0 10"));
    rig.env.log.clear();
    rig.env.demolishing = true;
    DrawOverlay(rig.env, rig.b);
    OBL_CHECK_EQ(rig.Log(), "demolishing 7\n");
}

OBL_TEST_CASE("building_draw.draw_swaps_the_footprint_for_odd_directions")
{
    Rig rig;
    rig.b.m_burn = 1;
    rig.main.m_direction = 1;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "bake S0.1 5,3\npreview S0.1 5,3 0\n"));
    OBL_CHECK_EQ(rig.b.m_burn, 0);
    OBL_CHECK(Has(rig.Log(), "cells")); // the screen cells are refreshed first
}

OBL_TEST_CASE("building_draw.draw_burn_limbo_returns_the_building_when_the_time_is_up")
{
    Rig rig;
    rig.b.m_burnLimbo = 1;
    rig.b.m_timeInLimbo = 10; // equal to the time: not yet
    Draw(rig.env, rig.b);
    OBL_CHECK_EQ(rig.b.m_burnLimbo, 1);
    OBL_CHECK_EQ(rig.b.m_burn, 0);
    rig.b.m_timeInLimbo = 9.5f;
    Draw(rig.env, rig.b);
    OBL_CHECK_EQ(rig.b.m_burnLimbo, 0);
    OBL_CHECK_EQ(rig.b.m_burn, 1);
    OBL_CHECK_EQ(rig.b.m_timeInLimbo, 0.0f);
}

OBL_TEST_CASE("building_draw.draw_progress_previews")
{
    Rig rig;
    rig.env.building = true;
    rig.env.buildProgress = 0.25f;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "alt S0.0 3,5 0.25 1\n"));
    rig.env.log.clear();
    rig.env.building = false;
    rig.env.demolishing = true;
    rig.env.demolishProgress = 0.5f;
    Draw(rig.env, rig.b);
    // (1 - 0.5) - 0.0001f in double, rounded to a float once
    OBL_CHECK(Has(
            rig.Log(), "demolish-progress\nalt S0.0 3,5 " + F(static_cast<float>((1.0 - 0.5) - static_cast<double>(0.0001f))) + " 1\n"));
}

OBL_TEST_CASE("building_draw.draw_custom_construction_scales_by_the_height_ratio")
{
    Rig rig;
    rig.def.m_customConstruct = 1;
    rig.ghostAnim.m_height = 60; // the ghost is twice as tall
    rig.env.building = true;
    rig.env.buildProgress = 0.5f;
    rig.main.m_direction = 2;
    Draw(rig.env, rig.b);
    // build, ghost taller: the ghost gets ratio(main/ghost) * progress and the main sprite the progress itself
    OBL_CHECK(Has(rig.Log(), "alt S0.2 3,5 0.25 0\nalt S0.2 3,5 0.5 1\n"));
    rig.env.log.clear();
    rig.ghostAnim.m_height = 15; // the ghost is lower: it gets the progress, the main sprite the float ratio * progress
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "alt S0.2 3,5 0.5 0\nalt S0.2 3,5 0.25 1\n"));
    rig.env.log.clear();
    rig.env.building = false;
    rig.env.demolishing = true;
    rig.env.demolishProgress = 0.25f;
    rig.ghostAnim.m_height = 60;
    Draw(rig.env, rig.b);
    // demolition, ghost taller: the ghost gets 1 - progress and the main sprite (1 - progress) * ratio(ghost/main)
    OBL_CHECK(Has(rig.Log(), "alt S0.2 3,5 0.75 0\nalt S0.2 3,5 1.5 1\n"));
}

OBL_TEST_CASE("building_draw.draw_clamps_the_ghost_direction")
{
    Rig rig;
    rig.def.m_customConstruct = 1;
    rig.ghostAnim.m_directions = 3;
    rig.env.building = true;
    rig.main.m_direction = 6;
    Draw(rig.env, rig.b);
    OBL_CHECK_EQ(rig.ghost.m_direction, 2);
    rig.ghostAnim.m_directions = 8;
    rig.main.m_direction = 6;
    Draw(rig.env, rig.b);
    OBL_CHECK_EQ(rig.ghost.m_direction, 6);
}

OBL_TEST_CASE("building_draw.draw_light_map_and_dirty_flag")
{
    Rig rig;
    rig.env.lightEffects = true;
    rig.env.renderLights = true;
    rig.b.m_lightMapOffset = 0;
    rig.b.m_lightsOn = 18;
    rig.b.m_lightsOff = 6;
    rig.env.dayTime = 12; // between off and on: no light, the building is lit by day; the shadow branch runs (no shadow anim here)
    rig.env.lastDayTime = 11.9f;
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "dirty"));
    OBL_CHECK(Has(rig.Log(), "mask buffer"));
    OBL_CHECK(!Has(rig.Log(), "blit"));
    rig.env.log.clear();
    rig.env.dayTime = 19; // the lights are on: the light map is blitted
    rig.env.lastDayTime = 17.9f;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "dirty"));
    OBL_CHECK(Has(rig.Log(), "clip 57,74 0,0 40,30"));
    OBL_CHECK(Has(rig.Log(), "blit 800 57,74 40x30 0,0 40x30"));
    rig.env.log.clear();
    rig.env.zoomLevel = 2; // zoomed out: no mask and the zoom-out blit
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "mask"));
    OBL_CHECK(Has(rig.Log(), "blit-zoom-out"));
    rig.env.log.clear();
    rig.env.zoomLevel = 3; // no blit at all
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "blit"));
}

OBL_TEST_CASE("building_draw.draw_shadow_animation_when_closed")
{
    Rig rig;
    rig.env.lightEffects = true;
    rig.b.m_lightMapOffset = 0;
    rig.b.m_shadowOffset = 0;
    rig.b.m_OpenStatus = 1;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "in-world S0.0 10"));
}

OBL_TEST_CASE("building_draw.draw_anim_sprites_use_the_hotspot_of_the_direction")
{
    Rig rig;
    rig.def.m_NumAnimSprites = 1;
    rig.def.m_AnimSprites[0].m_X[0] = 11;
    rig.def.m_AnimSprites[0].m_Y[0] = 12;
    rig.def.m_AnimSprites[0].m_X[1] = 21;
    rig.def.m_AnimSprites[0].m_Y[1] = 22;
    rig.def.m_AnimSprites[0].m_animated = 1;
    rig.b.m_buildingAnimTime = 3;
    rig.main.m_direction = 0;
    Draw(rig.env, rig.b);
    // x = 11 + 50 + the part's hotspot X 5, y = 12 + 70 + its hotspot Y -2; the flag selects the world time
    OBL_CHECK(Has(rig.Log(), "depth\nclipped S0.0 66,80 99 10\n"));
    OBL_CHECK_EQ(rig.b.m_buildingAnimTime, 10.0f); // open: the clock of the animation follows the world
    rig.env.log.clear();
    rig.main.m_direction = 1;
    rig.def.m_AnimSprites[0].m_animated = 0;
    Draw(rig.env, rig.b);
    // odd direction: 5 - 2 * 3 - 40 = -41 as the shift
    OBL_CHECK(Has(rig.Log(), "clipped S0.0 30,90 99 10\n"));
    rig.env.log.clear();
    rig.b.m_OpenStatus = 1;
    rig.b.m_buildingAnimTime = 4;
    rig.partSprite.m_type = 12; // an effect sprite: drawn in the world with the animation clock
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "in-world S0.0 4\n"));
    OBL_CHECK_EQ(rig.b.m_buildingAnimTime, 4.0f);
}

OBL_TEST_CASE("building_draw.draw_anim_sprite_with_a_far_direction_reads_the_following_fields")
{
    tables::BuildingDef def{};
    def.m_AnimSprites[1].m_X[0] = 77;
    // direction 5 of entry 0 is entry 0's m_Y[1]; entry 1 direction 0 is its own m_X[0]
    def.m_AnimSprites[0].m_Y[1] = 55;
    OBL_CHECK_EQ(AnimSpriteInt(def, false, 0, 5), 55);
    OBL_CHECK_EQ(AnimSpriteInt(def, false, 1, 0), 77);
}

OBL_TEST_CASE("building_draw.draw_loo_smoke_and_flies")
{
    Rig rig;
    rig.def.m_Behavior = BuildingBehavior::Toilet;
    rig.b.m_VisitsSinceCleaned = 0x14;
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "create"));
    rig.env.log.clear();
    rig.b.m_VisitsSinceCleaned = 0x15;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "create fly\nin-world smoke 10\nrelease\n"));
    OBL_CHECK(!Has(rig.Log(), "create test_smoke"));
    rig.env.log.clear();
    rig.b.m_VisitsSinceCleaned = 0x1f;
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "create test_smoke\nin-world smoke 10\nrelease\ncreate fly\n"));
    rig.env.log.clear();
    rig.b.m_VisitsSinceCleaned = -1; // unsigned compare
    Draw(rig.env, rig.b);
    OBL_CHECK(Has(rig.Log(), "create test_smoke"));
    rig.env.log.clear();
    rig.def.m_Behavior = BuildingBehavior::Bar;
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "create"));
}

OBL_TEST_CASE("building_draw.draw_broken_building_smokes_above_the_top_point")
{
    Rig rig;
    rig.b.m_RepairState = 3;
    Draw(rig.env, rig.b);
    // the smoke sprite is 20x30: x - 10, y - 30, depth 0xffff - 30 * 16
    OBL_CHECK(Has(rig.Log(), "create smoke\nclipped smoke 40,40 65055 10\nrelease\n"));
    rig.env.log.clear();
    rig.env.demolishing = true;
    Draw(rig.env, rig.b);
    OBL_CHECK(!Has(rig.Log(), "create smoke"));
}

// ---- UpdateScreenCells and DrawLightMask --------------------------------------------------------------------------------------------------------

namespace {

// A sprite with three animations: 0 the building (40x30, hotspot y 4), 1 the pick mask (50x40) and 2 the light map (60x30).
struct LightSprite {
    ui::SpriteAnim anims[3] = {};
    ui::SpriteAnim* table[3] = {};
    ui::Sprite sprite{};
    ui::SpriteInstance instance{};
    LightSprite()
    {
        const uint16_t sizes[3][2] = {{40, 30}, {50, 40}, {60, 30}};
        for (int i = 0; i < 3; ++i) {
            anims[i].m_width = sizes[i][0];
            anims[i].m_height = sizes[i][1];
            table[i] = &anims[i];
        }
        anims[0].m_hotspotY = 4;
        sprite.m_anims = table;
        sprite.m_animCount = 3;
        instance.m_sprite = &sprite;
    }
};

struct LightBuilding {
    object::Building object{};
    LightSprite sprite;
    LightBuilding(float x, float y)
    {
        object.m_sprite = &sprite.instance;
        object.m_WorldXYZ[0] = x;
        object.m_WorldXYZ[1] = y;
        object.m_pickOffset = 1;
        object.m_lightMapOffset = 2;
        object.m_lightsOn = 18;
        object.m_lightsOff = 6;
    }
};

} // namespace

OBL_TEST_CASE("building_draw.update_screen_cells_uses_animation_zero_and_gives_it_back")
{
    FakeEnv env;
    env.blockSize = 16;
    LightBuilding b(1, 2);
    b.sprite.instance.m_animIndex = 1;
    b.sprite.instance.m_frameIndex = 3;
    UpdateScreenCells(env, b.object, 1, 2, 3);
    // screen (50,70) + draw offset 7 / hotspot 4 of the animation 0 + camera (100,200): (157,274); the size is the one of the restored animation 1.
    OBL_CHECK_EQ(env.offsetAnims.size(), size_t(1));
    OBL_CHECK_EQ(env.offsetAnims[0], 0);
    OBL_CHECK_EQ(b.object.m_rScrExtents.left, 157 / 16);
    OBL_CHECK_EQ(b.object.m_rScrExtents.top, 274 / 16);
    OBL_CHECK_EQ(b.object.m_rScrExtents.right, (50 + 157) / 16 + 1);
    OBL_CHECK_EQ(b.object.m_rScrExtents.bottom, (40 + 274) / 16 + 1);
    OBL_CHECK_EQ(b.sprite.instance.m_animIndex, 1);
    OBL_CHECK_EQ(b.sprite.instance.m_frameIndex, 0);
    // Already on the animation 0: the frame is left alone.
    b.sprite.instance.m_animIndex = 0;
    b.sprite.instance.m_frameIndex = 5;
    UpdateScreenCells(env, b.object, 1, 2, 3);
    OBL_CHECK_EQ(b.sprite.instance.m_frameIndex, 5);
    OBL_CHECK_EQ(b.object.m_rScrExtents.right, (40 + 157) / 16 + 1);
    // A negative position divides toward zero.
    env.screenX = -300;
    UpdateScreenCells(env, b.object, 1, 2, 3);
    OBL_CHECK_EQ(b.object.m_rScrExtents.left, (-300 + 7 + 100) / 16);
}

namespace {

// Camera at the origin, the screen point is the world point: `this` covers (100,100)-(150,140) with its pick mask, the lit one (120,110)-(180,140).
struct MaskScene {
    FakeEnv env;
    LightBuilding lit{120, 110}, masked{100, 100}, later{110, 105};
    MaskScene()
    {
        env.screenFromWorld = true;
        env.cameraX = env.cameraY = 0;
        env.drawOffset = 0;
        env.dayTime = 3;
        env.list = {&lit.object, &masked.object, &later.object};
        lit.sprite.anims[2].m_hotspotY = 0;
        masked.sprite.anims[1].m_hotspotY = 0;
    }
    void Run() { DrawLightMask(env, masked.object, env.buffer); }
};

} // namespace

OBL_TEST_CASE("building_draw.light_mask_copies_the_overlap_of_buildings_listed_before")
{
    MaskScene scene;
    scene.Run();
    const std::string log = scene.env.Joined();
    // Only the building before `this` is walked (the one after is not), its light map animation is restored.
    OBL_CHECK(Has(log, "frame 0,0\ncopy 800 120,110 50x40 20,10,50,40\n"));
    OBL_CHECK_EQ(scene.env.offsetAnims.size(), size_t(2));
    OBL_CHECK_EQ(scene.lit.sprite.instance.m_animIndex, 0);
    OBL_CHECK_EQ(scene.later.sprite.instance.m_animIndex, 0);
    OBL_CHECK_EQ(scene.masked.sprite.instance.m_animIndex, 1); // `this` stays on its pick mask
}

OBL_TEST_CASE("building_draw.light_mask_skips_buildings_that_do_not_cast_light")
{
    {
        MaskScene scene;
        scene.lit.object.m_OpenStatus = 1;
        scene.Run();
        OBL_CHECK(!Has(scene.env.Joined(), "copy"));
        OBL_CHECK_EQ(scene.env.offsetAnims.size(), size_t(1));
    }
    {
        MaskScene scene;
        scene.lit.object.m_lightMapOffset = -1;
        scene.Run();
        OBL_CHECK(!Has(scene.env.Joined(), "copy"));
    }
    {
        // Between the switch-on and switch-off times of the lights (day 12, on 18, off 6) the sun does the lighting.
        MaskScene scene;
        scene.env.dayTime = 12;
        scene.Run();
        OBL_CHECK(!Has(scene.env.Joined(), "copy"));
        scene.env.dayTime = 3;
        scene.env.log.clear();
        scene.Run();
        OBL_CHECK(Has(scene.env.Joined(), "copy"));
        scene.env.dayTime = 19;
        scene.env.log.clear();
        scene.lit.sprite.instance.m_animIndex = 0;
        scene.Run();
        OBL_CHECK(Has(scene.env.Joined(), "copy"));
    }
    {
        // A building that is not in the list walks all of them.
        MaskScene scene;
        object::Building outsider;
        obl::test::ByteCopy(outsider, scene.masked.object);
        scene.env.list = {&scene.lit.object, &scene.later.object};
        DrawLightMask(scene.env, outsider, scene.env.buffer);
        const std::string log = scene.env.Joined();
        OBL_CHECK(Has(log, "copy"));
    }
}

OBL_TEST_CASE("building_draw.light_mask_clips_to_the_buffer_and_leaves_the_sprite_when_nothing_is_copied")
{
    {
        MaskScene scene;
        scene.env.width = 130; // the right edge of the copy is cut at the buffer's edge
        scene.Run();
        OBL_CHECK(Has(scene.env.Joined(), "copy 130 120,110 50x40 20,10,30,40\n"));
        OBL_CHECK_EQ(scene.lit.sprite.instance.m_animIndex, 0);
    }
    {
        MaskScene scene;
        scene.env.width =
                100; // the overlap starts to the right of the buffer: nothing is copied and the sprite keeps the light map animation
        scene.Run();
        OBL_CHECK(!Has(scene.env.Joined(), "copy"));
        OBL_CHECK_EQ(scene.lit.sprite.instance.m_animIndex, 2);
    }
    {
        MaskScene scene;
        scene.masked.object.m_WorldXYZ[0] = -30; // the overlap starts left of the buffer: the copy starts at its edge
        scene.lit.object.m_WorldXYZ[0] = -10;
        scene.Run();
        OBL_CHECK(Has(scene.env.Joined(), "copy 800 0,110 50x40 30,10,50,40\n"));
    }
    {
        MaskScene scene; // `this` outside the view: no building is looked at
        scene.env.width = 50;
        scene.env.height = 50;
        scene.Run();
        OBL_CHECK_EQ(scene.env.offsetAnims.size(), size_t(1));
    }
}
