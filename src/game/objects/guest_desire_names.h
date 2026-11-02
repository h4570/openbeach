#pragma once
#include <cstdint>

namespace obl::guest_search {

constexpr uint32_t kDesireCount = 26;

// the commodity family behind each desire; the retry after a miss looks the family of the last building up here.
inline constexpr const char* kDesireNames[kDesireCount] = {
        "ACCOMDTN",
        "ALCOHOL",
        "AMUSEMNT",
        "FIRSTAID",
        "PILLS",
        "MEALS",
        "PARTYFUN",
        "SHOWER",
        "SNACKS",
        "SOFTDRNK",
        "SOUVENIR",
        "SPORTFUN",
        "SUNBATHE",
        "SWIMFUN",
        "RELIEF",
        "WATERFUN",
        "STROLL",
        "LEAVE",
        "VANDAL",
        "FIGHT",
        "FLIRT",
        "UMBRELS",
        "LOTION",
        "HARDSEX",
        "SLEEP",
        "SCARED",
};

} // namespace obl::guest_search
