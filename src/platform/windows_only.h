#pragma once

// An installer of a windowed-host module (WINDOWED_MODULES in src/host_linux/CMakeLists.txt) in a shared install chain: the
// Windows game and the Linux windowed OpenBeach (OBL_WINDOWED_HOST) run it, the headless Linux obl-sim builds without
// the module and skips the call.
#if defined(_WIN32) || defined(OBL_WINDOWED_HOST)
#define OBL_WINDOWS_ONLY(install) (install)
#else
#define OBL_WINDOWS_ONLY(install) true
#endif
