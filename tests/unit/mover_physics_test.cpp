#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_physics.h"

#include <cmath>
#include <cstring>

using namespace obl;
namespace {
struct Holder {
    alignas(object::Mover) unsigned char bytes[sizeof(object::Mover)];
    Holder()
    {
        std::memset(bytes, 0, sizeof(bytes));
        obl::unit::StampVtable<object::Mover>(bytes);
        Object().m_mass = 1.0f;
        Object().m_maxVel = 10.0f;
        Object().m_maxForce = 1000.0f;
    }
    object::Mover& Object() { return *reinterpret_cast<object::Mover*>(bytes); }
};
bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}
} // namespace

OBL_TEST_CASE("moving_physics.seek_points_at_the_target_with_max_speed")
{
    Holder h;
    auto& o = h.Object();
    o.m_vTarget[0] = 100;
    moving::SteerSeek(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 10.0f, 0.3f)); // the reciprocal is an estimate: close, not exact
    OBL_CHECK_EQ(o.m_vSteerForce[1], 0.0f);
    o.m_vVelocity[0] = 4;
    moving::SteerSeek(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 6.0f, 0.3f)); // desired velocity minus the current one
}

OBL_TEST_CASE("moving_physics.seek_on_the_target_or_closer_than_a_thousandth_gives_zero_force")
{
    Holder h;
    auto& o = h.Object();
    o.m_vSteerForce[0] = 5;
    o.m_vSteerForce[1] = -3;
    moving::SteerSeek(o);
    OBL_CHECK_EQ(o.m_vSteerForce[0], 0.0f);
    OBL_CHECK_EQ(o.m_vSteerForce[1], 0.0f);
    o.m_vSteerForce[0] = 5;
    o.m_vTarget[0] = 0.0005f; // length 0.0005 <= 0.001
    moving::SteerSeek(o);
    OBL_CHECK_EQ(o.m_vSteerForce[0], 0.0f);
}

OBL_TEST_CASE("moving_physics.force_is_clamped_to_max_force")
{
    Holder h;
    auto& o = h.Object();
    o.m_vTarget[0] = 300;
    o.m_vTarget[1] = 400;
    o.m_maxVel = 100;
    o.m_maxForce = 25;
    moving::SteerSeek(o);
    const float length = std::sqrt(o.m_vSteerForce[0] * o.m_vSteerForce[0] + o.m_vSteerForce[1] * o.m_vSteerForce[1]);
    OBL_CHECK(Near(length, 25.0f, 0.5f));
    OBL_CHECK(o.m_vSteerForce[0] > 0 && o.m_vSteerForce[1] > o.m_vSteerForce[0]); // direction (3, 4) kept
}

OBL_TEST_CASE("moving_physics.arrive_slows_down_inside_the_follow_distance")
{
    Holder h;
    auto& o = h.Object();
    o.m_vTarget[0] = 10;
    o.m_followDistance = 50;
    moving::SteerArrive(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 2.0f, 0.02f)); // length / distance * maxVel = 10 / 50 * 10
    o.m_followDistance = 0; // replaced by 0.2 * maxVel = 2, speed 50 capped to maxVel
    moving::SteerArrive(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 10.0f, 0.05f));
    o.m_followDistance = 0xfffffffdu; // read as signed: a distance, here -3 (a negative speed)
    moving::SteerArrive(o);
    OBL_CHECK(o.m_vSteerForce[0] < 0);
}

OBL_TEST_CASE("moving_physics.follow_does_not_replace_a_zero_distance")
{
    Holder h;
    auto& o = h.Object();
    o.m_vTarget[0] = 10;
    o.m_followDistance = 0; // length / 0 = infinity, capped to maxVel
    moving::SteerFollow(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 10.0f, 0.05f));
    o.m_followDistance = 50;
    moving::SteerFollow(o);
    OBL_CHECK(Near(o.m_vSteerForce[0], 2.0f, 0.02f));
}

OBL_TEST_CASE("moving_physics.integrate_adds_acceleration_and_moves_by_dt_times_velocity")
{
    Holder h;
    auto& o = h.Object();
    o.m_mass = 2;
    o.m_vSteerForce[0] = 4;
    o.m_maxVel = 1000;
    moving::IntegrateMotion(o, 0.5f);
    OBL_CHECK(Near(o.m_vVelocity[0], 2.0f, 0.02f)); // the mass reciprocal is an estimate
    OBL_CHECK_EQ(o.m_vVelocity[1], 0.0f);
    OBL_CHECK(Near(o.m_vPosition[0], 1.0f, 0.02f));
}

OBL_TEST_CASE("moving_physics.integrate_caps_the_speed_and_skips_the_move_for_zero_speed")
{
    Holder h;
    auto& o = h.Object();
    o.m_vVelocity[0] = 30;
    o.m_vVelocity[1] = 40;
    o.m_maxVel = 5;
    moving::IntegrateMotion(o, 1.0f);
    OBL_CHECK(Near(std::sqrt(o.m_vVelocity[0] * o.m_vVelocity[0] + o.m_vVelocity[1] * o.m_vVelocity[1]), 5.0f, 0.05f));
    OBL_CHECK(o.m_vPosition[0] > 2.9f && o.m_vPosition[1] > 3.9f);
    Holder still;
    still.Object().m_vPosition[0] = 7;
    moving::IntegrateMotion(still.Object(), 1.0f);
    OBL_CHECK_EQ(still.Object().m_vPosition[0], 7.0f);
    OBL_CHECK_EQ(still.Object().m_vVelocity[0], 0.0f);
}

OBL_TEST_CASE("moving_physics.zero_dt_leaves_the_position")
{
    Holder h;
    auto& o = h.Object();
    o.m_vVelocity[0] = 3;
    o.m_vPosition[1] = 9;
    moving::IntegrateMotion(o, 0.0f);
    OBL_CHECK_EQ(o.m_vPosition[0], 0.0f);
    OBL_CHECK_EQ(o.m_vPosition[1], 9.0f);
}
