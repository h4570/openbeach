# Data formats: supported sources

Which copies of Beach Life the converter reads, how it tells them apart and where their data differs. All the
copies below are supported sources.

## Supported copies
A source combines an **executable build** with a **data revision**. The converter identifies them separately.

| Copy | Executable build | Data revision | Languages |
|---|---|---|---|
| Digital release (the reference installation) | `digital-2002-10` | `eu-2002-08` | French, German, Italian, Polish, Spanish |
| European CD (disc image or extracted disc) | `cd-2002-08` | `eu-2002-08` | as above |
| French/German CD (disc image or extracted disc) | `cd-2002-08` | `uk-2002-08` stamp, European tables | French, German, Italian, Polish, Spanish |
| UK/US CD *Virtual Resort Spring Break* (disc image or extracted disc) | `cd-2002-08` | `uk-2002-08` | English (`text_uk.ble`) |
| Late English CD (disc image or extracted disc) | `digital-2002-10` | `eu-2002-08` stamp, late scripts | English (`text_uk.ble`) |

The digital release supplies `BeachLife.exe` (2002-10-22) and four dgVoodoo wrapper DLLs;
it needs the data from a CD installation. In the reference installation, all 202 files shared with the European CD
(including `Data/`, `Music/`, `Photos/` and the Miles files) match byte for byte; `BeachLife.ini` is the Polish variant.
Applying the digital release to UK/US CD data is also supported. Recorded replays, world hashes and golden images
use the digital executable with European data only.

## Executable builds
These hashes identify supported inputs; the importer keeps the matching table in `SupportedExes()`.

| Build | SHA-256 | Linked | Copy |
|---|---|---|---|
| `digital-2002-10` | `983c098f69314c142ac8fd0d74fa34d098a49bd59f1aed88fb67a525c4b7f9e9` | 2002-10-22 | Digital release |
| `cd-2002-08` | `bdbc06034f298aff45acb49c1c085c1e10ff676fb7d5ad08b99ea33e01e2dd94` | 2002-08-17 | European CD |
| `cd-2002-08` | `2f56a5a52128909e211609cbdaadfb2fd67883869660663e909dbd430ff6a401` | 2008-07-09 (re-stamped) | UK/US CD |
| `cd-2002-08` | `2b2aa38d36fe34a799df7a6207f03c21b21afd028ef9e5f3a98137fbe26d52c0` | 2002-08-17 | French/German CD |

The CDs supply the same August game build for the tables the importer reads: the French/German executable differs from
the European one only in its copy-protection sections, so its GUI tables and resources convert to the same files. The
digital release, whose executable the late English CD also carries, supplies a later
October build with four different footprint grids and one additional script state. Tables occupy different locations
between builds; the converter locates them by content patterns ([converter/tables.md](converter/tables.md#executable-tables)).

- **Footprints:** 44 of 48 grids match. The pedalo hire, `BAR1`, casino and beach disco grids differ in a few cell flags
  and lower-layer values. A CD import keeps those differences.
- **Script vocabulary:** message names (`MT_*`) match. The October state table adds `STATE_GUEST_AEROBICS_AT_EVENT`,
  shifting later numeric ids. Scripts use names; the importer adjusts CD state ids to the engine's vocabulary and leaves
  the unused extra slot empty.

## Data revisions
Identified by the stamp at the start of `Data/Sprites/BLSprite.hdr` (and `.dat`): `2002:08:20 14:19:21` is `eu-2002-08`,
`2002:08:06 16:16:02` is `uk-2002-08`. The UK/US data is the **older** revision; the European one adds fixes.

Files that differ between the two revisions (everything else on both CDs is byte-identical):

| File | Difference |
|---|---|
| `Data/Script/Object/Guest.ble`, `Mechanic.ble`, `Shark.ble` | The European scripts carry three later fixes (one guest state line, the mechanic's move message, the shark's finishing state). |
| `Data/Scenario/Scenario1/Scenario1.ble` | Different times of the second day's boat events. |
| `Data/Scenario/Scenario9/Scenario9.pis`, `SandScenario9/SandScenario9.pis` | A different `WORLD_DEPTH_PLANE_OFFSET`. |
| `Data/Sprites/BLSprite.hdr`, `.dat` | Same size and the same 838 sprites; the metadata of two sprites and the pixels and palettes of three front-end buttons differ. |
| Text tables | European: `text_{french,german,italian,polish,spanish}.ble` plus `gui_polish.ble`, `gamewon_pol.ble`, `mainmenu_{fre,ger,ita,pol,spa}.ble` and the Polish photo font `photopol16`. UK/US: `text_uk.ble` only. |

Two more data sets mix these:
- **French/German CD:** the older scripts, scenarios and sprites of `uk-2002-08` with the European language tables. Its
  `text_polish.ble` differs slightly and `gamewon_pol.ble` is missing.
- **Late English CD:** the European sprite archive (hence the `eu-2002-08` stamp) with newer data than any other copy: a
  sixth event (`EVENT_AEROBICS`) and `*_COCKTAIL` lines in `BuildingList.ble`, new guest, mechanic and shark scripts,
  changed scenarios and a larger `GUI.ble`. Its file group is `UK Comp`.

Consequences for the converter:
- `pack.json` holds only an opaque `import.reference` over the identifying files' hashes
  ([converter/pack.md](converter/pack.md)); a pack is defined by the files it read, so a UK/US pack is a different data
  version with its own hash. `obl-convert identify` reports the build, revision and hashes.
- Language tables are optional per revision: the converter converts the text tables that exist. The game's language
  list already has an English slot (`text_uk`); English texts exist only on the UK/US CD.
- Unrecognized executable hashes or sprite stamps are refused with a clear message, not guessed. The hash table is
  `SupportedExes()` (`importer/src/source/source.h`); `IdentifyExe`/`IdentifySource` take another table, so tests let a
  synthetic executable stand for a build.

## CD cabinets
The CDs are InstallShield 6 cabinets (`data1.hdr`, `data1.cab`, `data2.cab`), with one directory per file group; the game
files are in `EURO_Comp` (European) and `UK_Comp` (UK/US), the language variants of `BeachLife.ini` in `Ess_<language>`.
The converter reads the cabinets itself (`obl-convert identify <CD root>`, `obl-convert
cabinet ...`, [converter/index.md](converter/cabinets.md#cabinets)), so no Windows install and no `unshield` is needed. The cabinet
lists the CD's `BeachLife.exe` as an external file of 1,613,824 bytes, while the file on the CD (the one installed)
has 2,211,279 bytes. Identification uses the actual file on the CD, not the cabinet entry's size.
