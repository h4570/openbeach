# Sprites

`src/game/ui/sprite.h` declares the animation cache, sprite resources, bank and per-object animation instance in
`obl::ui`. Runtime implementations build into the game host; the frame arithmetic and pixel planners remain portable.

- `sprite_anim.cpp` loads converted frames, either together or by direction. `ConfigureSpriteAnimations` selects the
  pack directory and registers the cache driver.
- `sprite_bank.cpp` loads resources, creates and releases instances, and sweeps idle animation caches.
  `ConfigureSpriteBank` selects the directory and registers the bank driver. `GetSpriteBank` and `SetSpriteBank`
  access the active world's bank.
- `sprite_instance_anim.cpp` advances clocks and plays animation sounds; `sprite_instance_geometry.cpp` computes
  anchors and depth; `sprite_instance_scan.cpp` performs opacity queries.
- `sprite_instance_render.cpp` selects pixel primitives. `sprite_instance_draw.cpp` handles clipping and palette tones.
  `sprite_instance_preview.cpp` draws placement and construction previews; `sprite_instance_bake.cpp` paints map
  backgrounds. `RegisterSpriteBakeDrivers` registers deterministic map fixtures used by the sprite tests.

Operations on a sprite record are members. Internal helpers take role-named inputs. Explicit `Construct`/`Destruct`
methods keep allocation and initialization at the existing ownership boundaries; adding methods does not add a vtable.
Unused layout-only fields are removed. Actor rendering retains its byte option as `m_actorRenderOption`.

Save keys and snapshot labels retain their exact spelling even when a C++ member is renamed. Sprite tests keep the
existing frame and background hashes; the headless suite checks world behavior against its existing references.
