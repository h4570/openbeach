#pragma once

#include "object_storage.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// A raw byte stream and an Env for the `Fields` templates of the *_serial.h / *_lifecycle.h headers (guest, staff, building records). The same
// Env writes or reads (`reading`), like the game's stream, so one filled object can be written, read into another object and written again.
// Every record appends its label to `names`; Base/Counter/BeachArea/Attached are game-side operations and only leave a marker.
namespace obl::unit {

struct Wire {
    std::vector<uint8_t> bytes;
    std::vector<std::string> names;
    std::vector<size_t> offsets; // stream position of each label that carries bytes (parallel to byteNames)
    std::vector<std::string> byteNames;
    size_t pos = 0;
    bool reading = false;

    void Raw(const char* name, void* data, size_t size)
    {
        names.push_back(name);
        byteNames.push_back(name);
        offsets.push_back(pos);
        if (reading) {
            if (pos + size > bytes.size()) {
                names.push_back("!underrun");
                pos += size;
                return;
            }
            std::memcpy(data, bytes.data() + pos, size);
        } else {
            bytes.resize(bytes.size() + size);
            std::memcpy(bytes.data() + pos, data, size);
        }
        pos += size;
    }
    template<class T> void Int(const char* n, T& v)
    {
        static_assert(sizeof(T) == 4);
        Raw(n, &v, 4);
    }
    template<class T> void UInt(const char* n, T& v)
    {
        static_assert(sizeof(T) == 4);
        Raw(n, &v, 4);
    }
    template<class T> void Float(const char* n, T& v)
    {
        static_assert(sizeof(T) == 4);
        Raw(n, &v, 4);
    }
    template<class T> void Bool(const char* n, T& v)
    {
        static_assert(sizeof(T) == 1);
        Raw(n, &v, 1);
    }
    template<class T> void Ints(const char* n, T* v, int count) { Raw(n, v, sizeof(T) * static_cast<size_t>(count)); }
    template<class T> void Floats(const char* n, T* v, int count) { Raw(n, v, sizeof(T) * static_cast<size_t>(count)); }
    template<class T> void Bytes(const char* n, T* v, int count) { Raw(n, v, sizeof(T) * static_cast<size_t>(count)); }
    template<class T> void Text(const char* n, T* v, int count) { Raw(n, v, sizeof(T) * static_cast<size_t>(count)); }
    template<class T> void Wide(const char* n, T* v, int count) { Raw(n, v, sizeof(T) * static_cast<size_t>(count)); }
    // One bit of a mask, as a byte.
    void MaskBit(const char* n, uint32_t& mask, int bit)
    {
        uint8_t value = (mask >> bit) & 1;
        Raw(n, &value, 1);
        if (reading) mask = (mask & ~(1u << bit)) | (static_cast<uint32_t>(value) << bit);
    }
    template<class T> void Mark(const char* n, T&) { names.push_back(n); }
    void Counter(const char* n) { names.push_back(n); }
    template<class T> void Base(T&) { names.push_back("<base>"); }
    template<class... T> void BeachArea(T&...) { names.push_back("<beach area>"); }
    template<class T> void Attached(T&) { names.push_back("<attached sprite>"); }
    template<class T> void Name(const char* n, T&) { names.push_back(n); }
};

// A heap object whose every byte but the vptr is a deterministic pseudo-random value (so float and bool fields carry arbitrary
// bits).
template<class T> std::unique_ptr<T> FilledObject(uint32_t seed)
{
    auto* raw = ::operator new(sizeof(T));
    auto* bytes = static_cast<uint8_t*>(raw);
    uint32_t state = seed * 2654435761u + 1;
    for (size_t i = 0; i < sizeof(T); ++i) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = static_cast<uint8_t>(state >> 24);
    }
    return std::unique_ptr<T>(&StampVtable<T>(raw));
}

} // namespace obl::unit

namespace obl::unit {
// Stream position of the `nth` (0-based) record with this label, SIZE_MAX when absent.
inline size_t OffsetOf(const Wire& w, const char* name, size_t nth = 0)
{
    for (size_t i = 0; i < w.byteNames.size(); ++i)
        if (w.byteNames[i] == name && nth-- == 0) return w.offsets[i];
    return SIZE_MAX;
}
} // namespace obl::unit
