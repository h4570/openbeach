#include "core/text16.h"
#include "unit_test.h"
#include "game/ui/gui_sort.h"

#include <array>
#include <limits>
#include <numeric>
#include <random>
#include <string>

using namespace obl::gui_sort;
namespace {
struct Row {
    float guest[14]{}, money[4]{};
    int32_t state{}, power{}, visits{};
    uint32_t age{}, faults{}, id{};
    obl::Text16 text;
};
struct Env {
    float GuestValue(const Row* row, GuestField field) const { return row->guest[static_cast<int>(field)]; }
    float BuildingFloat(const Row* row, BuildingField field) const { return row->money[static_cast<int>(field) - 1]; }
    int32_t BuildingSigned(const Row* row, BuildingField field) const
    {
        return field == BuildingField::RepairState ? row->state : field == BuildingField::Power ? row->power : row->visits;
    }
    uint32_t BuildingUnsigned(const Row* row, BuildingField field) const { return field == BuildingField::Age ? row->age : row->faults; }
    void Text(const Row* row, const char*, obl::Char16* out) const { obl::text16::Copy(out, row->text.c_str()); }
};
} // namespace

OBL_TEST_CASE("gui_sort.guest_keys_reverse_equal_and_nan")
{
    constexpr GuestField fields[] = {GuestField::Happiness, GuestField::Happiness, GuestField::Cash, GuestField::Happiness,
            GuestField::Happiness, GuestField::Sunburn, GuestField::Headache, GuestField::Drunk, GuestField::Bladder,
            GuestField::FoodIllness, GuestField::Price, GuestField::Safety, GuestField::Crowding, GuestField::Beauty,
            GuestField::Cleanliness, GuestField::Totty, GuestField::Variety, GuestField::Happiness};
    Env env;
    for (int key = 0; key < 18; ++key) {
        Row a, b;
        b.guest[static_cast<int>(fields[key])] = 1;
        OBL_CHECK(GuestLess(env, key, false, &a, &b));
        OBL_CHECK(!GuestLess(env, key, true, &a, &b));
        OBL_CHECK(!GuestLess(env, key, false, &a, &a));
        a.guest[static_cast<int>(fields[key])] = std::numeric_limits<float>::quiet_NaN();
        const bool unordered = key != 6 && key != 8 && !(key >= 10 && key <= 16);
        OBL_CHECK_EQ(GuestLess(env, key, false, &a, &b), unordered);
        OBL_CHECK_EQ(GuestLess(env, key, true, &a, &b), unordered);
    }
}

OBL_TEST_CASE("gui_sort.building_signed_unsigned_names_and_staff")
{
    Env env;
    Row a, b;
    a.state = a.power = a.visits = -1;
    a.age = a.faults = 0xffffffffu;
    for (int key : {0x12, 0x17, 0x1b}) {
        OBL_CHECK(BuildingLess(env, key, false, &a, &b));
        OBL_CHECK(!BuildingLess(env, key, true, &a, &b));
    }
    for (int key : {0x1a, 0x1c}) OBL_CHECK(!BuildingLess(env, key, false, &a, &b));
    for (int key = 0x13; key <= 0x16; ++key) {
        b.money[key - 0x13] = 1;
        OBL_CHECK(BuildingLess(env, key, false, &a, &b));
        a.money[key - 0x13] = std::numeric_limits<float>::quiet_NaN();
        OBL_CHECK(BuildingLess(env, key, false, &a, &b));
        OBL_CHECK(BuildingLess(env, key, true, &a, &b));
    }
    a.text = u"Zulu";
    b.text = u"Alpha";
    for (int key : {0, 0x11, 0x18, 0x19, 0x1d}) {
        OBL_CHECK(BuildingLess(env, key, false, &a, &b));
        OBL_CHECK(!BuildingLess(env, key, true, &a, &b));
    }
    OBL_CHECK(StaffTitleLess(env, &b, &a));
    OBL_CHECK(!StaffTitleLess(env, &a, &a));
}

