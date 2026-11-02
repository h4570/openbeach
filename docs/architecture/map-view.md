# Map view

`ui::MapView` owns the camera, background block table and rebuild queue. Its member functions are declared in
`src/game/ui/map_view.h`. The host stores the current view in `GameContext`; `map_view_access.h` exposes that service.
The renderer supplies the viewport dimensions and zoom. The camera uses world time for scroll interpolation.

## Camera and projection

`map_view_camera.cpp` applies the pure `map_camera.h` rules: positions clamp to the zoom-dependent map margins,
velocity is limited and damped, locked cameras stop velocity movement, and a timed scroll moves the camera to its
destination before unlocking it. Integer camera coordinates follow the floating position at frame boundaries.
The viewport excludes the bottom GUI bar. Camera updates also stream nearby blocks, process queued rebuilds,
update water animation and move the sound listener to the visible centre.

`map_view_math.cpp` samples the world's height map and supplies terrain height, depth and coordinate conversion.
`map_view_projection.cpp` projects world coordinates relative to the camera and terrain height. Output buffers may
alias the input where the API allows it. `WorldToInt` and `WorldToTile` are static because they need no view state.

## Background and effects

`map_view_blocks.cpp` builds a temporary interleaved colour/depth cache, maps blocks near the view and unmaps distant
blocks. Headless runs use zero-filled block storage. `map_view_restore.cpp` restores background rectangles and tile
masks. `map_view_rebuild.cpp` queues block rectangles and repaints them with terrain and building images as needed.
`map_view_effects.cpp` draws sea blocks, darkens the scene by depth and draws highlighted tiles.

`map_view_culling.cpp` clips visible object rectangles against the viewport and draws objects and overlays in order.
It reports the share of sea tiles under the view for ambient sound.

## Island image cache and lifecycle

`ui::MapCache` owns a four-slot round-robin cache of 128 by 128 colour/depth blocks. Its methods in `map_cache.cpp` read
the player-local map pack planes; headless runs leave those planes zero. A cache hit keeps its slot, a miss replaces
the next slot, and colour conversion to RGB555 follows the renderer's format. `SetMapPackDirectory` supplies the
pack root during host startup. The portable cache algorithms live in `map_cache.h`.

`map_view_lifecycle.cpp` binds the view to the host, creates the island cache and releases mapped blocks and cache
storage during teardown. Loading restores the rebuild queue and updates the camera. Save keys and snapshot labels
are stable data contracts even where the C++ fields have different names.

## Verification

Unit tests cover camera interpolation and clamps, projection, block geometry, culling, effects, cache replacement,
rebuild/restore masks and lifecycle. `map_view_camera.txt` checks camera positions and zoom changes;
`map_view_blocks.txt`, `map_view_rebuild.txt` and `map_view_restore.txt` check background hashes;
`map_view_culling.txt` checks object visibility. `map_view_math.txt` and `biz_map.txt` exercise the runtime bindings,
including a save/load round trip. Effects have optional golden images and retained call counters. Headless replays
and the scenario/save tests protect world hashes without recording new references.
