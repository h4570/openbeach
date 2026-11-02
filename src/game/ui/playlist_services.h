#pragma once

namespace obl::audio {
struct MusicPlayer;
} // namespace obl::audio

// Platform-provided playlist services: folder/shortcut scanning and settings persistence.
// The music panel depends on this game-owned contract, while the platform owns filesystem access.
namespace obl::platform {

void ScanMusicFolder(audio::MusicPlayer* player, bool rescan);
void SyncPlaylistToSettings(audio::MusicPlayer* player);

} // namespace obl::platform
