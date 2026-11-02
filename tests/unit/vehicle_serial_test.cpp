#include "unit_test.h"
#include "byte_copy.h"
#include "game/objects/vehicle_serial.h"
#include "importer/src/readers/save/save_writer.h"
#include "object_storage.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {
using obl::object::Vehicle;
struct Field {
    std::string name;
    char kind;
    size_t bytes;
};
struct Probe {
    std::vector<Field> fields;
    std::vector<void*> sprites;
    std::string suffix;
    template<class T> void Int(const char* n, T&, obl::object::Hashed) { fields.push_back({n, 'i', 4}); }
    template<class T> void UInt(const char* n, T&, obl::object::Hashed) { fields.push_back({n, 'u', 4}); }
    void Float(const char* n, float&, obl::object::Hashed) { fields.push_back({n, 'f', 4}); }
    void Byte(const char* n, uint8_t&, obl::object::Hashed) { fields.push_back({n, 'b', 1}); }
    void Bool(const char* n, uint8_t&, obl::object::Hashed) { fields.push_back({n, '?', 1}); }
    void Ints(const char* n, int32_t*, int count, obl::object::Hashed) { fields.push_back({n, 'a', static_cast<size_t>(count) * 4}); }
    void Sprite(const char*, void* sprite)
    {
        sprites.push_back(sprite);
        OBL_CHECK(fields.back().name == "vehicle.current_circuit_point");
    }
    void Circuit(Vehicle&) { suffix += "circuit "; }
    void Base(Vehicle&) { suffix += "base"; }
};
} // namespace

OBL_TEST_CASE("Vehicle state membership includes only idle and the ten vehicle states")
{
    for (uint32_t i = 0; i < 65536; ++i) OBL_CHECK(obl::vehicle::HasState(i) == (i == 1 || (i >= 65 && i <= 74)));
    OBL_CHECK(!obl::vehicle::HasState(UINT32_MAX));
    OBL_CHECK(!obl::vehicle::HasState(0x80000041u));
}

OBL_TEST_CASE("Vehicle append preserves coordinate bits and touches only the chosen point and count")
{
    Vehicle vehicle;
    obl::unit::FillObject(vehicle, 0xa5);
    vehicle.m_numWayPts = 0;
    // A quiet NaN: an optimizing compiler may quiet a signaling one it folds into the call.
    constexpr uint32_t kValues[] = {0x80000000u, 1, 0x7fc12345u};
    float coordinates[3];
    std::memcpy(coordinates, kValues, sizeof(kValues));
    unsigned char expected[sizeof(vehicle.m_WayPoints)];
    std::memcpy(expected, vehicle.m_WayPoints, sizeof(expected));
    for (uint32_t i = 0; i < 64; ++i) {
        std::memcpy(expected + i * sizeof(kValues), kValues, sizeof(kValues));
        OBL_CHECK(vehicle.AddRouteStop(coordinates[0], coordinates[1], coordinates[2]));
        OBL_CHECK(vehicle.m_numWayPts == i + 1);
        OBL_CHECK(std::memcmp(vehicle.m_WayPoints, expected, sizeof(expected)) == 0);
    }
}

