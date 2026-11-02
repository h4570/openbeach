#include "unit_test.h"
#include "game/objects/object_building.h"
#include "importer/src/readers/save/save_writer.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>

namespace {
namespace O = obl::object;

// Every entry of the field table against the converter's Building bridge table (object type 2).
struct BridgeKeys {
    int checked = 0;
    int hashed = 0;
    static bool Known(const std::string& key) { return obl::convert::OldObjectFieldKeyKnown(2, key); }
    void Check(const char* key, O::Hashed h)
    {
        OBL_CHECK_EQ(std::string(Known(key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
        hashed += h == O::Hashed::Yes;
    }
    template<class T> void Int(const char* key, T&, O::Hashed h) { Check(key, h); }
    template<class T> void UInt(const char* key, T&, O::Hashed h) { Check(key, h); }
    void Float(const char* key, float&, O::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, O::Hashed h) { Check(key, h); }
    void Ints(const char* key, int32_t*, int, O::Hashed h) { Check(key, h); }
    void Bytes(const char* key, uint8_t*, int, O::Hashed h) { Check(key, h); }
    void Floats(const char* key, float*, int, O::Hashed h) { Check(key, h); }
    void Sprites(const char* key, O::Building&) { OBL_CHECK(Known(std::string(key) + ".anim")); }
    void Construction(O::Building&)
    {
        OBL_CHECK(Known("building.under_construction"));
        OBL_CHECK(Known("building.under_demolition"));
    }
    // The presence flags of the schedule and the queue are dropped: their groups carry the records.
    void Events(const char* key, O::Building&)
    {
        OBL_CHECK(!Known(key));
        OBL_CHECK(Known(std::string(key) + ".owner"));
    }
    void Thoughts(const char* key, O::Building&) { Check(key, O::Hashed::No); }
    void Queue(const char* key, O::Building&, O::Hashed h)
    {
        OBL_CHECK(!Known(key));
        OBL_CHECK(Known(std::string(key) + ".owner"));
        hashed += h == O::Hashed::Yes;
    }
};
} // namespace

OBL_TEST_CASE("Building field table keys match the converter")
{
    O::Building b{};
    BridgeKeys keys;
    O::VisitFields(b, keys);
    OBL_CHECK_EQ(keys.checked, 96);
    OBL_CHECK_EQ(keys.hashed, 41); // the records the snapshot hashed before the table
}
