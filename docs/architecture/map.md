# The map module (grid and paths)

`src/game/map/`: the world's terrain grid, the route search on it, the routes the moving objects follow, the path tiles (the
ground look of paving and grass) and the sea animation. Pure headers hold the logic (template Env where they reach the game);
the subject-named `.cpp` files next to them connect it to the running game. Layers: `tools/check_layers.py`.

## Grid (`grid_state.h`)
- `map::Grid` owns the cells (`std::vector<GridCell>`, row-major, `Cell(x, y)` wraps like `TileIndex`), the height map
  (`std::optional<map_pack::HeightSamples>`, none = flat; `HeightMap()` answers a pointer or null), the beach and sea areas
  (`std::vector<Area>` in script order), the boat route (`BoatRoute`: at most ten points; `AddBoatPoint` drops the rest) and the
  island size. `Cell` is shallow-const: a const grid still hands out writable cells.
- `GridCell` (`grid_cell.h`): `m_flags` (terrain and occupation bits), `m_zOffset` (depth) and the list of objects on the tile
  (`cell_objects.h`: `AddObject`, `RemoveObject`, `ContainsObject`).
- Queries are members, defined by subject: neighbours and tile/world conversions in `grid_queries.cpp` (pure math in
  `grid_math.h`), areas and the boat route in `grid_areas.cpp` (`grid_areas.h`), the screen-to-tile rectangle in
  `grid_screen_rect.cpp` (`grid_screen_rect.h`) and the debug tile-class map in `grid_class_map.cpp` (`grid_class_map.h`).
- Areas are only added while the world script builds the world, so a `const Area*` stays valid for the world's life (the
  lifeguard keeps one). `AreaAt` answers null past the list; the area queries end in `fatal::UnhandledInput` there.
- Lifetime: `map::CreateGrid`/`DestroyGrid` (`grid_lifecycle.h/.cpp`), called by world construction and teardown. Cells are
  built in order and torn down from the last one. `map::SetPackDirectory` (from `dllmain.cpp`) names the directory the
  converted map packs are read from.
- Save records (`grid_save.h`, `map::VisitFields`): `sizeof`, the island size, one open flag per beach and sea area, then flags
  and depth per cell. The save block and the world snapshot both visit this table; the record names are part of the save
  format and do not follow field renames.

## Route search and routes (`path_state.h`, `path_search.h`, `path_route.h`, `path_data.h`)
- `path::Path` owns the `PathSearch` by value (neither copied nor moved: the open list points into it). The search keeps one
  `path::Node` per grid cell (`path_node.h`), sized on the first search and never cleared between searches; route order
  depends on those stale values, so keep it that way.
- `Path::CreateLandRoute`, `CreateLandOrBlockedRoute`, `CreateSeaRoute` and `CreatePavementRoute` (`path_route.cpp`) store
  the matching tile predicate (`path_predicates.cpp`) in the search and run it. `Path::IsTileAllowed` answers the movement
  messages. `path::PrepareRouteEndpoints`/`ClearTemporaryRouteTiles` (`path_route.h`) mark and clear the endpoint tiles.
- `path::Route` and `path::RouteCursor` (`path_data.h`) belong to the moving objects; `path::IsCursorStraight` and
  `ReleaseCursor` act on a cursor.
- Lifetime: `path::CreatePath(grid)`/`DestroyPath`, which also set and clear the alias `game::GetPath()` reads.

## Path tiles (`path_tiles_state.h`, `path_tiles.h`, `path_tiles_lifecycle.h`, `path_tiles_rebuild.h`)
- `path::PathTiles` holds the tile sprites as opaque `SpriteHandle`s (`path::SpriteOf`/`HandleOf` convert), the lookup tables,
  the surface fields, the full-tile frame and the pavement and grass sprite names (`GetPavementSpriteName`,
  `GetGrassSpriteName`, `GetFullTileFrame`, `GetTilesetNumber`).
- The rebuilds are members (`RebuildTile`, `RebuildAll`, `RebuildRect`, `UpdateBuildingOverlap`, `SetSurface`,
  `AddNeighbourBits`, `RemoveNeighbourBits`; `path_tiles_rebuild.cpp`) over the pure templates; they reach sprites only through
  Env calls. `tests/path_tiles_rebuild.txt` pins their result with `map-hash` values.
- Lifetime: `path::CreatePathTiles(pavement, beachEdge)`/`DestroyPathTiles` (`path_tiles_lifecycle.cpp`).

## Sea animation (`water_anim.h`)
- Pure functions over `ui::WaterAnim`: the frame for a game time, the mask decode and the per-block mask choice.
  `water_anim_renderer.cpp` loads the converted sheet from `map::PackDirectory()`, updates it every frame and draws the water
  blocks for the renderer. `tests/water_anim.txt` compares frames at three zoom levels with goldens (optional) and counts the updates and block draws.

## Tests
- Unit: `tests/unit/grid_*_test.cpp`, `path_*_test.cpp`, `water_anim_test.cpp`.
- Game: `grid_areas`, `grid_lifecycle`, `grid_queries`, `path_search`, `path_tiles`, `path_tiles_lifecycle`,
  `path_tiles_rebuild`, `water_anim` (`tests/manifest.txt`). Where a script's only assertion is that the code ran, it reads a
  `map.<subject>` call counter (`call-report map.grid-areas`); scripts with behavioral expects carry no counters.
