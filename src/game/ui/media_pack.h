#pragma once

#include <filesystem>
#include <string>

// The converted music and video of the pack (`obl-convert media`, docs/architecture/data-formats/assets.md "Music and video"):
// `<pack>/music/<name>.mp3` and `<pack>/video/intro.mpg`, byte copies of the game's files. The engine opens them relative to
// the game directory, like the other packs/base loaders. Pure logic: no Windows APIs.
namespace obl::media_pack {

// The soundtrack directory with the trailing separator the playlist adds the file name to (settings_ini.h, the playlist's
// night stream in game_states.h). It is the directory name saved in the ini, so it stays as it is.
inline constexpr const char* kTrackDirectory = "packs/base/music/";

// The intro movie in the selected pack's layers.
std::string IntroPath(const std::filesystem::path& pack);

// The directory the engine opens the playlist track `file` from, with the trailing separator (the caller appends `file`). A
// converted track (`directory` == kTrackDirectory) is looked up in the pack layers of `pack` (pack_path.h): `mod/music/<file>`
// shadows `<pack>/music/<file>`. Any other directory is returned as it is.
std::string TrackDirectory(const std::filesystem::path& pack, const char* directory, const char* file);
// The file the engine opens for the playlist track `file` of `directory`: TrackDirectory + file.
std::string TrackPath(const std::filesystem::path& pack, const char* directory, const char* file);

// False with `error` when `path` cannot be opened. The message names the file and the setup step that writes it.
bool CheckReadable(const std::string& path, std::string& error);

} // namespace obl::media_pack
