#pragma once

#include "core/ascii_case.h"

#include <string_view>

// The scenery kinds the game code tells apart by name. Names reach an object both as catalog keys and as catalog names, and
// the two differ in spelling (`bin`, `Bin`), so every test here ignores case.
namespace obl::scenery_names {

inline bool IsBin(std::string_view name)
{
    return ascii::EqualsNoCase(name, "Bin");
}

inline bool IsLounger(std::string_view name)
{
    return ascii::EqualsNoCase(name, "Lounger");
}

// Exactly the catalog name every piece of rubbish shares.
inline bool IsLitter(std::string_view name)
{
    return ascii::EqualsNoCase(name, "Litter");
}

// Litter by key as well: `litterburger`, `litterpuke` and the others carry the word inside the name.
inline bool HasLitterInName(std::string_view name)
{
    return ascii::ContainsNoCase(name, "Litter");
}

inline bool IsHoarding(std::string_view name)
{
    return ascii::ContainsNoCase(name, "HOARDING_");
}

} // namespace obl::scenery_names
