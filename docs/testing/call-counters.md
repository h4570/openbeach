# Call counters

`test::CallCounter` (`src/core/call_counter.h`) is a named counter for game calls and test fixture checks.
Counts span the process lifetime, except that the next script of a headless script list starts them from zero
(`StartNextScript`, [../architecture/headless/scripts.md](../architecture/headless/scripts.md) "Script lists").

## API

| Call | Meaning |
|---|---|
| `test::CallCounter g_x("Name")` | Registers a counter under the name scripts use. |
| `g_x.Count()` | Counts one call answered by the C++. Use this in counted functions. |
| `g_x.Record(ok, detail)` | Counts a golden/model check. A failed check logs `autotest: FAILED counter-check` and fails the test directly; the first eight failures include details. |
| `g_x.SetDriver(fn)` | Attaches a fixture for `call-drive Name N`, for paths normal gameplay rarely reaches. |

`Record(true, ...)` calls that checked nothing have all become `Count()`. Real golden/model checks remain `Record` calls.
Their failures do not depend on a later report or a script assertion.
`tests/unit/call_counter_test.cpp` covers direct failure logging, bounded failure details, driver calls and rejected commands
without a game process on Windows and Linux.

## Script commands

- `call-report <Name>` logs `autotest: call-report <Name> calls=N`. Read the count with `expect-last calls N` or
  `expect-last-range calls 1 1000000`.
- `call-drive <Name> <N>` runs the counter's fixture. A fixture may exercise several functions; only the entry points scripts
  actually drive are registered. The command logs `calls-delta=N` for calls added to that counter during the fixture;
  use `expect-last calls-delta N` for an exact fixture count without discarding startup calls.

The report has no `mismatches` or `skipped` keys. There is no reset or mode command. Scripts retain real `mismatches`
assertions from `desire-report`, whose reports are separate from these counters.

## Maintaining counters

Pattern modules: `src/game/world/world_queries.cpp` (shared query fixtures) and `src/game/ui/cursor.cpp` (unused
drivers removed).

1. List the scripts' `call-report` and `call-drive` names before changing counters or drivers.
2. Use `Count()` in game code. Keep `Record(expr, ...)` for golden/model checks in fixtures.
3. Delete unused driver registrations and helpers. Keep shared fixtures if another registered entry point uses them.
4. Delete dead comparison branches, snapshots and counters; retain every counter a script reports.
5. Remove unused includes. Format, build and run the affected scripts with `./tools/runtests.ps1 -Filter '<module>*'`.
