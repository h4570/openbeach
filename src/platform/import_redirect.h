#pragma once

// Redirects the calls openbeach.dll (with the SDL linked into it) makes to `dll`!`function` by rewriting its own import
// slot; `previousTarget` receives what the slot held. A function the DLL does not import has no caller to redirect: then
// `previousTarget` gets the export itself and the call succeeds without a change. Implemented in import_redirect.cpp.
namespace obl::platform {

bool RedirectImport(const char* dll, const char* function, void* detour, void** previousTarget);

template<typename T> bool RedirectImport(const char* dll, const char* function, T* detour, T** previousTarget)
{
    return RedirectImport(dll, function, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(previousTarget));
}

} // namespace obl::platform
