# Scenario bots

A safety net next to the command replays: a bot that wins a scenario headless, so a change that
makes it unwinnable (or wins it by accident) fails a test. A bot is ordinary code that **reads** the world
through a copied snapshot and **acts** only through commands ([commands.md](../commands.md)), the same input the GUI
produces; it never writes game memory, so whatever a bot achieves a player can achieve.

Code: `src/game/bot/bot.h` (API, verdicts, registry, run file; pure, built for Windows and Linux), `src/game/bot/bot_<name>.cpp`
(the bots), `src/testing/bot_host.cpp` (live view, queries, run loop), `src/game/game/scenario_outcome.cpp` (verdict).
Tests: `tests/unit/bot_test.cpp` (decisions on hand-made views), run files `tests/bots/*.bot`, ctest `headless_bot_*`,
`tools/bot_runs.py` (evidence), `tools/tests/test_bot_runs.py`.

## Parts
| Part | Content |
|---|---|
| [contract.md](contract.md) | The bot contract (world view, queries, commands), win and loss from the campaign scripts, run files and the headless host, and the placement rules. |
| [metrics.md](metrics.md) | Metric samples, tolerance bands and how they are recorded, `tools/bot_runs.py` and its evidence, and the `OBL_BOT_OPINIONS` diagnostics. |
| [writing-a-bot.md](writing-a-bot.md) | Steps for a bot for a new scenario. |
