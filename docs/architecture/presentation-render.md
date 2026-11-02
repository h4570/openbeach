# Presentation: blit, sprite and renderer facades

Part of [presentation.md](presentation.md).

## UI runtime subjects

`image.cpp` owns image allocation, loading and palette conversion through `ui::images`.
It shares `ui::RendererImage` with the renderer. `blit_runtime.cpp`, `line_draw_runtime.cpp`
and `palette_modifiers_runtime.cpp` connect the portable algorithms in their subject `.cpp`
files to the current renderer. The runtime files build into the game and headless host;
standalone units link the portable algorithms only.

`text_renderer.cpp` implements `ui::font_text`; `text_encoding` and `text_observer` supply UI
encoding and draw observation. `text_table.cpp` owns `ui::text_table`, including narrow and
wide lookup, resolution labels and the shared empty result. `save_slots.cpp` owns
`ui::save_panel` and pending save loads; the pure sorting and path helpers are in `save_slots.h`.

Start-up calls `Register*Drivers` only to attach synthetic test drivers. Runtime rendering
and save-panel calls use these APIs directly. Counters kept as fixture assertions use
`ui.<subject>` keys.

## Primitive facade

`pixel_primitives::` in `src/game/ui/blit.h` and `line_draw.h` is the entry point. Building and scenery
light masks, weather, water animation, renderer image drawing, guest and staff markers and GUI line drawing call it
directly. The synthetic blit drivers also call this API. `RleCopy32` takes a screen rectangle by value. `DrawLine` accepts
the opaque renderer handle. Both blit consumers use the `ui::RendererSurface` record in `game/ui/renderer_types.h`.

Sprite instance draws use UI, type, depth and shadow selectors, previews use `ActorSelect` and the four `DepthRamp*`
functions, and bakers read `GetMaskHalf`/`GetMaskQuarter`. `RegisterBlitDrivers` only registers the synthetic drivers.

`SpriteInstanceDraw` in `sprite_instance_render.h` calls `NotifySpriteDraw` before counting and drawing.
`SpriteAnimConstruct/Destruct/Unload/LoadAll/LoadDirection/GetFrame` in `sprite_anim.h` handle animations with lazy frame
loading. `GetFrame` takes a mutable animation because it may load frames. Bank loading, destruction and cache eviction call
those functions.

`SpriteBankLoad`, `SpriteCreate`, `SpriteBankCreateSprite`, `SpriteBankReleaseSprite`, `SpriteBankUnloadStale`,
`SpriteConstruct` and `SpriteDestruct` in `sprite_bank.h` manage banks and instances (`void*` for callers that hold them
untyped), with observer notification (bank flag), call counters and the "dummy" fallback.

The sprite records `ui::SpriteAnim`/`Sprite`/`SpriteBank`/`SpriteInstance` live in `src/game/ui/sprite.h`.
`Game::m_spriteBank` is a typed `ui::SpriteBank*` that Game construction and destruction load and unload.
`GetSpriteBank`/`SetSpriteBank` (`sprite_bank.h`) are the access to it.

## Renderer facade

`renderer::` in `src/game/ui/renderer_api.h` exposes ordinary calls for the renderer backend, over opaque handles. The
facade covers pixel-format initialization, clipping, fills, surface allocation, access and blits, image draws, water
effects, captures, display setup and presentation. Backend implementations keep their counters, fill observers, surface
origins, dirty-rectangle order and pixel arithmetic. The headless host has a null renderer and acquires no display.

## Renderer records

`src/game/ui/renderer_types.h` owns `ui::Renderer`, `RendererFade`, `RendererSurface` (with `SurfaceAt`),
`WaterAnim`, `WaterBlockImage`, the borrowed `RendererImage` view, `RendererLoadingGradient` and `ShimmerObject`.
`screen::Rect` lives in `core/rect.h`.

## Isometric tile footprint

The building bake gate uses a 2:1 diamond in a 62 by 32 pixel footprint, with
one transparent bottom guard row. Sample pixel centres in doubled coordinates:
`abs(2*x - 61) + 2*abs(2*y - 31) <= 63`. This includes the boundary, gives
2 pixels at each tip and 62 on each of the two middle rows, and covers 1,024 pixels.
`sprite_bake::IsoTileMask` computes this geometry directly; no baked pixel table is stored.
