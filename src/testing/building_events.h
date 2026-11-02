#pragma once

#include <string>
#include <vector>

namespace obl::test {
bool RegisterBuildingEventTrace();
bool ExecuteBuildingEventCommand(const std::string& name, const std::vector<std::string>& args);
// Queues the save in slot (number or "last" = the slot of the latest save-panel) for loading on the next frame.
bool LoadSaveSlot(const std::string& slot, bool compareSavedEvents = false);
bool ConvertSave(const std::string& save, const std::string& output);
bool LoadOblSave(const std::string& path);
// An old binary save file (save<N>.dat): converted in process (obl-convert save) to testlogs/old_saves/<name>.obl.save,
// which loads through the bridge (save-switch.md).
bool LoadOldSaveFile(const std::string& path);
// `save-obl`: the game's obl.save writer (game::WriteOblSave) on the live world, with a fixed save time.
bool SaveObl(const std::string& path);
// True when the file is an obl.save document (JSON) rather than an old binary save: `start save` loads it through the bridge.
bool IsOblSaveFile(const std::string& path);
bool AwaitBuildingEventState(const std::vector<std::string>& args);
// Before the next script of a headless script list: forgets the saved event state, the expected load index and the
// save-panel slot, so a later script's load is not compared with an earlier script's save.
void ResetBuildingEvents();
// The round-trip oracle of the building events: before an explicit test save (serial-save, save-confirm) the live
// events of every building are kept by owner id, and the next load compares each loaded building with them.
void CaptureBuildingEvents();
// Drops the kept events: a load of another file (corpus, obl.save, a slot by number) is not compared with them.
void ForgetBuildingEvents();
} // namespace obl::test
