#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
// MSVC has no LeakSanitizer interface header.
#elif defined(__SANITIZE_ADDRESS__)
#define OBL_UNIT_LSAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OBL_UNIT_LSAN 1
#endif
#endif

#ifdef OBL_UNIT_LSAN
#include <sanitizer/lsan_interface.h>
#endif

template<class T> T* LeakOk(T* block)
{
#ifdef OBL_UNIT_LSAN
    if (block) __lsan_ignore_object(block);
#endif
    return block;
}
