#include "unit_test.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "byte_copy.h"
#include "core/char16.h"
#include "importer/src/readers/save/save_writer.h"
#include "game/objects/staff_serial.h"

#include <algorithm>
#include <string>

namespace {
using namespace obl::unit;

// Every entry of Staff's field table against the converter's bridge table (the staff group every profession ends with): each
// key is one the converter maps an old label onto.
struct StaffBridgeKeys {
    obl::object::ObjectType type = obl::object::ObjectType::ServiceStaff;
    int checked = 0;
    int hashed = 0;
    bool Known(const std::string& key) const { return obl::convert::OldObjectFieldKeyKnown(static_cast<uint8_t>(type), key); }
    void Check(const char* key, obl::object::Hashed h)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unknown key
        ++checked;
        hashed += h == obl::object::Hashed::Yes;
    }
    template<class T> void Int(const char* key, T&, obl::object::Hashed h) { Check(key, h); }
    template<class T> void UInt(const char* key, T&, obl::object::Hashed h) { Check(key, h); }
    void Float(const char* key, float&, obl::object::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, obl::object::Hashed h) { Check(key, h); }
    void Wide(const char* key, obl::Char16*, int capacity, obl::object::Hashed h)
    {
        OBL_CHECK_EQ(capacity, 256);
        Check(key, h);
    }
    // The professions' entries (Mechanic, Rep, SecurityGuard).
    void Counter(const char* key, obl::object::Hashed h) { Check(key, h); }
    void MaskBit(const char* key, uint32_t&, int, obl::object::Hashed h) { Check(key, h); }
    void Ints(const char* key, int32_t*, int, obl::object::Hashed h) { Check(key, h); }
    void Text(const char* key, char*, int capacity, obl::object::Hashed h)
    {
        OBL_CHECK_EQ(capacity, 40);
        Check(key, h);
    }
    void BeachArea(const obl::map::Area*&, int32_t) {}
    // The presence flag is dropped by the converter; the sprite's name is the group's first record.
    void Attached(const char* key, void*&, obl::object::Hashed h)
    {
        OBL_CHECK(!Known(key));
        OBL_CHECK(Known(std::string(key) + ".sprite"));
        ++checked;
        hashed += h == obl::object::Hashed::Yes;
    }
};
} // namespace

OBL_TEST_CASE("staff_serial.field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys;
    obl::object::Staff staff{};
    obl::object::VisitFields(staff, keys);
    OBL_CHECK_EQ(keys.checked, 29);
    OBL_CHECK_EQ(keys.hashed, 9); // home, wage, happiness, salary expectation, start, end, hired on, sacked, ended shift
}

OBL_TEST_CASE("staff_serial.service_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys;
    obl::object::ServiceStaff service{};
    obl::object::VisitFields(service, keys);
    OBL_CHECK_EQ(keys.checked, 1);
    OBL_CHECK_EQ(keys.hashed, 0);
}

OBL_TEST_CASE("staff_serial.builder_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::Builder};
    obl::object::Builder builder{};
    obl::object::VisitFields(builder, keys);
    OBL_CHECK_EQ(keys.checked, 21);
    OBL_CHECK_EQ(keys.hashed, 2); // action state, job type
}

OBL_TEST_CASE("staff_serial.cleaner_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::Cleaner};
    obl::object::Cleaner cleaner{};
    obl::object::VisitFields(cleaner, keys);
    OBL_CHECK_EQ(keys.checked, 22);
    OBL_CHECK_EQ(keys.hashed, 1); // action state
}

OBL_TEST_CASE("staff_serial.lifeguard_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::Lifeguard};
    obl::object::Lifeguard lifeguard{};
    obl::object::VisitFields(lifeguard, keys);
    OBL_CHECK_EQ(keys.checked, 29);
    OBL_CHECK_EQ(keys.hashed, 2); // action state, influence area
}

OBL_TEST_CASE("staff_serial.mechanic_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::Mechanic};
    obl::object::Mechanic mechanic{};
    obl::object::VisitFields(mechanic, keys);
    OBL_CHECK_EQ(keys.checked, 19);
    OBL_CHECK_EQ(keys.hashed, 0);
}

OBL_TEST_CASE("staff_serial.rep_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::Rep};
    obl::object::Rep rep{};
    obl::object::VisitFields(rep, keys);
    OBL_CHECK_EQ(keys.checked, 28); // seven counter rows
    OBL_CHECK_EQ(keys.hashed, 1); // m_ActionState
}

OBL_TEST_CASE("staff_serial.security_field_keys_match_the_save_bridge")
{
    StaffBridgeKeys keys{obl::object::ObjectType::SecurityGuard};
    obl::object::SecurityGuard guard{};
    obl::object::VisitFields(guard, keys);
    OBL_CHECK_EQ(keys.checked, 32);
    OBL_CHECK_EQ(keys.hashed, 1); // m_ActionState
}
