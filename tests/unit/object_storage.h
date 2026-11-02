#pragma once

#include "game/objects/object_base.h"
#include "game/objects/object_building.h"

#include <cstring>
#include <memory>
#include <type_traits>

// A unit test builds an object in a byte buffer to see which fields Construct writes. StampVtable makes such a buffer an object
// of its class, so it dispatches through (and UBSan's vptr check sees) the vtable of T (docs/testing/unit-tests.md, "ASan/UBSan").
namespace obl::unit {

template<class T> T& StampVtable(void* storage)
{
    static const T kPrototype{};
    std::memcpy(storage, static_cast<const void*>(&kPrototype), sizeof(void*));
    return *static_cast<T*>(storage);
}

// Every byte of `o` set to `byte` (a fresh raw allocation), then, for a game object, its class's vtable at offset 0 (generic
// fixtures also fill plain records such as definitions).
template<class T> void FillObject(T& o, unsigned char byte = 0)
{
    // A game object is not trivially destructible (its destructor is virtual); Building, the one that holds a container, has its own
    // overload below.
    static_assert(std::is_trivially_destructible_v<T> || std::is_base_of_v<object::Base, T>,
            "never memset a record that holds a std:: container");
    std::memset(static_cast<void*>(&o), byte, sizeof(T));
    if constexpr (std::is_base_of_v<object::Base, T>) StampVtable<T>(&o);
}

// A building holds its thought list as a std::deque: the list is destroyed, the bytes filled, then the list built again empty.
inline void FillObject(object::Building& o, unsigned char byte = 0)
{
    std::destroy_at(&o.m_ThoughtList);
    std::memset(static_cast<void*>(&o), byte, sizeof(o));
    StampVtable<object::Building>(&o);
    std::construct_at(&o.m_ThoughtList);
}

// A fresh object in place of `o`: game objects are not copyable, so `o = T{}` cannot reset one.
template<class T> void ResetObject(T& o)
{
    std::destroy_at(&o);
    std::construct_at(&o);
}

} // namespace obl::unit
