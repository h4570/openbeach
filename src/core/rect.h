#pragma once

// An integer screen rectangle. The renderer and the object classes that embed one share this type.
namespace obl::screen {

struct Rect {
    int left, top, right, bottom;
};

} // namespace obl::screen
