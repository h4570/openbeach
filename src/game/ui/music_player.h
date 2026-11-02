#pragma once

namespace obl::audio {
struct MusicPlayer;
} // namespace obl::audio

namespace obl::audio {
// The GUI manager's music player, null before the GUI exists.
audio::MusicPlayer* GetMusicPlayer();
} // namespace obl::audio
