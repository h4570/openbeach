#pragma once

#include <cstddef>
#include <cstring>

// Fills a widget's fields with a byte, as a recycled heap block would, and keeps the vptr so that it stays a live object.
template<class T> void PoisonFields(T& widget, int byte)
{
    std::memset(reinterpret_cast<unsigned char*>(&widget) + sizeof(void*), byte, sizeof(T) - sizeof(void*));
}