OBL_TEST_CASE("Vehicle serial visits every fixed slot and preserves sprite order")
{
    Vehicle vehicle{};
    void* const pointers[] = {reinterpret_cast<void*>(1), reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
            reinterpret_cast<void*>(4), reinterpret_cast<void*>(5)};
    for (unsigned mask = 0; mask < 32; ++mask) {
        vehicle.m_vehicleSprite = mask & 1 ? pointers[0] : nullptr;
        vehicle.m_doorSprite = mask & 2 ? pointers[1] : nullptr;
        vehicle.m_auxSprite1 = mask & 4 ? pointers[2] : nullptr;
        vehicle.m_auxSprite2 = mask & 8 ? pointers[3] : nullptr;
        vehicle.m_auxSprite3 = mask & 16 ? pointers[4] : nullptr;
        Probe env;
        obl::vehicle::Fields(vehicle, env);
        OBL_REQUIRE(env.fields.size() == 238);
        OBL_CHECK(env.fields[0].name == "vehicle.guests");
        OBL_CHECK(env.fields[0].bytes == 1200);
        OBL_CHECK(env.fields[1].name == "vehicle.guests_to_wait_for");
        OBL_CHECK(env.fields[1].kind == 'i');
        OBL_CHECK(env.fields[5].name == "vehicle.destroy_point");
        OBL_CHECK(env.fields[5].kind == 'b');
        OBL_CHECK(env.fields[7].name == "vehicle.unload_dir");
        OBL_CHECK(env.fields[7].kind == 'u');
        for (int point = 0; point < 64; ++point)
            for (int axis = 0; axis < 3; ++axis) {
                const auto& field = env.fields[10 + point * 3 + axis];
                OBL_CHECK(field.name == std::string("vehicle.way_points[].") + "xyz"[axis]);
                OBL_CHECK(field.kind == 'f');
            }
        OBL_CHECK(env.fields[202].name == "vehicle.way_point_count");
        OBL_CHECK(env.fields[220].name == "vehicle.vehicle_type");
        OBL_CHECK(env.fields.back().name == "vehicle.grid.y");
        std::vector<void*> expected;
        for (unsigned i = 0; i < 5; ++i)
            if (mask & (1u << i)) expected.push_back(pointers[i]);
        OBL_CHECK(env.sprites == expected);
        OBL_CHECK(env.suffix == "circuit base");
    }
}

OBL_TEST_CASE("Vehicle load restores world circuit links only for jetski and speedboat")
{
    struct Query {
        std::vector<int> calls;
        uint32_t CircuitPointCount(int32_t type)
        {
            calls.push_back(100 + type);
            return 21 + type;
        }
        void* CircuitPoints(int32_t type)
        {
            calls.push_back(200 + type);
            return reinterpret_cast<void*>(static_cast<intptr_t>(0x2000 + type));
        }
    };
    for (int type : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 100}) {
        Vehicle vehicle;
        obl::unit::FillObject(vehicle, 0xa5);
        vehicle.m_vehicleType = type;
        Vehicle expected;
        obl::test::ByteCopy(expected, vehicle);
        Query query;
        obl::vehicle::RestoreCircuit(vehicle, query);
        if (type == 0 || type == 1) {
            expected.m_circuitPointCount = 21 + type;
            expected.m_circuitPoints = reinterpret_cast<void*>(static_cast<intptr_t>(0x2000 + type));
            OBL_CHECK(query.calls == std::vector<int>({100 + type, 200 + type}));
        } else
            OBL_CHECK(query.calls.empty());
        OBL_CHECK(std::memcmp(&vehicle, &expected, sizeof(vehicle)) == 0);
    }
}

namespace {
// Checks every field table entry's obl.save key against the converter's bridge table: each is one the converter maps an old
// label onto (the way points and sprites as list elements).
struct BridgeKeys {
    int checked = 0;
    static bool Known(const std::string& key)
    {
        return obl::convert::OldObjectFieldKeyKnown(static_cast<uint8_t>(obl::object::ObjectType::Vehicle), key);
    }
    void Check(const char* key)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
    }
    template<class T> void Int(const char* k, T&, obl::object::Hashed) { Check(k); }
    template<class T> void UInt(const char* k, T&, obl::object::Hashed) { Check(k); }
    void Float(const char* k, float&, obl::object::Hashed) { Check(k); }
    void Byte(const char* k, uint8_t&, obl::object::Hashed) { Check(k); }
    void Bool(const char* k, uint8_t&, obl::object::Hashed) { Check(k); }
    void Ints(const char* k, int32_t*, int, obl::object::Hashed) { Check(k); }
    void Sprite(const char* key, void*)
    {
        OBL_CHECK(Known(std::string(key) + ".dir"));
        ++checked;
    }
};
} // namespace

OBL_TEST_CASE("vehicle.field_keys_match_the_save_bridge")
{
    int sprite = 0;
    Vehicle vehicle{};
    vehicle.m_doorSprite = &sprite;
    BridgeKeys keys;
    obl::object::VisitFields(vehicle, keys);
    OBL_CHECK_EQ(keys.checked, 47 + 3 * Vehicle::kMaxWayPoints);
}
