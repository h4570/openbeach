#include "platform/import_redirect.h"

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>

#include <cstring>
#endif

namespace obl::platform {

#ifdef _WIN32
namespace {

// The import descriptors of the module this code is linked into.
const IMAGE_IMPORT_DESCRIPTOR* Imports(uint8_t*& base)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&Imports), &module))
        return nullptr;
    base = reinterpret_cast<uint8_t*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    return directory.Size ? reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress) : nullptr;
}

// The import slot of `dll`!`function` (matched by name) in this module, or null.
void** ImportSlot(const char* dll, const char* function)
{
    uint8_t* base = nullptr;
    for (const IMAGE_IMPORT_DESCRIPTOR* d = Imports(base); d && d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0) return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

} // namespace
#endif

bool RedirectImport(const char* dll, const char* function, void* detour, void** previousTarget)
{
#ifndef _WIN32
    // The Linux obl-sim imports no Windows DLL; only Windows-only modules redirect imports (docs/architecture/linux-host.md).
    (void)detour;
    if (previousTarget) *previousTarget = nullptr;
    OBL_LOG("import %s!%s: no Windows imports on this platform", dll, function);
    return false;
#else
    void** slot = ImportSlot(dll, function);
    if (!slot) {
        HMODULE module = GetModuleHandleA(dll);
        void* export_ = module ? reinterpret_cast<void*>(GetProcAddress(module, function)) : nullptr;
        if (previousTarget) *previousTarget = export_;
        OBL_LOG("import %s!%s: not imported by the DLL, nothing to redirect", dll, function);
        return !previousTarget || export_;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
        OBL_LOG("import %s!%s failed: VirtualProtect (%lu)", dll, function, GetLastError());
        return false;
    }
    if (previousTarget) *previousTarget = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    OBL_LOG("import %s!%s redirected", dll, function);
    return true;
#endif
}

} // namespace obl::platform
