# C++ style

Code written by different models (and people) must look like one codebase. Formatting is mechanical and enforced by a
tool; naming and structure follow the conventions below.

## Formatting: clang-format
- Configuration: `.clang-format` in the repo root (clang-format **21.x**; other major versions format differently).
  It was tuned to the dominant style of the code base: 4-space indent, 140 columns, function braces on their own line,
  other braces on the same line, `Type* name`, short functions on one line only inside class bodies.
- `python tools/format.py` formats every tracked C/C++ file; `--check` lists unformatted files and exits 1;
  `python tools/format.py FILE...` limits it to files. Generated files (`*.gen.*`) and `third_party/` are skipped.
- Install the pinned binary once: `pip install clang-format==21.1.2` (the script also finds it on `PATH`).
- Run the check on touched files before every commit with C++ changes. The whole-tree reformat commit is listed in
  `.git-blame-ignore-revs`; use `git blame --ignore-revs-file .git-blame-ignore-revs` (or set `blame.ignoreRevsFile`).
- Use `// clang-format off` / `on` only where formatting would change meaning or never settle, with a short reason:
  - single-line MSVC `__asm instr` statements: the end of the line ends the statement, and clang-format would join the
    next statement onto it (a compile error at best);
  - X-macro lists (`#define LIST(X) X(a) X(b) ...`): clang-format bin-packs them and does not converge between runs;
  - tables whose alignment carries meaning.
- After a large reformat, check that only whitespace changed (token comparison) and build both configurations.

## Naming and structure
- C++20, namespace `obl` (sub-namespaces per area, e.g. `obl::game`). `obl` is the code prefix (`OBL_*`, `obl_*`, `obl-convert`).
- Functions and types `PascalCase`; locals and parameters `camelCase`; globals `g_name`; constants `kName`; members
  `m_name`. The full rules are the naming standard below.
- Headers use `#pragma once`. Include what you use.
- Comments explain *why*, not *what*; no commented-out code. Code and comments are in English.
- No raw `+0x` field access.

## Naming standard
Names describe what the code does in game terms. Every line you touch follows these rules.

- **Functions:** `PascalCase` verb phrase without underscores: `FindNearestSeaPoint`, `RestoreBackground`.
  The module comes from the namespace or class, never from a prefix (`Grid_FindBeachArea` -> `map::FindBeachArea`
  or `Grid::FindBeachArea`). Bool queries start with `Is`/`Has`/`Can`; `Find...` may fail (returns null/optional);
  `Get...` only for lookups that always succeed; `Count...`, `Create...`/`Destroy...`, `Load...`/`Save...` as usual.
- **A name states what the code does:** no guess markers, catch-all words or numbered duplicates (`Foo2`).
  If the meaning is unclear, find it from the tests and behavior before naming.
- **Behavior that owns state is a member function** of its type (`manager.AddJob(id)`), not a free function with a
  `self` pointer. A free function that takes an object names the parameter by its role (`guest`, `building`), never
  `self`/`_this`.
- **Types:** `PascalCase` nouns from the game's vocabulary (`Guest`, `Building`, `MapView`), no `C`/`T` prefixes.
- **Namespaces:** `obl::<module>` matching `src/<area>/<module>/` (`obl::map`, `obl::objects`, `obl::gui`);
  `obl::game` only for the game-loop module itself.
- **Fields and variables:** named by meaning; no `unknown`, `unk`, `field*`, `pad*`, `gap*`, `dword*`, and no
  Hungarian type prefixes (`dw`, `lp`, `p`, `sz`, `f`, `i`, `b`, `n`: `m_gameStartTime`, not `m_fGameStartTime`).
  Members `m_name`, locals and parameters `camelCase`, globals `g_name`, constants `kName`, enumerators `PascalCase`
  (as `.clang-tidy`). This includes unscoped id/state enums: `vehicle::lifecycle::Jetski`,
  `vehicle::messages::StateIdle`, `tables::Beauty`; reserve `kName` for non-enumerator constants.
- **Macros:** avoid them (`constexpr`, `enum class`, templates); a needed macro is `OBL_UPPER_CASE`, X-macro lists
  included.
- **Types and structure:** a type hierarchy and the split into files follow the design; a file is named after its subject (`water_anim.cpp`), one subject per file
  pair, never a grab bag (`*_leftovers`, `*_misc`, `small_functions`).
- **Beyond `src/`:** the same words are banned in test script names, manifest entries, tool names and doc pages; a rename updates `tests/`, `tools/` and `docs/` in the
  same commit.
- **Comments:** explain why; no plan-step or history references (history lives in git).
- Names fixed by our own data formats (pack keys, script vocabulary) are strings in data, not identifiers.

### Module patterns
The map family ([map.md](../architecture/map.md)) shows these patterns; other families follow them unless a family doc says why not.
- **Behavior lives in members** of the owning type (`grid->GetNeighbour(...)`), declared in the state header and defined in
  a `.cpp` named after the subject (`grid_areas.cpp`, `path_route.cpp`). Functions without an owner live in the module
  namespace (`map::CreateGrid`).
- **The game-bound glue of a pure header** is that subject's `.cpp` in the same directory, in the target list of
  `src/app/CMakeLists.txt` and the `REMOVE_ITEM` list of `tests/unit/linux/CMakeLists.txt`.
- **Start-up wiring is plain calls or setters** from the host's start-up (`map::SetPackDirectory(gameDir)`).
- **A header holds one subject** (`grid_areas.h`, `grid_screen_rect.h`), and its unit tests split with it.
- **Call counters:** a counter whose test has other behavioral expects (map hashes, goldens, world hashes) is dropped with
  its `call-report` lines; one that is the test's only assertion (optional goldens do not count) stays, named `<module>.<subject>` (`map.grid-areas`).
- **A rename of a shared core API** can keep an inline alias while each family moves its own callers; the alias goes with
  the last one.
- **Field renames:** a member is renamed repo-wide when it has one declaration; a name shared by several structs of another
  family waits for that family. **Strings stay**: save keys, snapshot labels and
  script words are data (`"m_uiFlags"` in `grid_save.h` is a save record name), and a changed world hash or snapshot
  means a string was renamed by mistake.

## Naming lint: clang-tidy
`.clang-tidy` enforces the naming rules above (`readability-identifier-naming`) plus a small set of bug-prone checks
(dangling handles, use after move, `sizeof`/`memset` mistakes, integer division, infinite loops) and the modern
ownership/cast guard: `modernize-use-nullptr`, `modernize-use-override`, `cppcoreguidelines-owning-memory` (a bare
`new`/`delete` on a touched line needs a `std::unique_ptr`/container owner) and `bugprone-casting-through-void`
(`static_cast<T*>(static_cast<void*>(p))` hides a `reinterpret_cast`; use `std::bit_cast` or `core/byte_reader.h`).
`python3 tools/tidy.py [--base REV]` reports findings **only on the lines a change adds or modifies** (default base: the
merge base with `origin/next`), because most old code predates the rules; it converges as modules are refactored.
- Linux only (CI job `tidy`, or WSL): needs `clang-tidy`, `clang`, `ninja` and the MinGW headers of
  `tools/windows_syntax_linux.sh`. It configures `build/linux-tidy/` for `tests/unit` + `src/common` and asks
  `windows_syntax_linux.sh --compdb` for Windows-only sources; a changed header is checked through one file that includes it.
- A finding on a line you touched is fixed, not suppressed. Names fixed by our data formats are strings in data, not
  identifiers, so they need no suppression.
