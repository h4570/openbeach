

#include "unit_test.h"

#include "game/ui/map_camera.h"

#include <cmath>
#include <limits>

using namespace obl;
using namespace obl::map_camera;

namespace {

ui::MapView MakeView()
{
    ui::MapView view{};
    view.m_mapWidth = view.m_mapHeight = 4096;
    view.m_viewWidth = 800;
    view.m_viewHeight = 500;
    view.m_cameraX = view.m_cameraY = 1000;
    view.m_cameraPositionX = view.m_cameraPositionY = 1000.0f;
    return view;
}

Screen MakeScreen(int zoom = 1)
{
    static const float kScale[] = {2.0f, 1.0f, 0.5f};
    return {800, 600, zoom, kScale[zoom], 100};
}

const map_math::Terrain kFlat{4096, 4096, nullptr, 0.0f};
const float kNan = std::numeric_limits<float>::quiet_NaN();

} // namespace

OBL_TEST_CASE("map_camera.clamp_keeps_margins_per_zoom_level")
{
    ui::MapView view = MakeView();
    view.m_cameraX = 10;
    view.m_cameraY = 5000;
    view.m_cameraPositionX = 5000.0f;
    view.m_cameraPositionY = 3.0f;
    Clamp(view, 1);
    OBL_CHECK_EQ(view.m_cameraX, 64);
    OBL_CHECK_EQ(view.m_cameraY, 4096 - 500 - 32);
    OBL_CHECK_EQ(view.m_cameraPositionX, static_cast<float>(4096 - 800 - 64));
    OBL_CHECK_EQ(view.m_cameraPositionY, 32.0f);

    view.m_cameraX = view.m_cameraY = 9000;
    view.m_cameraPositionX = view.m_cameraPositionY = 9000.0f;
    Clamp(view, 0);
    OBL_CHECK_EQ(view.m_cameraX, 4096 - 400 - 64);
    OBL_CHECK_EQ(view.m_cameraY, 4096 - 250 - 32);
    OBL_CHECK_EQ(view.m_cameraPositionY, static_cast<float>(4096 - 250 - 32));
    Clamp(view, 2);
    OBL_CHECK_EQ(view.m_cameraX, 4096 - 1600 - 64);
    OBL_CHECK_EQ(view.m_cameraPositionX, static_cast<float>(4096 - 1600 - 64));
    OBL_CHECK_EQ(view.m_cameraPositionY, static_cast<float>(4096 - 1000 - 32));

    view.m_cameraX = -5;
    Clamp(view, 3);
    OBL_CHECK_EQ(view.m_cameraX, -5);
}

OBL_TEST_CASE("map_camera.clamp_nan_goes_low_and_a_negative_limit_does_not_clamp_floats")
{
    ui::MapView view = MakeView();
    view.m_cameraPositionX = kNan;
    view.m_cameraPositionY = -kNan;
    Clamp(view, 1);
    OBL_CHECK_EQ(view.m_cameraPositionX, 64.0f);
    OBL_CHECK_EQ(view.m_cameraPositionY, 32.0f);

    view.m_mapWidth = 500;
    view.m_cameraX = 300;
    view.m_cameraPositionX = 300.0f;
    Clamp(view, 1);
    OBL_CHECK_EQ(view.m_cameraX, 500 - 800 - 64);
    OBL_CHECK_EQ(view.m_cameraPositionX, 300.0f);
}

OBL_TEST_CASE("map_camera.velocity_is_clamped_and_remembers_real_inputs")
{
    ui::MapView view = MakeView();
    AddVelocity(view, 50.0f, -1e-9f);
    OBL_CHECK_EQ(view.m_lastInputX, 50.0f);
    OBL_CHECK_EQ(view.m_lastInputY, 0.0f);
    AddVelocity(view, 50.0f, kNan);
    OBL_CHECK_EQ(view.m_velocityX, 60.0f);
    OBL_CHECK_EQ(view.m_velocityY, -60.0f);
    OBL_CHECK_EQ(view.m_lastInputY, 0.0f);
    AddVelocity(view, -200.0f, 1.1920929e-07f);
    OBL_CHECK_EQ(view.m_velocityX, -60.0f);
    OBL_CHECK_EQ(view.m_lastInputY, 1.1920929e-07f);
}

OBL_TEST_CASE("map_camera.smoothstep_and_tween")
{
    OBL_CHECK_EQ(SmoothStep(0.0f), 0.0);
    OBL_CHECK_EQ(SmoothStep(1.0f), 1.0);
    OBL_CHECK_EQ(SmoothStep(0.5f), 0.5);

    ui::MapView view = MakeView();
    view.m_mapLocked = view.m_movingMap = 1;
    view.m_moveMapStartTime = 10.0f;
    view.m_moveMapDuration = 2.0f;
    view.m_moveMapStartX = 100;
    view.m_moveMapStartY = 200;
    view.m_moveMapDestX = 300;
    view.m_moveMapDestY = 0;
    UpdateTween(view, 11.0f);
    OBL_CHECK_EQ(view.m_cameraPositionX, 200.0f);
    OBL_CHECK_EQ(view.m_cameraPositionY, 100.0f);
    OBL_CHECK(view.m_movingMap);

    UpdateTween(view, 13.0f);
    OBL_CHECK(!view.m_movingMap);
    OBL_CHECK(!view.m_mapLocked);
    OBL_CHECK_EQ(view.m_cameraPositionX, 100.0f);

    view.m_cameraPositionX = 7.0f;
    UpdateTween(view, 20.0f);
    OBL_CHECK_EQ(view.m_cameraPositionX, 7.0f);
}

