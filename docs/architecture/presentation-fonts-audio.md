# Presentation: fonts, text and audio

Part of [presentation.md](presentation.md).

## Fonts and text

`src/game/ui/font.h` owns `ui::BitmapFont`, `ui::GdiFont`, glyph rectangles and `ui::TextRenderer` state. The atlas is an
opaque renderer image pointer; atlas allocation, palettes and surface locking belong to the renderer backend. The font pack
and pure text algorithms use these types.

`ConstructBitmapFont` and `ConstructGdiFont` in `font_construct.h` are the construction API.
`font_text::` in `text_renderer.h` exposes construction, setters, metrics, truncation and drawing. GUI callers
and the bitmap-font test driver use these functions directly. `Game_Construct` builds the debug font with
`ConstructGdiFont`.

The outer encoding layer: font names containing `Pol` select CP1250 on Windows, other names select CP1252; code units
already below 256 pass through unchanged. Observers see the caller's text before encoding, and wrapped draws report only the
whole string. Truncation keeps a temporary buffer and spare ellipsis space. Glyph stride is 255, with signed drawing kerning
and 16-bit metric advances. The debug font is blank on every host (no system font is rasterized; nothing draws it).

## Audio

`src/game/ui/audio_types.h` owns the scripted-sound records. World owns a typed `audio::ScriptedSound`,
constructed and loaded through `scripted_sound_runtime.h` and destroyed before its storage is released. The API covers
pack loading, sprite round-robin indices, building lookup, teardown order and the runtime and load counters.

`src/game/ui/loop_sound_types.h` owns the loop and queued sprite records. The two lists are `std::list` members.
`loop_sound_control.h` exposes start, stop, enqueue and per-list updates, used by buildings, vehicles, scripted sound and
guests. A guest's random-sound arguments are captured at its enqueue call (position copy, volume, group and RNG assertions).

Game construction, destruction and frame update (`game.cpp`) and the Game test enqueue (`world_test_state.cpp`) call these
functions directly. The sample, bank and stream operations are members of `audio::Sound` and `audio::SoundBankRecord`
(`sample_mixer.cpp`, `music_backend.cpp`, `sound_lifecycle.cpp`); `sound_api.h` only declares `audio::CurrentSound()`. The
audio family lives in `obl::audio`. `ScriptedSound::PlayEvent` in `scripted_sound_runtime.h` plays a guest's opinion sound,
with its RNG draw, position copy, category and group selection, test capture and call counters. Game builds Sound through
`Sound::Construct`.

`src/game/ui/sound_types.h` owns the `Sound`, `SoundChannel`, sample bank header (`SoundBankRecord`; the name
`audio::SoundBank` is the sample store of `sound_bank.h`), `PlaylistTrack`, `Playlist` and `MusicPlayer` records.
`Game::m_sound` and `m_loopSounds` are typed. Readers use `audio::CurrentSound()` (`sound_api.h`) and `CurrentLoopSounds()`
(`loop_sound_control.h`); only the owners write the globals behind them (`sound_lifecycle.cpp`, `loop_sound_control.cpp` and
the scratch-object sample test), which is why the accessors read the globals and not the Game: the LoopSounds construct
check and the sample test swap the global for a scratch object.
