# Presentation

Presentation covers everything that turns the world into pixels and sound and the player's clicks into commands: messages
to the GUI, the GUI (`gui*.h`), the map view, sprites, the renderer, fonts, the cursor and audio. `src/game/ui/` holds the
pure presentation model (widgets, map view state, sprite instances, text layout) and `src/platform/` the SDL/OS backends
(window, blits, sound output); `tools/check_layers.py` enforces the split.

## Pieces
| Piece | Where | Notes |
|---|---|---|
| Sprites | `game/ui/sprite.h` and the `sprite_*` files | `SpriteInstance` is embedded in objects |
| Map view | `game/ui/`, [map-view.md](map-view.md) | owned by the World |
| Renderer | `game/ui/renderer_types.h`, `renderer_api.h`, `platform/renderer_*` | the back buffer and image cache; headless keeps a null one |
| GUI | `game/ui/gui_*` | the largest family; scripts (`gui_script.h`) build the widget tree |
| Messages to the GUI | `message_facade::` | shared with the objects |
| Fonts and text | `game/ui/font.h`, `text_renderer.h` | [presentation-fonts-audio.md](presentation-fonts-audio.md) |
| Cursor | `game/ui/cursor_types.h` | owned by `Game` |
| Audio | `game/ui/audio_types.h`, `sound_types.h`, `loop_sound_types.h` | `ScriptedSound` is owned by the World |
| Input | [input.md](input.md) | |

## Files worth knowing
- `sprite_baker::` (`src/game/ui/sprite_instance_bake.h`) is the bake API of the background bakers: `BakeBuilding`,
  `BakeBuildingRect`, `BakeMasked`, `BakeGhost`, `DrawGroundTile`. The writers are private. The clip rectangle is taken by
  value.
- `sprite_instance_{anim,bake,draw,geometry,preview,scan}.cpp` and `sprite_instance_render.{h,cpp}` draw and animate sprite
  instances; `sprite_bank.h` loads banks.
- `sprite_pack`, `sprite_animation`, `sound_bank`, `scripted_sound_pack.cpp` and the `gui_content`, `gui_layouts` files
  read the pack's formats. `gui_content_schemas.h.in` embeds their schemas.
- `SoundDestruct` lives in `platform/sound_lifecycle.cpp`: it closes samples and the music stream before teardown, then
  checks SDL cleanup. The game provides `SoundShutdownMusic` for it. `playlist_services.h` is the game-owned folder-scan and
  settings-sync contract; its implementation is `platform/playlist_directory_scan.cpp`, including filesystem handling. The
  panel includes only that contract.
- The Linux unit build drops `game/ui/sound_bank.cpp` without SDL.

## Safety net
World hashes do not see presentation (the snapshot has no map-view or GUI section), so the net is:
- **Golden images** ([../testing/golden-images.md](../testing/golden-images.md)): `golden_frames`, `golden_sprites`,
  `golden_sprites_zoom`, `m6_gui_golden`, pixel-exact on fixed frames.
- **Map-hash fixtures**: the `call-drive` scenes of the bakers and the map view (`sprite_bake`, `sprite_overlay`,
  `sprite_ground`, `map_view_*`) hash the background blocks after synthetic draws.
- **Call counters** prove a code path ran.
- Replays and bots run for every change that touches a draw callback of an object (they catch crashes and any write into
  simulation state from a draw path).

## Topics
- [presentation-render.md](presentation-render.md): the blit and line primitives, the sprite and renderer facades and the
  sprite and renderer records.
- [map-view.md](map-view.md): the map view families and `ui::MapView`.
- [presentation-fonts-audio.md](presentation-fonts-audio.md): fonts and text, audio records and APIs.
