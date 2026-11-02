#include "core/char16.h"
#include "unit_test.h"
#include "importer/src/readers/save/save_writer.h"
#include "game/objects/object_guest.h"

#include <algorithm>
#include <string>

namespace {
using namespace obl::unit;
namespace L = obl::layout;
// Every entry of Guest's field table against the converter's bridge table (type 3): each key is one the converter maps an
// old label onto. The attached sprite's presence flag is dropped and its group carries the sprite's name.
struct GuestBridgeKeys {
    int checked = 0;
    int hashed = 0;
    static bool Known(const std::string& key) { return obl::convert::OldObjectFieldKeyKnown(3, key); }
    void Check(const char* key, obl::object::Hashed h)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unknown key
        ++checked;
        hashed += h == obl::object::Hashed::Yes;
    }
    void Int(const char* key, int32_t&, obl::object::Hashed h) { Check(key, h); }
    void UInt(const char* key, uint32_t&, obl::object::Hashed h) { Check(key, h); }
    void Float(const char* key, float&, obl::object::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, obl::object::Hashed h) { Check(key, h); }
    void Floats(const char* key, float*, int, obl::object::Hashed h) { Check(key, h); }
    void Ints(const char* key, int32_t*, int, obl::object::Hashed h) { Check(key, h); }
    void Bytes(const char* key, uint8_t*, int, obl::object::Hashed h) { Check(key, h); }
    void Text(const char* key, char*, int capacity, obl::object::Hashed h)
    {
        OBL_CHECK_EQ(capacity, 40);
        Check(key, h);
    }
    void Name(const char* key, obl::Char16*&, obl::object::Hashed h) { Check(key, h); }
    void Attached(const char* key, void*&, obl::object::Hashed h)
    {
        OBL_CHECK(!Known(key));
        OBL_CHECK(Known(std::string(key) + ".sprite"));
        ++checked;
        hashed += h == obl::object::Hashed::Yes;
    }
};
} // namespace

OBL_TEST_CASE("guest_serial.field_keys_match_the_save_bridge")
{
    GuestBridgeKeys keys;
    obl::object::Guest guest{};
    obl::object::VisitFields(guest, keys);
    OBL_CHECK_EQ(keys.checked, 82);
    // The four arrays, vehicle, activity, drunk, oxygen, cash, happiness, action state, the four hotel records, both day-arrived
    // copies, time arrived, time to leave, sunburn, food illness and the ill flag.
    OBL_CHECK_EQ(keys.hashed, 22);
}
