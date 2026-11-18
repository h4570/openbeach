// The browser game's --pre-js (docs/architecture/browser-build.md). Before main it copies the game files from the
// origin's private file system (OPFS) into MEMFS /game, so every loader keeps its synchronous reads, then main starts with
// --game-dir /game. The OPFS tree: packs/base/... as the browser importer (importer/web/import_worker.js) writes it, plus
// the player's BeachLife.ini and SaveGames/. src/launcher/browser_files_check.cjs runs it under Node against a fake OPFS.
// It also keeps the log and reports a game that stopped (docs/architecture/browser-failures.md).

// The runtime reads the arguments of main when its code loads, after this file.
Module["arguments"] = ["--game-dir", "/game"].concat(Module["arguments"] || []);

// ---- Failures and the log ----
// A tab that stops leaves nothing behind on its own: the log is a MEMFS file. So the log's tail is kept here, written to
// OPFS logs/last.log as it grows (the next start moves it to logs/previous.log, which the start layer offers), and every
// way the game can stop reports once through Module.oblFailure, which shows the page's "failed" layer. Set up before the
// runtime's code because it copies Module.printErr when it loads; an abort can come before preRun (no memory for wasm).
const oblBrowserLog = (function oblSetUpBrowserLog() {
    const logPath = "/game/openbeach.log";
    const logsDirectory = "logs";
    const keptBytes = 1 << 20; // the log's tail kept in memory and in OPFS
    const keptErrorLines = 200;
    const tickMs = 2000;
    const stallMs = 15000;

    let logText = "";
    let logRead = 0; // bytes of the MEMFS log already in logText
    const errorLines = []; // the runtime's printErr and the page's errors, the last keptErrorLines
    let failure = null;
    let changes = 0; // the text grew; persistLog writes when this moved
    let persistedChanges = 0;
    let persistReady = false; // set once the start rotated last.log, which needs this tab's lock
    let persisting = Promise.resolve();

    function readLog() {
        let bytes;
        try {
            bytes = FS.readFile(logPath);
        } catch (error) {
            return ""; // no log yet, or no file system (an abort before the runtime)
        }
        if (bytes.length < logRead) logRead = 0; // the game reopened the log
        if (bytes.length === logRead) return "";
        const text = new TextDecoder().decode(bytes.subarray(logRead));
        logRead = bytes.length;
        logText += text;
        if (logText.length > keptBytes) logText = logText.slice(logText.indexOf("\n", logText.length - keptBytes) + 1);
        changes++;
        return text;
    }

    // New log text goes to the console as the game writes it.
    function copyLogToConsole() {
        const text = readLog();
        if (text) console.log(text.replace(/\n$/, ""));
    }

    function noteError(line) {
        errorLines.push(line);
        if (errorLines.length > keptErrorLines) errorLines.shift();
        changes++;
    }

    function fullText() {
        let text = logText;
        if (errorLines.length > 0) text += "==== browser: last errors\n" + errorLines.join("\n") + "\n";
        if (failure) {
            text += "==== browser: " + failure.kind + ": " + failure.message + "\n";
            if (failure.stack) text += failure.stack.replace(/\n?$/, "\n");
        }
        const agent = globalThis.navigator && navigator.userAgent;
        if (agent) text += "==== browser: " + agent + "\n";
        return text;
    }

    async function logsHandle(create) {
        const root = await navigator.storage.getDirectory();
        return root.getDirectoryHandle(logsDirectory, {create});
    }

    async function readStored(name) {
        try {
            return await (await (await (await logsHandle(false)).getFileHandle(name)).getFile()).text();
        } catch (error) {
            if (error && error.name === "NotFoundError") return null;
            throw error;
        }
    }

    async function writeStored(name, text) {
        const handle = await (await logsHandle(true)).getFileHandle(name, {create: true});
        const stream = await handle.createWritable();
        await stream.write(new TextEncoder().encode(text));
        await stream.close();
    }

    // Writes run one after the other: two writables on one file race. A failed write is retried on the next change.
    function persistLog() {
        if (!persistReady || changes === persistedChanges) return persisting;
        const at = changes;
        const text = fullText();
        persisting = persisting.then(() => writeStored("last.log", text)).then(() => {
            persistedChanges = at;
        }, (error) => {
            console.error("browser: cannot store the log: " + error.message);
        });
        return persisting;
    }

    // At the start, the last run's log becomes the previous one. OPFS has no portable rename, so it is read, written and
    // removed. Without a last.log the previous one stays: a start without play keeps the log of the run before it.
    async function rotate() {
        try {
            const last = await readStored("last.log");
            if (last !== null) {
                await writeStored("previous.log", last);
                await (await logsHandle(false)).removeEntry("last.log");
            }
        } catch (error) {
            console.error("browser: cannot keep the previous log: " + error.message);
        }
        persistReady = true;
    }

    function triggerDownload(name, bytes, type) {
        const url = URL.createObjectURL(new Blob([bytes], {type}));
        const link = document.createElement("a");
        link.href = url;
        link.download = name;
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
    }

    Module["downloadLog"] = function oblDownloadLog() {
        copyLogToConsole();
        triggerDownload("openbeach.log", new TextEncoder().encode(fullText()), "text/plain");
    };

    Module["hasPreviousLog"] = async function oblHasPreviousLog() {
        return (await readStored("previous.log").catch(() => null)) !== null;
    };

    Module["downloadPreviousLog"] = async function oblDownloadPreviousLog() {
        const text = await readStored("previous.log");
        if (text === null) throw new Error("no previous log");
        triggerDownload("openbeach-previous.log", new TextEncoder().encode(text), "text/plain");
    };

    // Once: the first report names the cause, the ones it sets off (an abort's rethrow, the exit after a FatalError) are
    // the same failure.
    Module["oblFailure"] = function oblFailure(kind, message, stack) {
        if (failure) return;
        failure = {kind: String(kind), message: String(message || ""), stack: stack ? String(stack) : ""};
        Module["oblFrameAt"] = 0; // no stall report after the failure
        const steps = [
            copyLogToConsole,
            () => console.error("browser: game failed (" + failure.kind + "): " + failure.message),
            () => Module["flushGameFiles"] && Module["flushGameFiles"](),
            () => { changes++; persistLog(); },
            () => {
                const sdl = Module["SDL3"];
                if (sdl && sdl.audioContext && sdl.audioContext.state === "running") sdl.audioContext.suspend();
            },
            () => Module["onGameFailed"] && Module["onGameFailed"]({...failure}),
        ];
        for (const step of steps) {
            try {
                step();
            } catch (error) {
                console.error("browser: while reporting the failure: " + error.message);
            }
        }
    };

    // The game's own code: its script or its wasm. An error of the page's own scripts (or another origin's "Script error.")
    // is only noted, so a page bug cannot cover a running game.
    function fromGame(error, filename) {
        return fromWasm(error) || /OpenBeach\.js/.test(String(filename || "") + " " + String((error && error.stack) || ""));
    }

    // A rejection counts only from the wasm: SDL's script glue (in OpenBeach.js) leaves promises such as
    // AudioContext.resume() uncaught, and their rejection does not stop the game.
    function fromWasm(error) {
        if (typeof WebAssembly !== "undefined" && error instanceof WebAssembly.RuntimeError) return true;
        return /wasm-function/.test(String((error && error.stack) || ""));
    }

    const printErr = Module["printErr"];
    Module["printErr"] = function oblPrintErr(text) {
        noteError(String(text));
        if (printErr) printErr(text);
        else console.error(text);
    };
    const onAbort = Module["onAbort"];
    Module["onAbort"] = function oblOnAbort(what) {
        if (onAbort) onAbort(what);
        Module["oblFailure"]("abort", what, new Error().stack);
    };
    // Called only when the runtime really ends (emscripten_force_exit); a plain exit() inside the main loop does not.
    const onExit = Module["onExit"];
    Module["onExit"] = function oblOnExit(code) {
        if (onExit) onExit(code);
        Module["oblFailure"]("exit", "the game exited with code " + code, new Error().stack);
    };
    // A trap in the wasm (or a throw in the game's script) leaves the main loop's animation frame as an uncaught error.
    // The loop stops, while SDL's audio callbacks keep the music going.
    window.addEventListener("error", (event) => {
        const error = event.error;
        noteError("page error: " + event.message + (event.filename ? " (" + event.filename + ":" + event.lineno + ")" : ""));
        if (fromGame(error, event.filename)) Module["oblFailure"]("error", event.message, error && error.stack);
    });
    window.addEventListener("unhandledrejection", (event) => {
        const reason = event.reason;
        const message = reason && reason.message ? reason.message : String(reason);
        noteError("unhandled rejection: " + message);
        if (fromWasm(reason)) Module["oblFailure"]("rejection", message, reason && reason.stack);
    });

    // The watchdog: the game's frames beat Module.oblFrameAt (performance.now(), RunBrowserFrame). A loop that stopped
    // without an error leaves the beat behind while the page lives on. A hidden tab gets no animation frames, so only a
    // visible one counts, from the moment it became visible. A tick that comes late saw the main thread busy (a frame
    // that loads a world), which is no stop, so the count starts again.
    let lastTick = 0;
    let watchFrom = 0;
    function checkFrames() {
        const now = performance.now();
        const late = lastTick > 0 && now - lastTick > 2 * tickMs;
        lastTick = now;
        if (late) watchFrom = now;
        if (late || failure || !(Module["oblFrameAt"] > 0) || document.visibilityState !== "visible") return;
        const since = now - Math.max(Module["oblFrameAt"], watchFrom);
        if (since > stallMs) Module["oblFailure"]("stalled", "no frame for " + Math.round(since / 1000) + " s while the tab was visible", "");
    }

    function tick() {
        copyLogToConsole();
        checkFrames();
        persistLog();
    }

    function becameVisible() {
        watchFrom = performance.now();
    }

    return {copyLogToConsole, persistLog, rotate, tick, becameVisible, triggerDownload, tickMs};
})();

