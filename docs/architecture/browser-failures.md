# Browser failures and logs

How the [browser build](browser-build.md) reports a game that stopped and keeps its log. A tab that stops leaves nothing
behind on its own: the log is a MEMFS file and dies with the page. So the pre-js (`launcher/browser_files.js`) keeps the
log's tail itself, and every way the game stops reports once through `Module.oblFailure(kind, message, stack)`.
- **What reports.** A trap in the wasm or a throw in the game's script leaves the main loop's animation frame as an uncaught
  `error` (kind `error`): the loop stops and the picture freezes, while SDL's audio callbacks keep the music going. An
  `unhandledrejection` from the wasm (`rejection`; one from SDL's script glue, such as an `AudioContext` that does not
  resume, is only noted), `Module.onAbort` (`abort`), `Module.onExit` (`exit`) and the
  watchdog (`stalled`) report too. An error of the page's own scripts, or another origin's `Script error.`, is only noted in
  the log, so a page bug cannot cover a running game.
- **FatalError and ShowErrorAndExit.** The runtime is kept alive (no `EXIT_RUNTIME`), so an `exit()` inside the main loop
  only throws `ExitStatus`, which the loop swallows: the next frame ran the game on. In the browser `GameExit`
  (`platform/diagnostics.cpp`, not the headless Node builds) cancels the loop, calls `Module.oblFailure` with `fatal` or
  `error` and the message, and ends the runtime with `emscripten_force_exit`. The sprite and path tile loaders reach it
  through `FatalError` instead of `std::_Exit`. A FatalError during that exit (natively, a static destructor that finds a
  broken state) ends at once.
- **The watchdog.** After each frame `RunBrowserFrame` sets `Module.oblFrameAt` (`performance.now()`); the end of the game
  clears it. The 2 s tick reports `stalled` after 15 s without a frame while the tab is visible, counted from the moment it
  became visible. A tick that comes more than two intervals late saw the main thread busy (a frame that loads a world),
  so the count starts again; a main thread that never comes back cannot be seen from the page at all, only its log stays.
- **The report.** `oblFailure` appends `==== browser: <kind>: <message>` and the stack to the log text, after the last 200
  lines of `printErr` and page errors, prints the new log text, flushes the game files, stores the log, suspends SDL's
  `AudioContext` and calls the page's `Module.onGameFailed({kind, message, stack})`: the "failed" layer with the cause,
  Download log, Download saves, Reload and a link to the issues.
- **Stored logs.** The tail (about 1 MB) goes to OPFS `logs/last.log` from the tick when it grew, on `pagehide` and on a
  failure; it is not one of the game files, so it stays out of the saves export. The next start, under the tab's Web Lock,
  moves it to `logs/previous.log` (a second tab leaves it alone), and the start layer offers "Download the last log"
  when there is one (`Module.hasPreviousLog`, `Module.downloadPreviousLog`): the way to a log after a tab that froze.
  `Module.downloadLog` builds the file from the kept text, so it works after an abort and without the MEMFS file.
- **Checks.** `browser_files_check.cjs` covers each report (once), the stored and downloaded log with the stack, the page
  errors, the rotation and the watchdog (visible, hidden, late tick, ended); `site_check.cjs` the "failed" layer and the
  start layer's button. With the probe, `--prepare tools/probe_steps/failure_prepare.js` removes the sprite sheets from the
  game copy, so a running game ends in FatalError, and `--script tools/probe_steps/failure.json` checks the layer, its log
  and the previous log after a reload.
- **A closed audio device's node.** SDL's Emscripten backend closes the physical device when its last stream closes (a
  song ends, the samples stop) and opens a new one with the next stream. It disconnects the old `ScriptProcessorNode`
  and frees the device, but keeps the node's `onaudioprocess`, which only checks that some device is open. After a long
  frame Firefox still delivers the old node's queued events, and each called into the freed device: "indirect call to
  null" once the memory was reused (issue #81). The pre-js makes a full `disconnect()` of a `ScriptProcessorNode` drop
  its handler; `browser_files_check.cjs` checks it.