OBL_TEST_CASE("gui_sort.object_ids_threshold_depth_and_permutation")
{
    std::mt19937 rng(1);
    for (int n : {0, 1, 2, 15, 16, 17, 31, 32, 65, 257, 1024}) {
        std::vector<Row> rows(n);
        std::vector<Row*> pointers;
        for (auto& row : rows) {
            row.id = rng();
            pointers.push_back(&row);
        }
        if (n > 2) {
            rows[0].id = 0;
            rows[1].id = 0xffffffffu;
            rows[2].id = rows.back().id;
        }
        const auto input = pointers;
        const auto id = [](auto p) { return p->id; };
        for (int mode = 0; mode < 4; ++mode) {
            pointers = input;
            if (mode == 1) std::reverse(pointers.begin(), pointers.end());
            if (mode == 2)
                for (auto& row : rows) row.id = 7;
            if (mode == 3) {
                ObjectIdIntrosortLoop(pointers.begin(), pointers.end(), 0, id);
                InsertionSort(pointers.begin(), pointers.end(), [&](auto a, auto b) { return id(a) < id(b); });
            } else
                ObjectIdSort(pointers.begin(), pointers.end(), id);
            OBL_CHECK(std::is_sorted(pointers.begin(), pointers.end(), [&](auto a, auto b) { return id(a) < id(b); }));
            OBL_CHECK(std::is_permutation(input.begin(), input.end(), pointers.begin(), pointers.end()));
        }
    }
}

OBL_TEST_CASE("gui_sort.buffered_schedule_preserves_finite_stability")
{
    std::mt19937 rng(12);
    for (int size = 0; size < 260; ++size) {
        std::vector<Row> rows(size);
        std::vector<Row*> pointers;
        for (auto& row : rows) {
            row.id = rng() % 5;
            pointers.push_back(&row);
        }
        auto expected = pointers;
        const auto less = [](auto lhs, auto rhs) { return lhs->id < rhs->id; };
        std::stable_sort(expected.begin(), expected.end(), less);
        StableSortUnordered(pointers.begin(), pointers.end(), less);
        OBL_CHECK(pointers == expected);
    }
}

OBL_TEST_CASE("gui_sort.nan_insertion_tests_the_sentinel_first")
{
    Row rows[4];
    rows[0].guest[0] = 0;
    rows[1].guest[0] = 2;
    rows[2].guest[0] = std::numeric_limits<float>::quiet_NaN();
    rows[3].guest[0] = 1;
    std::array<Row*, 4> pointers{&rows[0], &rows[1], &rows[2], &rows[3]};
    InsertionSort(pointers.begin(), pointers.end(), [](auto a, auto b) { return GuestLess(Env{}, 1, false, a, b); });
    const std::array<Row*, 4> expected{&rows[3], &rows[2], &rows[0], &rows[1]};
    OBL_CHECK(pointers == expected);
}

OBL_TEST_CASE("gui_sort.unordered_merge_only_when_a_nan_reaches_a_float_key")
{
    std::array<Row, 3> rows{};
    std::array<const Row*, 3> pointers{&rows[0], &rows[1], &rows[2]};
    const Env env;
    for (int key = -1; key < 18; ++key) OBL_CHECK(!GuestSortNeedsUnordered(env, key, pointers.begin(), pointers.end()));
    rows[1].guest[static_cast<int>(GuestField::Cash)] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(GuestSortNeedsUnordered(env, 2, pointers.begin(), pointers.end()));
    OBL_CHECK(!GuestSortNeedsUnordered(env, 3, pointers.begin(), pointers.end()));
    OBL_CHECK(!GuestSortNeedsUnordered(env, 2, pointers.begin(), pointers.begin() + 1));
    for (int key = 0x10; key <= 0x1c; ++key) OBL_CHECK(!BuildingSortNeedsUnordered(env, key, pointers.begin(), pointers.end()));
    rows[2].money[2] = std::numeric_limits<float>::quiet_NaN();
    for (int key = 0x10; key <= 0x1c; ++key)
        OBL_CHECK(BuildingSortNeedsUnordered(env, key, pointers.begin(), pointers.end()) == (key == 0x15));
}
