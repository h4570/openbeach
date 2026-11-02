#pragma once

#include "core/text16.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <vector>

namespace obl::gui_sort {

enum class GuestField {
    Happiness,
    Cash,
    Sunburn,
    Headache,
    Drunk,
    Bladder,
    FoodIllness,
    Price,
    Safety,
    Crowding,
    Beauty,
    Cleanliness,
    Totty,
    Variety
};
enum class BuildingField { RepairState, Income, Cost, Profit, AverageProfit, Power, Age, Visits, Faults };

inline GuestField GuestKey(int key)
{
    switch (key) {
    case 2: return GuestField::Cash;
    case 5: return GuestField::Sunburn;
    case 6: return GuestField::Headache;
    case 7: return GuestField::Drunk;
    case 8: return GuestField::Bladder;
    case 9: return GuestField::FoodIllness;
    case 10: return GuestField::Price;
    case 11: return GuestField::Safety;
    case 12: return GuestField::Crowding;
    case 13: return GuestField::Beauty;
    case 14: return GuestField::Cleanliness;
    case 15: return GuestField::Totty;
    case 16: return GuestField::Variety;
    default: return GuestField::Happiness;
    }
}

inline bool GuestUnorderedIsLess(int key)
{
    return key != 6 && key != 8 && !(key >= 10 && key <= 16);
}
inline bool FloatLess(float lhs, float rhs, bool unordered)
{
    return lhs < rhs || (unordered && (std::isnan(lhs) || std::isnan(rhs)));
}

template<class Env, class T> bool GuestLess(const Env& env, int key, bool reverse, T lhs, T rhs)
{
    if (reverse) std::swap(lhs, rhs);
    const float right = env.GuestValue(rhs, GuestKey(key));
    return FloatLess(env.GuestValue(lhs, GuestKey(key)), right, GuestUnorderedIsLess(key));
}

template<class Env, class T> bool BuildingLess(const Env& env, int key, bool reverse, T lhs, T rhs)
{
    if (reverse) std::swap(lhs, rhs);
    switch (key) {
    case 0x12: return env.BuildingSigned(lhs, BuildingField::RepairState) < env.BuildingSigned(rhs, BuildingField::RepairState);
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16: {
        const auto field = static_cast<BuildingField>(static_cast<int>(BuildingField::Income) + key - 0x13);
        return FloatLess(env.BuildingFloat(lhs, field), env.BuildingFloat(rhs, field), true);
    }
    case 0x17: return env.BuildingSigned(lhs, BuildingField::Power) < env.BuildingSigned(rhs, BuildingField::Power);
    case 0x1a: return env.BuildingUnsigned(lhs, BuildingField::Age) < env.BuildingUnsigned(rhs, BuildingField::Age);
    case 0x1b: return env.BuildingSigned(lhs, BuildingField::Visits) < env.BuildingSigned(rhs, BuildingField::Visits);
    case 0x1c: return env.BuildingUnsigned(lhs, BuildingField::Faults) < env.BuildingUnsigned(rhs, BuildingField::Faults);
    default: {
        Char16 left[256], right[256];
        env.Text(rhs, "BUILDING_NAME", right);
        env.Text(lhs, "BUILDING_NAME", left);
        return text16::Compare(right, left) < 0;
    }
    }
}

template<class Env, class T> bool StaffTitleLess(const Env& env, T lhs, T rhs)
{
    Char16 left[256], right[256];
    env.Text(lhs, "STAFF_JOB_TITLE", left);
    env.Text(rhs, "STAFF_JOB_TITLE", right);
    return text16::Compare(left, right) < 0;
}

template<class Env, class It> bool GuestSortNeedsUnordered(const Env& env, int key, It begin, It end)
{
    return std::any_of(begin, end, [&](auto row) { return std::isnan(env.GuestValue(row, GuestKey(key))); });
}

template<class Env, class It> bool BuildingSortNeedsUnordered(const Env& env, int key, It begin, It end)
{
    if (key < 0x13 || key > 0x16) return false;
    const auto field = static_cast<BuildingField>(static_cast<int>(BuildingField::Income) + key - 0x13);
    return std::any_of(begin, end, [&](auto row) { return std::isnan(env.BuildingFloat(row, field)); });
}

template<class It, class Less> void InsertionSort(It begin, It end, Less less)
{
    if (begin == end) return;
    for (auto pos = begin + 1; pos != end; ++pos) {
        const auto value = *pos;

        if (less(value, *begin)) {
            std::move_backward(begin, pos, pos + 1);
            *begin = value;
            continue;
        }
        auto hole = pos;
        while (hole != begin && less(value, hole[-1])) {
            *hole = hole[-1];
            --hole;
        }
        *hole = value;
    }
}

template<class It, class Less> void StableSortUnordered(It begin, It end, Less less)
{
    const auto size = end - begin;
    if (size < 2) return;
    using Value = typename std::iterator_traits<It>::value_type;
    std::vector<Value> buffer(size);
    const auto merge = [&](auto a, auto aEnd, auto b, auto bEnd, auto out) {
        while (a != aEnd && b != bEnd) *out++ = less(*b, *a) ? *b++ : *a++;
        out = std::copy(a, aEnd, out);
        std::copy(b, bEnd, out);
    };
    const auto runs = [&](auto first, auto last, auto out, ptrdiff_t width) {
        while (first != last) {
            const auto middle = first + std::min<ptrdiff_t>(width, last - first);
            const auto finish = middle + std::min<ptrdiff_t>(width, last - middle);
            merge(first, middle, middle, finish, out);
            out += finish - first;
            first = finish;
        }
    };
    const auto half = [&](It first, It last) {
        for (auto chunk = first; chunk != last;) {
            const auto finish = chunk + std::min<ptrdiff_t>(7, last - chunk);
            InsertionSort(chunk, finish, less);
            chunk = finish;
        }
        for (ptrdiff_t width = 7; width < last - first; width *= 4) {
            runs(first, last, buffer.begin(), width);
            runs(buffer.begin(), buffer.begin() + (last - first), first, width * 2);
        }
    };
    const auto middle = begin + (size + 1) / 2;
    half(begin, middle);
    half(middle, end);
    if (middle - begin <= end - middle) {
        std::copy(begin, middle, buffer.begin());
        merge(buffer.begin(), buffer.begin() + (middle - begin), middle, end, begin);
    } else {
        std::copy(middle, end, buffer.begin());
        auto left = middle;
        auto right = buffer.begin() + (end - middle);
        auto out = end;
        while (left != begin && right != buffer.begin()) {
            --out;
            if (less(right[-1], left[-1]))
                *out = *--left;
            else
                *out = *--right;
        }
        while (right != buffer.begin()) *--out = *--right;
    }
}

template<class It, class Id>
void AdjustHeap(It begin, ptrdiff_t hole, ptrdiff_t size, typename std::iterator_traits<It>::value_type value, Id id)
{
    const auto top = hole;
    auto child = 2 * hole + 2;
    while (child < size) {
        if (id(begin[child]) < id(begin[child - 1])) --child;
        begin[hole] = begin[child];
        hole = child;
        child = 2 * hole + 2;
    }
    if (child == size) {
        begin[hole] = begin[child - 1];
        hole = child - 1;
    }
    while (hole > top) {
        const auto parent = (hole - 1) / 2;
        if (id(value) <= id(begin[parent])) break;
        begin[hole] = begin[parent];
        hole = parent;
    }
    begin[hole] = value;
}

template<class It, class Id> void HeapSort(It begin, It end, Id id)
{
    auto size = end - begin;
    if (size < 2) return;
    for (auto parent = (size - 2) / 2;; --parent) {
        AdjustHeap(begin, parent, size, begin[parent], id);
        if (parent == 0) break;
    }
    while (size > 1) {
        const auto value = begin[--size];
        begin[size] = *begin;
        AdjustHeap(begin, 0, size, value, id);
    }
}

template<class It, class Id> void ObjectIdIntrosortLoop(It begin, It end, int depth, Id id)
{
    while (end - begin > 16) {
        if (depth == 0) {
            HeapSort(begin, end, id);
            return;
        }
        --depth;
        const auto a = *begin, b = begin[(end - begin) / 2], c = end[-1];
        const auto pivot = id(a) < id(b) ? (id(b) < id(c) ? b : (id(a) < id(c) ? c : a)) : (id(a) < id(c) ? a : (id(b) < id(c) ? c : b));
        auto left = begin, right = end;
        for (;;) {
            while (id(*left) < id(pivot)) ++left;
            do {
                --right;
            } while (id(pivot) < id(*right));
            if (left >= right) break;
            std::iter_swap(left++, right);
        }
        ObjectIdIntrosortLoop(left, end, depth, id);
        end = left;
    }
}

template<class It, class Id> void ObjectIdSort(It begin, It end, Id id)
{
    int depth = 0;
    for (auto size = end - begin; size > 1; size >>= 1) depth += 2;
    ObjectIdIntrosortLoop(begin, end, depth, id);
    const auto less = [&](auto lhs, auto rhs) { return id(lhs) < id(rhs); };
    InsertionSort(begin, begin + std::min<ptrdiff_t>(16, end - begin), less);

    for (auto pos = begin + std::min<ptrdiff_t>(16, end - begin); pos != end; ++pos) {
        const auto value = *pos;
        auto hole = pos;
        while (less(value, hole[-1])) {
            *hole = hole[-1];
            --hole;
        }
        *hole = value;
    }
}

} // namespace obl::gui_sort

namespace obl::gui {
bool RegisterGuiSortDrivers();

// The stable sorts of guests, buildings and staff titles: the tables call them directly.
namespace gui_table_sort {
void Guests(void** begin, void** end, int key, bool reverse);
void Buildings(void** begin, void** end, int key, bool reverse);
void StaffByTitle(void** begin, void** end);
} // namespace gui_table_sort
} // namespace obl::gui
