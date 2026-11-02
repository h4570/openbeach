#pragma once

#include "core/ascii_case.h"

#include <cstddef>
#include <string>
#include <string_view>

// The fifteen guest sets and the file each one comes from (game-data.md, "Guest sets"). The converter reads the
// installed `Data/Script/<stem>.ble` files, the scenario converter maps a scenario's WORLD_GUESTLIST to a set, and the engine
// maps the same WORLD_GUESTLIST value to `packs/base/guests/<set>.json`. One table keeps the three in step.
namespace obl::guest_sets {

struct Set {
    const char* stem; // lowercase file name without the folder and the extension
    const char* name; // the set's name: guests/<name>.json
};

inline constexpr Set kSets[] = {{"guests01", "campaign_01"}, {"guests02", "campaign_02"}, {"guests03", "campaign_03"},
        {"guests04", "campaign_04"}, {"guests05", "campaign_05"}, {"guests06", "campaign_06"}, {"guests07", "campaign_07"},
        {"guests08", "campaign_08"}, {"guests09", "campaign_09"}, {"guests10", "campaign_10"}, {"guests11", "campaign_11"},
        {"guests12", "campaign_12"}, {"sandboxe_guests", "sandbox_easy"}, {"sandboxm_guests", "sandbox_medium"},
        {"sandboxh_guests", "sandbox_hard"}};

// The set a guest file names: the file name after its last folder separator, ASCII case ignored, with the extension `.pis` or `.ble`
// (the scenario's WORLD_GUESTLIST spells the .pis, the engine loads the .ble). nullptr for any other name.
inline const char* SetForList(std::string_view list)
{
    const size_t separator = list.find_last_of("/\\");
    std::string name(separator == std::string_view::npos ? list : list.substr(separator + 1));
    for (char& c : name) c = ascii::Lower(c);
    for (std::string_view extension : {".pis", ".ble"}) {
        if (name.size() <= extension.size() || name.compare(name.size() - extension.size(), extension.size(), extension) != 0) continue;
        name.resize(name.size() - extension.size());
        for (const Set& set : kSets)
            if (name == set.stem) return set.name;
        return nullptr;
    }
    return nullptr;
}

// The set with this name (`campaign_01`, `sandbox_easy`, ...), the form a scenario's `guests` key holds. nullptr for any other name.
inline const char* SetForName(std::string_view name)
{
    for (const Set& set : kSets)
        if (name == set.name) return set.name;
    return nullptr;
}

// A guest set of an own pack (`guests/<name>.json`): lowercase letters, digits, `_` and `-`, so the name cannot leave the folder.
inline bool IsPackSetName(std::string_view name)
{
    if (name.empty()) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

} // namespace obl::guest_sets
