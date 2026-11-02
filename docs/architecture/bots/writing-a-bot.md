# Scenario bots: writing a bot

## Writing a bot

For a new scenario: check the goal in the converted scenario script (`scenarios/<id>/world.wsc` in the pack,
`STATE_WORLD_RUNGAME`; game data, never committed), add the view fields and
queries it needs here, write `src/game/bot/bot_<name>.cpp` with a unit test on a hand-made view, register it in `kBots`
(`bot.cpp`), add `tests/bots/<name>.bot`, its ctest entry (`obl_bot_test(<name>)` in `tests/unit/CMakeLists.txt`) and
its bands (`tools/bot_runs.py --record --filter <name>`).

## Plan files

The per-scenario tables of a bot (home buildings and hire targets, build lists, shark windows) are not C++: they sit in
`tests/bots/<name>.plan` next to the run file, and the bot code keeps only the loop. The host loads the plan of the run file
(`bot_host.cpp`, `LoadBotPlan`) and passes it to the bot's `Make...Bot(const BotPlan&)`; a run without a plan file (the L0 and
`wait` bots) gets an empty plan. A line is `<name>: <word> ...` (`#` comments); a word joins fields with `:` (`SOFTDRNK:2`,
`ACCOMBLK:1:save`, `1:11.0:1.0`; the key `-` is a blank entry).
An L0 bot's plan goes under `tests/bots/l0/`; plans of the imported campaign scenarios are not in the repository.
