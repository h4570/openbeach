#pragma once

// obl-sim only: MSVC's <intrin.h> for the shared sources (__rdtsc and friends come from <x86intrin.h>; __cpuid is a Clang
// builtin under -fms-extensions; _ReturnAddress is in msvc_compat.h).
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#include <chrono>
#include <cstdint>
// WebAssembly and arm64 (macOS) have no __rdtsc; the profile timers only need a monotonic tick
// (docs/architecture/web-build.md).
inline uint64_t __rdtsc()
{
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif
