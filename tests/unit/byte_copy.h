#pragma once

#include "game/objects/object_building.h"

#include <cstring>
#include <memory>

namespace obl::test {

// A copy of an object with every byte of the source: a copy constructor would set the dynamic type to the static one.
template<class T> void ByteCopy(T& copy, const T& source)
{
    std::memcpy(static_cast<void*>(&copy), static_cast<const void*>(&source), sizeof(T));
}

// A building holds its thought list as a std::deque, which bytes cannot copy: the copy keeps an empty list of its own (the
// source's entries are not copied).
inline void ByteCopy(object::Building& copy, const object::Building& source)
{
    std::destroy_at(&copy.m_ThoughtList);
    std::memcpy(static_cast<void*>(&copy), static_cast<const void*>(&source), sizeof(copy));
    std::construct_at(&copy.m_ThoughtList);
}

} // namespace obl::test
