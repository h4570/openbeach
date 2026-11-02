#include "binary_serial.h"

#include "game/game/save_load.h"
#include "building_events.h"
#include "core/log.h"
#include "game/ui/save_slot_header.h"
#include "test_util.h"
#include "core/os.h"

#include <filesystem>

namespace obl::test {
namespace {
void Report(bool same, const char* label)
{
    OBL_LOG("autotest: %s serial %s", same ? "PASS" : "FAILED", label);
}

int g_slot = -1;
std::string SlotPath(int slot)
{
    return os::ResolvePath(save_slots::OblSlotPath(slot));
}
int FreeSlot()
{
    int slot = 9000;
    while (save_slots::SlotFileExists(slot)) ++slot;
    return slot;
}
bool SettledWorld()
{
    if (!World() || GameStateIndex() != 0 || Game()->m_guiReloadPending || g_slot >= 0) {
        Report(false, "save-needs-settled-world");
        return false;
    }
    return true;
}
void Copy(const std::string& from, const std::string& copy)
{
    if (copy.empty()) return;
    const std::filesystem::path target(copy);
    std::error_code error;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), error);
    Report(std::filesystem::copy_file(from, target, std::filesystem::copy_options::overwrite_existing, error), "save-copy");
}

// Writes the live world into a free save slot (9000 and up) as the save panel does (game::WriteOblSave), with
// a fixed save time so the file does not depend on the wall clock, and copies it to `copy` (relative to the game directory)
// when given: the save a headless replay starts from (`start save`, docs/architecture/headless/index.md).
void SaveWorld(const std::string& copy)
{
    if (!SettledWorld()) return;
    g_slot = FreeSlot();
    CaptureBuildingEvents();
    std::string error;
    const bool written = game::WriteOblSave(SlotPath(g_slot), 1790841600u, error);
    if (!written) OBL_LOG("autotest: obl.save write: %s", error.c_str());
    Report(written, "world-save-nonempty");
    if (written) Copy(SlotPath(g_slot), copy);
}

void RemoveSlot()
{
    std::error_code error;
    if (g_slot >= 0) std::filesystem::remove(SlotPath(g_slot), error);
    g_slot = -1;
}
} // namespace

void ResetBinarySerial()
{
    RemoveSlot();
}

bool ExecuteBinarySerialCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "serial-save" && args.size() <= 1)
        SaveWorld(args.empty() ? std::string() : args[0]);
    else if (name == "serial-load" && args.empty()) {
        if (g_slot < 0)
            Report(false, "load-needs-save");
        else
            LoadSaveSlot(std::to_string(g_slot), true);
    } else if (name == "serial-load" && args.size() == 1) {
        // Any save file relative to the game directory, through a scratch slot (the game's own file path): an obl.save is
        // copied there, an old `.dat` (the save corpus, tools/save_corpus_hash.ps1) converted there in process.
        std::error_code error;
        if (g_slot >= 0 || !std::filesystem::is_regular_file(args[0], error)) {
            Report(false, g_slot >= 0 ? "load-file-needs-cleanup" : "load-file-missing");
        } else {
            // The slot file stays for a later serial-load and serial-cleanup.
            const int slot = FreeSlot();
            const bool ready = IsOblSaveFile(args[0]) ? std::filesystem::copy_file(args[0], SlotPath(slot), error)
                                                      : ConvertSave(args[0], SlotPath(slot));
            if (ready) {
                g_slot = slot;
                LoadSaveSlot(std::to_string(g_slot));
            } else {
                Report(false, "load-file-copy");
            }
        }
    } else if (name == "serial-cleanup" && args.empty()) {
        RemoveSlot();
    } else
        return false;
    return true;
}
} // namespace obl::test
