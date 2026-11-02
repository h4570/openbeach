#pragma once

#include "game/objects/object_base.h"

#include <array>
#include <optional>
#include <string_view>

namespace obl::object {

// The personnel classes in the order the world messages reach them (service staff first, then the six staff professions),
// each under the name the packs use for its profession.
struct StaffClass {
    ObjectType type;
    std::string_view name;
};
inline constexpr std::array<StaffClass, 7> kStaffClasses = {{{ObjectType::ServiceStaff, "ServiceStaff"},
        {ObjectType::Lifeguard, "Lifeguard"}, {ObjectType::Mechanic, "Mechanic"}, {ObjectType::Rep, "Rep"},
        {ObjectType::Builder, "Builder"}, {ObjectType::Cleaner, "Cleaner"}, {ObjectType::SecurityGuard, "SecurityGuard"}}};

// Every class that takes the day and personnel messages: the personnel classes, then guests.
inline constexpr std::array<int, 8> kHumanClasses = {static_cast<int>(ObjectType::ServiceStaff), static_cast<int>(ObjectType::Lifeguard),
        static_cast<int>(ObjectType::Mechanic), static_cast<int>(ObjectType::Rep), static_cast<int>(ObjectType::Builder),
        static_cast<int>(ObjectType::Cleaner), static_cast<int>(ObjectType::SecurityGuard), static_cast<int>(ObjectType::Guest)};

// The class a pack row names, or nothing for a name outside the list.
inline std::optional<ObjectType> StaffClassByName(std::string_view name)
{
    for (const StaffClass& entry : kStaffClasses)
        if (entry.name == name) return entry.type;
    return std::nullopt;
}

} // namespace obl::object
