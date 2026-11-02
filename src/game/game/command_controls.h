#pragma once

#include "game/game/command.h"

#include <bit>
#include <type_traits>

namespace obl::command {

template<class T> struct ObjectControlTraits;

template<> struct ObjectControlTraits<SetRepairInterval> {
    static constexpr const char* kKeyword = "BUILDING_REPAIRINTERVAL";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetBeerStrength> {
    static constexpr const char* kKeyword = "BEER_STRENGTH";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetBeerPrice> {
    static constexpr const char* kKeyword = "BUILDING_BEERPRICE";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetBuildingOpenPolicy> {
    static constexpr const char* kKeyword = "BI_BUILDING_OPEN";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetBeachOpenPolicy> {
    static constexpr const char* kKeyword = "STAFF_BEACH_OPEN";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetCleanerToilets> {
    static constexpr const char* kKeyword = "CLEANER_TOILETS";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetCleanerBins> {
    static constexpr const char* kKeyword = "CLEANER_BINS";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetCleanerLitter> {
    static constexpr const char* kKeyword = "CLEANER_LITTER";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetMechanicRepair> {
    static constexpr const char* kKeyword = "MECHANIC_REPAIR";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetMechanicVandals> {
    static constexpr const char* kKeyword = "MECHANIC_VANDALS";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetMechanicWater> {
    static constexpr const char* kKeyword = "MECHANIC_WATER";
    static constexpr bool kByte = true;
};

template<> struct ObjectControlTraits<SetRepGuestTime> {
    static constexpr const char* kKeyword = "REP_GUEST_TIME";
    static constexpr bool kByte = false;
};

template<> struct ObjectControlTraits<SetSecurityHarshness> {
    static constexpr const char* kKeyword = "SECURITY_HARSHNESS";
    static constexpr bool kByte = false;
};

template<class T> std::optional<uint32_t> ObjectControlWord(const T& control)
{
    if constexpr (std::is_same_v<decltype(control.value), float>) {
        return std::bit_cast<uint32_t>(control.value);
    } else {
        if constexpr (ObjectControlTraits<T>::kByte)
            if (control.value < 0 || control.value > 255) return std::nullopt;
        return static_cast<uint32_t>(control.value);
    }
}

} // namespace obl::command
