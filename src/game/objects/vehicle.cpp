#include "game/objects/object_vehicle.h"

#include "core/call_counter.h"
#include "game/objects/vehicle_serial.h"
#include "game/objects/object_scratch.h"

#include <cstring>

// The vehicle's state query and route stops, and their test drivers.
namespace obl::object {
namespace {
test::CallCounter g_state("objects.vehicle-has-state"), g_routeStop("objects.vehicle-route-stop");

// The route driver passes the raw words of the floats, so signalling NaNs reach the logic unchanged.
bool AddRouteStopWords(Vehicle& vehicle, uint32_t x, uint32_t y, uint32_t z)
{
    float values[3];
    std::memcpy(&values[0], &x, 4);
    std::memcpy(&values[1], &y, 4);
    std::memcpy(&values[2], &z, 4);
    return vehicle.AddRouteStop(values[0], values[1], values[2]);
}
void DriveState(int count)
{
    Scratch<Vehicle> scratch; // a bare fixture: the teardown would stop sounds and clear cells
    Vehicle& vehicle = *scratch;
    for (int i = 0; i < count; ++i) {
        for (uint32_t state = 0; state <= 0x100; ++state) vehicle.Vehicle::HasState(state);
        vehicle.Vehicle::HasState(UINT32_MAX);
    }
}
void DriveRoute(int count)
{
    constexpr uint32_t bits[] = {0, 0x80000000u, 1, 0x7f800000u, 0xff800000u, 0x7fc12345u, 0x7f812345u, 0x42f68000u};
    for (int i = 0; i < count; ++i) {
        Scratch<Vehicle> scratch;
        Vehicle& vehicle = *scratch;
        std::memset(vehicle.m_WayPoints, 0xa5, sizeof(vehicle.m_WayPoints));
        vehicle.m_numWayPts = 0;
        for (uint32_t point = 0; point < 64; ++point) {
            AddRouteStopWords(
                    vehicle, bits[point % std::size(bits)], bits[(point + 1) % std::size(bits)], bits[(point + 2) % std::size(bits)]);
        }
    }
}
// `call-drive objects.vehicle-state-driver` / `objects.vehicle-route-driver`: synthetic vehicles through HasState and AddRouteStop.
test::CallCounter g_stateDriver("objects.vehicle-state-driver", &DriveState), g_routeDriver("objects.vehicle-route-driver", &DriveRoute);
} // namespace

bool Vehicle::AddRouteStop(float x, float y, float z)
{
    g_routeStop.Count();
    // The stop copies the raw argument bits; its callers validate the capacity.
    auto& point = m_WayPoints[m_numWayPts];
    std::memcpy(&point[0], &x, sizeof(x));
    std::memcpy(&point[1], &y, sizeof(y));
    std::memcpy(&point[2], &z, sizeof(z));
    ++m_numWayPts;
    return true;
}

bool Vehicle::HasState(uint32_t state)
{
    g_state.Count();
    return vehicle::HasState(state);
}

bool Vehicle::IsSelectable()
{
    return false;
}
} // namespace obl::object
