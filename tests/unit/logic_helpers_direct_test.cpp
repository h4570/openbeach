// Direct coverage of the small helpers in cleaning_logic.h and feedback_logic.h with minimal hand-written
// environments (the larger tests reach them only through the shared models).

#include "unit_test.h"

#include "game/cleaning/cleaning_logic.h"
#include "game/feedback/feedback_logic.h"

#include <cstring>
#include <string>
#include <vector>

OBL_TEST_CASE("cleaning_logic.TypeOfName maps the three object names and compares at most 32 bytes")
{
    using namespace obl::cleaning;
    OBL_CHECK_EQ(TypeOfName("Litter"), kTypeLitter);
    OBL_CHECK_EQ(TypeOfName("Bin"), kTypeBin);
    OBL_CHECK_EQ(TypeOfName("LOO"), kTypeNone); // a toilet is decided by its behavior (KindOf), not by the name
    OBL_CHECK_EQ(TypeOfName("litter"), kTypeLitter);
    OBL_CHECK_EQ(TypeOfName("Litter2"), kTypeNone);
    OBL_CHECK_EQ(TypeOfName(""), kTypeNone);
    // Only the first 32 bytes count: a name that matches the expected word for 32 bytes is a match.
    const std::string longName = std::string(32, 'A') + "tail";
    OBL_CHECK(NameIs(longName.c_str(), std::string(32, 'A').c_str()));
    OBL_CHECK(!NameIs("Bin", "Bins"));
}

OBL_TEST_CASE("feedback_logic.StaffMask selects the field per object type and ignores other types")
{
    using namespace obl::feedback;
    Manager m;
    OBL_CHECK_EQ(StaffMask(m, 7), &m.lifeGuardMessages);
    OBL_CHECK_EQ(StaffMask(m, 8), &m.mechanicMessages);
    OBL_CHECK_EQ(StaffMask(m, 9), &m.repMessages);
    OBL_CHECK_EQ(StaffMask(m, 10), &m.builderMessages);
    OBL_CHECK_EQ(StaffMask(m, 11), &m.cleanerMessages);
    OBL_CHECK_EQ(StaffMask(m, 12), &m.securityGuardMessages);
    OBL_CHECK_EQ(StaffMask(m, 6), &m.staffMessages);
    OBL_CHECK_EQ(StaffMask(m, 5), nullptr);
    OBL_CHECK_EQ(StaffMask(m, 13), nullptr);
    OBL_CHECK_EQ(StaffMask(m, -1), nullptr);

    MarkStaffMessage(m, 0x5, 8);
    MarkStaffMessage(m, 0x2, 8);
    MarkStaffMessage(m, 0xff, 99); // unrecognized type: no effect
    OBL_CHECK_EQ(GetStaffMessages(m, 8), 0x7u);
    OBL_CHECK_EQ(GetStaffMessages(m, 7), 0u);
    OBL_CHECK_EQ(GetStaffMessages(m, 99), 0u);
    ClearStaffMessages(m, 8);
    OBL_CHECK_EQ(GetStaffMessages(m, 8), 0u);
}

OBL_TEST_CASE("feedback_logic.CommodityMessageKey covers 0..25 and is null beyond")
{
    using namespace obl::feedback;
    for (uint32_t i = 0; i < static_cast<uint32_t>(kFamilies); ++i) {
        const char* key = CommodityMessageKey(i);
        OBL_REQUIRE(key != nullptr);
        OBL_CHECK(std::strncmp(key, "FEEDBACK_", 9) == 0);
    }
    OBL_CHECK_EQ(CommodityMessageKey(static_cast<uint32_t>(kFamilies)), nullptr);
    OBL_CHECK_EQ(CommodityMessageKey(0xffffffffu), nullptr);
    OBL_CHECK_EQ(std::string(CommodityMessageKey(14)), std::string("FEEDBACK_TOILET"));
}

OBL_TEST_CASE("feedback_logic.IsSuppressed compares the low byte only and honours the stored count")
{
    using namespace obl::feedback;
    Manager m;
    m.suppressed[0] = 5;
    m.suppressed[1] = 9; // beyond the count below: ignored
    m.suppressedCount = 1;
    OBL_CHECK(IsSuppressed(m, 5));
    OBL_CHECK(IsSuppressed(m, 0x105)); // only the low byte is compared
    OBL_CHECK(!IsSuppressed(m, 9));
    m.suppressedCount = 2;
    OBL_CHECK(IsSuppressed(m, 9));
    // A corrupt count is clamped to the array size instead of reading past it.
    m.suppressedCount = 0xffffffffu;
    OBL_CHECK(!IsSuppressed(m, 77));
}

OBL_TEST_CASE("feedback_logic.AddScenarioMessage switches, silences and stops at the array limit")
{
    using namespace obl::feedback;
    Manager m;
    AddScenarioMessage(m, "MESS_TYPE_WORLD");
    AddScenarioMessage(m, "MESS_TYPE_OBJECT");
    OBL_CHECK_EQ(m.suppressWorld, 1);
    OBL_CHECK_EQ(m.suppressObject, 1);
    AddScenarioMessage(m, "MESS_TYPE_GUEST");
    AddScenarioMessage(m, "MESS_TYPE_UNLISTED");
    OBL_CHECK_EQ(m.suppressedCount, 1u);
    OBL_CHECK_EQ(m.suppressed[0], 5u);
    for (int i = 0; i < 200; ++i) AddScenarioMessage(m, "MESS_TYPE_STAFF");
    OBL_CHECK_EQ(m.suppressedCount, static_cast<uint32_t>(kMaxSuppressed));
}
