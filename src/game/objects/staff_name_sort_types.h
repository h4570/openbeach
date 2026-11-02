#pragma once

#include <cstdint>

namespace obl::object {

// The unused WorldStaffNameSort helpers compare the byte string after the six-byte prefix.
struct StaffNameSortKeyView {
    uint8_t m_prefix[6];
    char m_name[1];
};

} // namespace obl::object