OBL_TEST_CASE("map_camera.scroll_jumps_or_starts_a_tween")
{
    ui::MapView view = MakeView();

    OBL_CHECK(ScrollTo(view, MakeScreen(), kFlat, 1000.0f, 2000.0f, 0.0f, 5.0f));
    OBL_CHECK_EQ(view.m_cameraPositionX, 600.0f);
    OBL_CHECK_EQ(view.m_cameraPositionY, 700.0f);
    OBL_CHECK(!view.m_movingMap);
    OBL_CHECK(ScrollTo(view, MakeScreen(), kFlat, 1000.0f, 2000.0f, kNan, 0.0f));

    view.m_cameraPositionX = 1500.5f;
    view.m_velocityX = 3.0f;
    OBL_CHECK(!ScrollTo(view, MakeScreen(), kFlat, 2000.0f, 600.0f, 1.5f, 42.0f));
    OBL_CHECK(view.m_movingMap);
    OBL_CHECK(view.m_mapLocked);
    OBL_CHECK_EQ(view.m_moveMapStartTime, 42.0f);
    OBL_CHECK_EQ(view.m_moveMapDuration, 1.5f);
    OBL_CHECK_EQ(view.m_moveMapStartX, 1500);
    OBL_CHECK_EQ(view.m_moveMapDestX, 1600);
    OBL_CHECK_EQ(view.m_moveMapDestY, 0);
    OBL_CHECK_EQ(view.m_velocityX, 0.0f);
}

OBL_TEST_CASE("map_camera.update_moves_evens_damps_and_stops")
{
    ui::MapView view = MakeView();
    view.m_velocityX = 10.5f;
    view.m_velocityY = -3.0f;
    view.m_lastInputY = -3.0f;
    Frame frame = Update(view, MakeScreen(), 0.0f);
    OBL_CHECK_EQ(view.m_cameraPositionX, 1010.5f);
    OBL_CHECK_EQ(view.m_cameraX, 1010);
    OBL_CHECK_EQ(view.m_cameraY, 998);
    OBL_CHECK_EQ(view.m_velocityX, 5.25f);
    OBL_CHECK_EQ(view.m_velocityY, -3.0f);
    OBL_CHECK(frame.redraw);
    OBL_CHECK(frame.listener);

    OBL_CHECK_EQ(frame.centre[0], 1410);
    OBL_CHECK_EQ(frame.centre[1], 1298);

    view = MakeView();
    frame = Update(view, MakeScreen(0), 0.0f);
    OBL_CHECK_EQ(frame.centre[0], 1200);
    OBL_CHECK(!frame.redraw);

    view = MakeView();
    view.m_velocityX = 0.08f;
    frame = Update(view, MakeScreen(), 0.0f);
    OBL_CHECK_EQ(view.m_velocityX, 0.0f);
    OBL_CHECK(!frame.redraw);

    EndFrame(view);
    OBL_CHECK_EQ(view.m_lastInputX, 0.0f);
}

OBL_TEST_CASE("map_camera.update_locked_runs_only_the_tween")
{
    ui::MapView view = MakeView();
    view.m_mapLocked = 1;
    view.m_velocityX = 20.0f;
    Frame frame = Update(view, MakeScreen(), 0.0f);
    OBL_CHECK(!frame.redraw);
    OBL_CHECK(!frame.listener);
    OBL_CHECK_EQ(view.m_cameraPositionX, 1000.0f);

    view.m_movingMap = 1;
    view.m_moveMapDuration = 1.0f;
    view.m_moveMapStartX = view.m_moveMapDestX = 1001;
    view.m_moveMapStartY = view.m_moveMapDestY = 1003;
    frame = Update(view, MakeScreen(), 0.5f);
    OBL_CHECK(frame.redraw);
    OBL_CHECK(!frame.listener);
    OBL_CHECK_EQ(view.m_cameraX, 1001);
    OBL_CHECK_EQ(view.m_cameraY, 1003);
}

OBL_TEST_CASE("map_camera.setters")
{
    ui::MapView view = MakeView();
    SetPosition(view, 1, 5, 99999);
    OBL_CHECK_EQ(view.m_cameraPositionX, 64.0f);
    OBL_CHECK_EQ(view.m_cameraPositionY, static_cast<float>(4096 - 500 - 32));
    OBL_CHECK_EQ(view.m_cameraX, 1000);

    SetViewportSize(view, MakeScreen(), 1024, 768);
    OBL_CHECK_EQ(view.m_viewWidth, 1024);
    OBL_CHECK_EQ(view.m_viewHeight, 668);
    ResetViewport(view, MakeScreen());
    OBL_CHECK_EQ(view.m_viewHeight, 500);

    view.m_velocityX = 3.0f;
    Stop(view);
    OBL_CHECK_EQ(view.m_velocityX, 0.0f);
    Lock(view);
    OBL_CHECK(view.m_mapLocked);
    Unlock(view);
    OBL_CHECK(!view.m_mapLocked);
}
