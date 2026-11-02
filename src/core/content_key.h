#pragma once

#include "core/ascii_case.h"

#include <string>
#include <string_view>

namespace obl::content_key {
// The content key rule (docs/architecture/data-formats/converter/save-conversion.md): lowercase, every run of other characters one
// '_'. Shared by old saves and map props: discard leading/trailing separators, collapse each internal run.
inline std::string ContentKey(std::string_view name, bool* clean = nullptr)
{
    std::string key;
    bool separator = false;
    for (char c : name) {
        const char lower = ascii::Lower(c);
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_') {
            if (separator && !key.empty()) key += '_';
            separator = false;
            key += lower;
        } else {
            separator = true;
            if (clean) *clean = false;
        }
    }
    return key;
}
} // namespace obl::content_key
