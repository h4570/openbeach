#pragma once

// An executable build the importer knows by its hash (importer/src/source/source.h). Public because ImportOptions holds a
// std::span of it, and libc++ before 19 (Apple's too) cannot copy a span of an incomplete type.

#include <string_view>

namespace obl::convert {

enum class ExeBuild { DigitalOct2002, CdAug2002 };

struct KnownExe {
    std::string_view sha256; // lowercase hex
    ExeBuild build;
};

} // namespace obl::convert
