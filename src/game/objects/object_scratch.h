#pragma once

#include "game/objects/object_building.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace obl::object {

// A game object whose teardown never runs: the diagnostic drivers and the save check work on copies of live objects or on
// bare fixtures, and a copy shares the sprites, route and grid cell of the object it was copied from, so its destructor
// would give them back. Scratch builds the object in its own storage and, when it goes, frees only what it owns as a C++
// value (a building's thought list container). Game objects are not copyable: a copy takes the source's bytes, so the
// source must be a T itself (not a subclass); a building copy keeps an empty thought list of its own.
template<class T> class Scratch {
    static_assert(std::is_base_of_v<Base, T>);

public:
    Scratch() : m_object(new (m_storage) T()) {}
    explicit Scratch(const T& source) : Scratch()
    {
        if constexpr (std::is_base_of_v<Building, T>) std::destroy_at(&m_object->m_ThoughtList);
        std::memcpy(static_cast<void*>(m_object), static_cast<const void*>(&source), sizeof(T));
        if constexpr (std::is_base_of_v<Building, T>) std::construct_at(&m_object->m_ThoughtList);
    }
    ~Scratch()
    {
        if constexpr (std::is_base_of_v<Building, T>) std::destroy_at(&m_object->m_ThoughtList);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    T& operator*() const { return *m_object; }
    T* operator->() const { return m_object; }
    T* Get() const { return m_object; }

private:
    alignas(T) unsigned char m_storage[sizeof(T)];
    T* m_object;
};

// N bare fixture objects in a std::array (the drivers hand them to helpers that take the array), built value-initialized; like
// Scratch, their teardown never runs.
template<class T, size_t N> class ScratchArray {
    static_assert(std::is_base_of_v<Base, T>);
    using Array = std::array<T, N>;

public:
    ScratchArray() : m_objects(new (m_storage) Array{}) {}
    ~ScratchArray()
    {
        if constexpr (std::is_base_of_v<Building, T>)
            for (T& object : *m_objects) std::destroy_at(&object.m_ThoughtList);
    }
    ScratchArray(const ScratchArray&) = delete;
    ScratchArray& operator=(const ScratchArray&) = delete;

    Array& operator*() const { return *m_objects; }

private:
    alignas(Array) unsigned char m_storage[sizeof(Array)];
    Array* m_objects;
};

} // namespace obl::object
