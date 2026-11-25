#pragma once

#include <array>
#include <string>

// Guest needs added by mods (catalog/guest_needs/<key>.json, docs/modding/reference/guest-needs.md). The 26 desires of the pack keep
// their own fixed slots; a need is an extra desire that follows them in load order, so the pack's needs decide exactly as before.
namespace obl::tables {

// Needs a world can have. The guest keeps them in fixed arrays (object_guest.h), so its byte backups in the test drivers keep working.
inline constexpr int kMaxGuestNeeds = 16;
// The desire number of need 0 in the guest's search: the needs follow the 26 desires of the pack.
inline constexpr int kFirstGuestNeed = 26;

// One need: the key a guest set and scripts name it by, its display text and the commodity family whose buildings serve it.
struct GuestNeedDef {
    bool operator==(const GuestNeedDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_name; // text key
    int m_family{}; // CommodityFamilyDef id
    std::string m_familyKey;
};

// A guest type's view of one need, in WorldParams::m_guestNeeds order: a type that gives no `preferences` range for it never wants it.
struct GuestNeedPref {
    bool operator==(const GuestNeedPref&) const = default;
    std::string m_key;
    std::string m_family; // the need's commodity family key (GuestNeedDef::m_familyKey)
    std::string m_name; // its text key (GuestNeedDef::m_name)
    bool m_named{};
    std::array<float, 2> m_pref{}; // growth rate range, drawn when a guest is made
    int m_max{}; // repeat limit, like MAX_<desire>
};

} // namespace obl::tables