Module["preRun"] = [].concat(Module["preRun"] || [], function oblLoadGameFiles() {
    const target = "/game";
    // OPFS entries copied into the game directory: the pack must exist, the settings and the saves may not yet.
    const entries = [
        {path: "packs/base", kind: "directory", required: true},
        {path: "BeachLife.ini", kind: "file", required: false},
        {path: "SaveGames", kind: "directory", required: false},
        {path: "progress.json", kind: "file", required: false},
    ];
    // What the game writes and the flush returns to OPFS.
    const writtenFiles = ["BeachLife.ini", "progress.json"];
    const writtenDirectory = "SaveGames";
    const flushIntervalMs = oblBrowserLog.tickMs;
    const lockName = "openbeach-game";

    async function lookup(root, path, kind) {
        const parts = path.split("/");
        let dir = root;
        try {
            for (const part of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(part);
            const name = parts[parts.length - 1];
            return kind === "file" ? await dir.getFileHandle(name) : await dir.getDirectoryHandle(name);
        } catch (error) {
            if (error && error.name === "NotFoundError") return null;
            throw error;
        }
    }

    // Reads overlap: one at a time left the copy waiting on each OPFS read. The pack is about 1 GB in 5600 files.
    const parallelReads = 16;

    // MEMFS keeps the read buffer as the file's contents (canOwn) instead of copying it.
    async function copyFile(handle, to) {
        const file = await handle.getFile();
        FS.writeFile(to, new Uint8Array(await file.arrayBuffer()), {canOwn: true});
    }

    // Creates the directories at once and returns the files to copy: [{handle, to}].
    async function listDirectory(handle, to, files) {
        FS.mkdirTree(to);
        for await (const [name, child] of handle.entries()) {
            const path = to + "/" + name;
            if (child.kind === "directory") {
                await listDirectory(child, path, files);
            } else {
                files.push({handle: child, to: path});
            }
        }
        return files;
    }

    async function copyFiles(files) {
        let next = 0;
        async function reader() {
            while (next < files.length) {
                const {handle, to} = files[next++];
                await copyFile(handle, to);
            }
        }
        const readers = [];
        for (let i = 0; i < Math.min(parallelReads, files.length); i++) readers.push(reader());
        await Promise.all(readers);
        return files.length;
    }

    async function load() {
        const root = await navigator.storage.getDirectory();
        let files = 0;
        for (const entry of entries) {
            const handle = await lookup(root, entry.path, entry.kind);
            if (!handle) {
                if (entry.required) throw new Error("no " + entry.path + " in browser storage: import the game first");
                continue;
            }
            const to = target + "/" + entry.path;
            FS.mkdirTree(to.slice(0, to.lastIndexOf("/")));
            files += await copyFiles(entry.kind === "file" ? [{handle, to}] : await listDirectory(handle, to, []));
        }
        // The game writes its saves there even when the player has none yet.
        FS.mkdirTree(target + "/SaveGames");
        return files;
    }

    // ---- Writes back ----
    // The game writes MEMFS synchronously; a snapshot of (mtime, size) per watched file tells what changed since the
    // last flush, so a save reaches OPFS within one interval without a callback in the game.
    const flushed = new Map(); // MEMFS path -> "mtime:size" as last written to OPFS
    let flushing = Promise.resolve();

    function listWritten() {
        const found = [];
        for (const name of writtenFiles) found.push(name);
        let names = [];
        try {
            names = FS.readdir(target + "/" + writtenDirectory);
        } catch (error) {
            // no SaveGames directory yet
        }
        for (const name of names) if (name !== "." && name !== "..") found.push(writtenDirectory + "/" + name);
        return found;
    }

    function stamp(relative) {
        try {
            const stat = FS.stat(target + "/" + relative);
            return stat.mtime.getTime() + ":" + stat.size;
        } catch (error) {
            return null; // not there
        }
    }

    async function opfsDirectory(root, relative, create) {
        let dir = root;
        for (const part of relative.split("/").filter(Boolean)) dir = await dir.getDirectoryHandle(part, {create});
        return dir;
    }

    async function writeOpfs(root, relative, bytes) {
        const slash = relative.lastIndexOf("/");
        const dir = await opfsDirectory(root, slash < 0 ? "" : relative.slice(0, slash), true);
        const handle = await dir.getFileHandle(relative.slice(slash + 1), {create: true});
        const stream = await handle.createWritable();
        await stream.write(bytes);
        await stream.close();
    }

    async function removeOpfs(root, relative) {
        const slash = relative.lastIndexOf("/");
        try {
            const dir = await opfsDirectory(root, slash < 0 ? "" : relative.slice(0, slash), false);
            await dir.removeEntry(relative.slice(slash + 1));
        } catch (error) {
            if (!error || error.name !== "NotFoundError") throw error;
        }
    }

    async function flushNow() {
        const root = await navigator.storage.getDirectory();
        const present = new Set();
        let count = 0;
        for (const relative of listWritten()) {
            const now = stamp(relative);
            if (now === null) continue;
            present.add(relative);
            if (flushed.get(relative) === now) continue;
            await writeOpfs(root, relative, FS.readFile(target + "/" + relative));
            flushed.set(relative, now);
            count++;
        }
        // A deleted save leaves OPFS too.
        for (const relative of [...flushed.keys()]) {
            if (present.has(relative)) continue;
            await removeOpfs(root, relative);
            flushed.delete(relative);
            count++;
        }
        return count;
    }

    // Flushes run one after the other; a failure is reported and the next flush retries the file.
    Module["flushGameFiles"] = function oblFlushGameFiles() {
        flushing = flushing.then(flushNow).then((count) => {
            if (count) console.log("browser: " + count + " game file(s) flushed to browser storage");
            return count;
        }, (error) => {
            console.error("browser: cannot flush the game files: " + error.message);
            if (Module["onFlushError"]) Module["onFlushError"](error);
            return 0;
        });
        return flushing;
    };

    // Save export/import as one JSON file: {format, files: {"SaveGames/x": base64}} with the saves, settings and progress.
    function toBase64(bytes) {
        let text = "";
        for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
        return btoa(text);
    }

    // The first name stays readable: a file exported before the rename imports like any other.
    const savesFormats = ["openbeach-saves-1", "openbeachlife-saves-1"];

    Module["exportSaves"] = function oblExportSaves() {
        const files = {};
        for (const relative of listWritten()) {
            if (stamp(relative) !== null) files[relative] = toBase64(FS.readFile(target + "/" + relative));
        }
        return JSON.stringify({format: savesFormats[0], files});
    };

    Module["downloadSaves"] = function oblDownloadSaves() {
        oblBrowserLog.triggerDownload("openbeach-saves.json", new TextEncoder().encode(Module["exportSaves"]()), "application/json");
    };

    // Writes the exported files to OPFS (the next start reads them); returns how many. Run before the game starts, or
    // reload afterwards: a running game would overwrite them with its own state.
    Module["importSaves"] = async function oblImportSaves(text) {
        const data = JSON.parse(text);
        if (!data || !savesFormats.includes(data.format) || typeof data.files !== "object") {
            throw new Error("not an OpenBeach saves file");
        }
        const root = await navigator.storage.getDirectory();
        let count = 0;
        for (const [relative, base64] of Object.entries(data.files)) {
            const allowed = writtenFiles.includes(relative) ||
                (relative.startsWith(writtenDirectory + "/") && !relative.slice(writtenDirectory.length + 1).includes("/"));
            if (!allowed || relative.includes("..")) throw new Error("unexpected file in the saves file: " + relative);
            const raw = atob(base64);
            const bytes = new Uint8Array(raw.length);
            for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
            await writeOpfs(root, relative, bytes);
            count++;
        }
        return count;
    };

    // The language codes of the pack's text tables (packs/base/text/<code>.json in OPFS): what the start layer offers.
    Module["packLanguages"] = async function oblPackLanguages() {
        const root = await navigator.storage.getDirectory();
        const dir = await lookup(root, "packs/base/text", "directory");
        const codes = [];
        if (dir) for await (const [name, child] of dir.entries()) if (child.kind === "file" && name.endsWith(".json")) codes.push(name.slice(0, -5));
        return codes.sort();
    };

    // The game's BeachLife.ini as the game copy has it (empty before the first start). The game reads it in main, which waits
    // for the start click, so a change before the click is what the game starts with; the flush returns it to OPFS.
    Module["readSettings"] = function oblReadSettings() {
        try {
            return new TextDecoder().decode(FS.readFile(target + "/BeachLife.ini"));
        } catch (error) {
            return "";
        }
    };

    Module["writeSettings"] = function oblWriteSettings(text) {
        FS.writeFile(target + "/BeachLife.ini", new TextEncoder().encode(text));
        return Module["flushGameFiles"]();
    };

    function startWriteBack() {
        // What load() copied is already in OPFS.
        for (const relative of listWritten()) {
            const now = stamp(relative);
            if (now !== null) flushed.set(relative, now);
        }
        setInterval(() => {
            oblBrowserLog.tick();
            Module["flushGameFiles"]();
        }, flushIntervalMs);
        document.addEventListener("visibilitychange", () => {
            if (document.visibilityState === "hidden") Module["flushGameFiles"]();
            else oblBrowserLog.becameVisible();
        });
        window.addEventListener("pagehide", () => {
            oblBrowserLog.copyLogToConsole();
            oblBrowserLog.persistLog();
            Module["flushGameFiles"]();
        });
    }

    // One game per origin: a second tab would overwrite the first one's saves in OPFS. The lock is held until the tab ends.
    function acquireLock() {
        if (!navigator.locks) return Promise.resolve(); // no Web Locks: the tabs share OPFS
        return new Promise((resolve, reject) => {
            navigator.locks.request(lockName, {ifAvailable: true}, (lock) => {
                if (!lock) {
                    reject(new Error("the game is already open in another tab"));
                    return undefined;
                }
                resolve();
                return new Promise(() => {});
            });
        });
    }

    // Browsers start an AudioContext only inside a user gesture: main (and SDL's Web Audio device) waits for the page's
    // click-to-start layer, which calls Module.startGame from its click handler (web/site.js).
    addRunDependency("obl-start-click");
    let started = false;
    Module["startGame"] = function oblStartGame() {
        if (started) return;
        started = true;
        removeRunDependency("obl-start-click");
    };

    addRunDependency("obl-game-files");
    const copyStarted = Date.now();
    // The log rotates only under the lock: a second tab must not move the running one's last.log.
    acquireLock().then(oblBrowserLog.rotate).then(load).then((files) => {
        startWriteBack();
        console.log("browser: " + files + " game files copied to " + target + " in " + (Date.now() - copyStarted) + " ms");
        // The page swaps its loading text for the click-to-start layer.
        if (Module["onGameFilesReady"]) Module["onGameFilesReady"](files);
        removeRunDependency("obl-game-files");
    }, (error) => {
        // main never runs: the run dependency stays.
        console.error("browser: cannot load the game files: " + error.message);
        if (Module["onGameFilesError"]) Module["onGameFilesError"](error);
    });
});
