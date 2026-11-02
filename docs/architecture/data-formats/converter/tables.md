# Converter: tables

## Executable tables
Some tables exist only in the executable of the player's copy. The importer reads them from that file during the import and
writes them to the pack, so none of them is in this repository. Their place differs between the supported builds
([sources.md](../sources.md)), so each table is found by its own shape, every candidate is checked, and ambiguous input stops
the import with an error.

- **Footprints:** 48 grids of `width`, `height` and 100 rows of 25 `int32`, one per building key of the player's `BuildingList`.
  The catalog writer refuses a grid whose size is not the building's `GRIDWIDTH`/`GRIDHEIGHT` or that belongs to no building.
  A CD import keeps the four grids of its own build.
- **GUI tables** ([game-data.md](../game-data.md) "GUI tables"): column headings (27 records), guest filters (19), building
  filters (16), cell types (9), staff filters (7), hire types (6), events (6, or 5 on a CD), the row buttons (8 sprite names and
  tooltip keys) and the credits (205 rows).
