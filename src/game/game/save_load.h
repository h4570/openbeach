#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "game/objects/object_manager_serial.h"
#include "game/ui/map_view.h"

namespace obl::world {
struct World;
} // namespace obl::world
namespace obl::object {
struct Base;
} // namespace obl::object
namespace obl::map {
struct Grid;
} // namespace obl::map
namespace obl::player {
struct Player;
} // namespace obl::player

namespace obl::game {
// The obl.save load (docs/architecture/data-formats/converter/save-load-bridge.md): reads and validates the document with the
// catalog and pack identity of the game's pack (packs/base), checks every section and object, and queues it for the next
// ui::save_panel::LoadPendingSave (ui::QueueSaveLoad), which builds the world from it (world::LoadFromSave, obl_load below). The caller
// switches to the main menu as for a slot load. False with a reason when the file, the pack or a check fails; nothing is
// queued then.
bool QueueOblSave(const std::filesystem::path& file, std::string& error);

// The native side of a queued obl.save load, current while world::LoadFromSave builds the world.
namespace obl_load {
// Whether the load in progress reads the section of this old section kind ("World", "Player", ...) from the document instead of
// the records. False outside an obl.save load.
bool ReadsSection(std::string_view kind);
// The world load's own fields; returns the script state for the world script's set-state step. QueueOblSave checked the section, so a refusal
// here is a fatal error.
uint32_t ReadWorld(obl::world::World& world);
// The grid load's records; the section must fit the island's grid (cell count, one open flag per area), else a fatal error.
void ReadMap(obl::map::Grid& grid);
// The object manager load from the document (save-switch-objects.md): the manager's block from `managers.objects` and the object
// list, then per object its header (`objects[index]`: the type byte and the creation name) and its body (ReadObject, `type` from
// the header). QueueOblSave checked every entry, so a refusal here is a fatal error.
obl::object_manager::SavedHeader ReadObjectManager();
void ReadObjectHeader(size_t index, uint8_t& type, std::string& name);
void ReadObject(obl::object::Base& object, uint8_t type, size_t index);
// The player load's and the map view load's fields (the view's burn queue appended and its camera brought in line, as the map view load ends).
void ReadPlayer(obl::player::Player& player);
void ReadView(ui::MapView* view);
// A manager section by its old section kind ("FerryManager", "Weather", ...) into the world's manager, through the module's
// LoadFields with its post-steps.
void ReadManager(std::string_view kind, obl::world::World& world);
} // namespace obl_load

bool WriteOblSave(const std::filesystem::path& file, uint32_t timestamp, std::string& error, size_t* objectCount = nullptr);
} // namespace obl::game
