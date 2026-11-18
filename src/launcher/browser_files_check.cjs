// Runs the browser game's pre-js (browser_files.js) under Node against a fake OPFS made of Maps and a fake MEMFS, as the
// emitted OpenBeach.js would before main. tools/wasm_linux.sh runs it.
//   node browser_files_check.cjs [<browser_files.js>]
// Exit code 1 on a failed check.
"use strict";

const fs = require("fs");
const path = require("path");

let failures = 0;
function check(condition, what) {
    console.log(`${condition ? "PASS" : "FAIL"} ${what}`);
    if (!condition) failures++;
}

// How many file reads were in flight at once, the most over a run.
const reads = {inFlight: 0, most: 0};

const notFound = () => Object.assign(new Error("not found"), {name: "NotFoundError"});

// A directory handle over a nested object: a Uint8Array is a file, an object a directory.
function fakeDirectory(tree) {
    return {
        kind: "directory",
        async getDirectoryHandle(name, {create = false} = {}) {
            if (!tree[name] && create) tree[name] = {};
            const child = tree[name];
            if (!child || child instanceof Uint8Array) throw notFound();
            return fakeDirectory(child);
        },
        async getFileHandle(name, {create = false} = {}) {
            if (!tree[name] && create) tree[name] = new Uint8Array(0);
            if (!(tree[name] instanceof Uint8Array)) throw notFound();
            return {kind: "file",
                async getFile() {
                    const child = tree[name];
                    return {async arrayBuffer() {
                        reads.most = Math.max(reads.most, ++reads.inFlight);
                        await new Promise((resolve) => setTimeout(resolve, 1)); // a read takes a while
                        reads.inFlight--;
                        return child.buffer.slice(child.byteOffset, child.byteOffset + child.byteLength);
                    }, async text() {
                        return Buffer.from(child).toString();
                    }};
                },
                async createWritable() {
                    return {
                        async write(bytes) {
                            tree[name] = new Uint8Array(bytes);
                        },
                        async close() {},
                    };
                }};
        },
        async removeEntry(name) {
            if (!(name in tree)) throw notFound();
            delete tree[name];
        },
        async* entries() {
            for (const name of Object.keys(tree)) {
                yield [name, tree[name] instanceof Uint8Array ? await this.getFileHandle(name) : await this.getDirectoryHandle(name)];
            }
        },
    };
}

// Lets the timers and the fake OPFS's reads finish.
const settle = async () => {
    for (let i = 0; i < 6; i++) await new Promise((resolve) => setTimeout(resolve, 2));
};

// The pre-js with the runtime pieces it uses; resolves with the file system and the arguments once main would run. The
// page's click (Module.startGame) comes after the copy unless clickFirst, and never when click is false. `clock.now` is
// performance.now(); downloads and the page's onGameFailed calls are collected.
function runPreJs(source, opfs, {click = true, clickFirst = false, lock = "none"} = {}) {
    const files = new Map();
    const dirs = new Set();
    const logged = [];
    const mtimes = new Map();
    const owned = new Set(); // files written with canOwn: MEMFS keeps the buffer, no copy
    let clock = 1000;
    const FS = {
        mkdirTree(dir) {
            dirs.add(dir);
        },
        writeFile(file, bytes, {canOwn = false} = {}) {
            if (canOwn) owned.add(file);
            files.set(file, Buffer.from(bytes));
            mtimes.set(file, clock += 1000);
        },
        readFile(file) {
            if (!files.has(file)) throw new Error("ENOENT");
            return new Uint8Array(files.get(file));
        },
        stat(file) {
            if (!files.has(file)) throw new Error("ENOENT");
            return {mtime: new Date(mtimes.get(file)), size: files.get(file).length};
        },
        readdir(dir) {
            const names = [".", ".."];
            for (const file of files.keys()) if (file.startsWith(dir + "/")) names.push(file.slice(dir.length + 1));
            return names;
        },
        unlink(file) {
            files.delete(file);
        },
    };
    const ticks = [];
    const listeners = {};
    const timeline = {now: 100000};
    const downloads = [];
    const failed = [];
    const blobs = new Map();
    const fakeUrl = {
        createObjectURL(blob) {
            blobs.set("blob:" + blobs.size, blob);
            return "blob:" + (blobs.size - 1);
        },
        revokeObjectURL() {},
    };
    const events = {
        document: {visibilityState: "visible", addEventListener(type, fn) { listeners[type] = fn; },
            createElement: () => ({click() {
                downloads.push({name: this.download, blob: blobs.get(this.href)});
            }})},
        window: {addEventListener(type, fn) { listeners[type] = fn; }},
        setInterval(fn) { ticks.push(fn); },
        navigatorLocks: lock === "none" ? undefined : {
            request(name, options, callback) {
                return Promise.resolve(callback(lock === "free" ? {name} : null));
            },
        },
    };
    const Module = {arguments: ["--hidden"], onGameFailed: (info) => failed.push(info)};
    return new Promise((resolve) => {
        const dependencies = new Set();
        let ready = false;
        const done = (error) => resolve({files, dirs, owned, Module, error, ready, waiting: [...dependencies], logged, FS, ticks, listeners, events,
            clock: timeline, downloads, failed});
        const addRunDependency = (id) => dependencies.add(id);
        const removeRunDependency = (id) => {
            dependencies.delete(id);
            if (dependencies.size === 0) done(null);
        };
        Module.onGameFilesError = done;
        Module.onGameFilesReady = () => {
            ready = true;
            if (click) Module.startGame();
            else setTimeout(() => done(null), 0);
        };
        const navigator = {locks: events.navigatorLocks, storage: {async getDirectory() {
            return fakeDirectory(opfs);
        }}};
        const console = {log(...args) { logged.push(args.join(" ")); }, error() {}};
        const performance = {now: () => timeline.now};
        new Function("Module", "FS", "addRunDependency", "removeRunDependency", "navigator", "console", "document", "window",
            "setInterval", "performance", "URL", source)(Module, FS, addRunDependency, removeRunDependency, navigator, console,
            events.document, events.window, events.setInterval, performance, fakeUrl);
        for (const step of Module.preRun) step();
        if (clickFirst) {
            Module.startGame();
            Module.startGame(); // a second click does nothing
        }
        if (dependencies.size === 0) done(null);
    });
}

async function main() {
    const source = fs.readFileSync(process.argv[2] || path.join(__dirname, "browser_files.js"), "utf8");
    const bytes = (text) => new Uint8Array(Buffer.from(text));

    const full = await runPreJs(source, {
        packs: {
            base: {"pack.json": bytes("{}"), gfx: {"sea.png": bytes("png"), deep: {"a.bin": new Uint8Array([0, 1, 255])}}},
            other: {"skip.json": bytes("x")},
        },
        "BeachLife.ini": bytes("[Beach Life Options]\n"),
        SaveGames: {"slot1.obl.save": bytes("save")},
        "import.log": bytes("not copied"),
    });
    check(!full.error, "full tree: main runs");
    check(JSON.stringify(full.Module.arguments) === JSON.stringify(["--game-dir", "/game", "--hidden"]),
        "full tree: main gets --game-dir /game before the page's arguments");
    const expected = {
        "/game/packs/base/pack.json": "{}",
        "/game/packs/base/gfx/sea.png": "png",
        "/game/packs/base/gfx/deep/a.bin": "\x00\x01\xff",
        "/game/BeachLife.ini": "[Beach Life Options]\n",
        "/game/SaveGames/slot1.obl.save": "save",
    };
    check(full.files.size === Object.keys(expected).length, `full tree: ${full.files.size} files copied, other packs and stray files not`);
    for (const [file, text] of Object.entries(expected)) {
        check(full.files.has(file) && full.files.get(file).equals(Buffer.from(text, "latin1")), `full tree: ${file} byte for byte`);
    }
    check(full.dirs.has("/game/packs/base/gfx/deep"), "full tree: nested directories created");
    check([...full.files.keys()].every((file) => full.owned.has(file)), "full tree: MEMFS owns the read buffers, no copy");

    // The pack's 5600 files: reads overlap instead of waiting for each other.
    const many = {};
    for (let i = 0; i < 100; i++) many["f" + i + ".bin"] = bytes(String(i));
    reads.most = 0;
    const wide = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}"), sprites: many}}});
    check(!wide.error && wide.files.size === 101 && wide.files.get("/game/packs/base/sprites/f42.bin").toString() === "42",
        "many files: every file copied");
    check(reads.most > 1 && reads.most <= 16, `many files: ${reads.most} reads at once, at most 16`);

    const fresh = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}});
    check(!fresh.error && fresh.files.size === 1, "pack only: main runs without settings and saves");
    check(fresh.dirs.has("/game/SaveGames"), "pack only: an empty SaveGames directory");

    const empty = await runPreJs(source, {"BeachLife.ini": bytes("")});
    check(empty.error && /packs\/base/.test(empty.error.message), "no pack: main does not run, the error names packs/base");

    const noClick = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}}, {click: false});
    check(noClick.ready && JSON.stringify(noClick.waiting) === JSON.stringify(["obl-start-click"]),
        "no click: files copied, main waits for the click-to-start layer");
    const early = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}}, {clickFirst: true});
    check(!early.error && early.ready && early.files.size === 1, "click before the copy ends: main runs after the copy");

    // Writes back: the game changes MEMFS, the tick (or pagehide) flushes it to OPFS.
    const opfs = {
        packs: {base: {"pack.json": bytes("{}")}},
        SaveGames: {"old.obl.save": bytes("old")},
        "progress.json": bytes("{}"),
    };
    const run = await runPreJs(source, opfs, {lock: "free"});
    check(!run.error && run.files.has("/game/progress.json"), "write-back: progress.json is copied too");
    await run.Module.flushGameFiles();
    check(opfs.SaveGames["old.obl.save"].length === 3, "write-back: unchanged files are not rewritten");
    run.FS.writeFile("/game/SaveGames/new.obl.save", bytes("new"));
    run.FS.writeFile("/game/BeachLife.ini", bytes("[ini]"));
    run.FS.writeFile("/game/openbeach.log", bytes("line one\n"));
    run.FS.writeFile("/game/packs/base/ignored.bin", bytes("x"));
    for (const tick of run.ticks) tick();
    await run.Module.flushGameFiles();
    check(Buffer.from(opfs.SaveGames["new.obl.save"]).toString() === "new", "write-back: a new save reaches OPFS");
    check(Buffer.from(opfs["BeachLife.ini"]).toString() === "[ini]", "write-back: the INI reaches OPFS");
    check(!("openbeach.log" in opfs) && !("ignored.bin" in opfs.packs.base), "write-back: log and pack stay out of the game files");
    check(run.logged.includes("line one"), "log: new log text goes to the console");
    await settle();
    check(opfs.logs && Buffer.from(opfs.logs["last.log"]).toString().startsWith("line one\n"), "log: its tail lands in OPFS logs/last.log");
    run.FS.writeFile("/game/openbeach.log", bytes("line one\nline two\n"));
    run.listeners.pagehide();
    await run.Module.flushGameFiles();
    check(run.logged.includes("line two") && !run.logged.slice(run.logged.indexOf("line two") + 1).includes("line one"),
        "log: only the appended text is printed again");
    run.FS.unlink("/game/SaveGames/old.obl.save");
    run.events.document.visibilityState = "hidden";
    run.listeners.visibilitychange();
    await run.Module.flushGameFiles();
    check(!("old.obl.save" in opfs.SaveGames), "write-back: a deleted save leaves OPFS on visibilitychange");

    await checkFailures(source, bytes);

    // The start layer's language: the pack's tables and the settings file, changed before the game starts.
    const langOpfs = {packs: {base: {"pack.json": bytes("{}"), text: {"pl.json": bytes("{}"), "en.json": bytes("{}"), "notes.txt": bytes("")}}},
        "BeachLife.ini": bytes("[Language]\nLanguage=1\n")};
    const lang = await runPreJs(source, langOpfs, {lock: "free"});
    check((await lang.Module.packLanguages()).join() === "en,pl", "languages: the pack's text tables are listed");
    check(lang.Module.readSettings() === "[Language]\nLanguage=1\n", "languages: the settings are read from the game copy");
    await lang.Module.writeSettings("[Language]\r\nLanguage=5\r\n");
    check(Buffer.from(langOpfs["BeachLife.ini"]).toString() === "[Language]\r\nLanguage=5\r\n",
        "languages: a change reaches the game copy and OPFS");
    check((await (await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}}, {lock: "free"})).Module.packLanguages()).length === 0,
        "languages: a pack without texts lists none");

    // Export and import as a file.
    const exported = JSON.parse(run.Module.exportSaves());
    check(exported.format === "openbeach-saves-1" && Object.keys(exported.files).sort().join() ===
        "BeachLife.ini,SaveGames/new.obl.save,progress.json", "export: saves, INI and progress in one file");
    const target = {packs: {base: {"pack.json": bytes("{}")}}};
    const importer = await runPreJs(source, target, {lock: "free"});
    check(await importer.Module.importSaves(JSON.stringify(exported)) === 3 &&
        Buffer.from(target.SaveGames["new.obl.save"]).toString() === "new", "import: the files land in OPFS");
    const older = {packs: {base: {"pack.json": bytes("{}")}}};
    const olderImporter = await runPreJs(source, older, {lock: "free"});
    check(await olderImporter.Module.importSaves(JSON.stringify({...exported, format: "openbeachlife-saves-1"})) === 3 &&
        Buffer.from(older.SaveGames["new.obl.save"]).toString() === "new", "import: a file with the first format name lands in OPFS");
    let refused = false;
    await importer.Module.importSaves(JSON.stringify({format: "openbeach-saves-1", files: {"packs/base/pack.json": ""}}))
        .catch(() => { refused = true; });
    check(refused, "import: a path outside the saves is refused");
    refused = false;
    await importer.Module.importSaves("{\"format\":\"x\"}").catch(() => { refused = true; });
    check(refused, "import: a foreign file is refused");

    // One game per origin.
    const second = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}}, {lock: "held"});
    check(second.error && /another tab/.test(second.error.message), "lock: a second tab does not start");
    const nolocks = await runPreJs(source, {packs: {base: {"pack.json": bytes("{}")}}});
    check(!nolocks.error, "lock: without Web Locks the game starts");

    console.log(failures ? `${failures} check(s) failed` : "all browser file checks passed");
    process.exit(failures ? 1 : 0);
}

// Every way the game stops reports once, with the log and the cause in OPFS and in the download; the watchdog sees a loop
// that stopped while the tab is visible.
async function checkFailures(source, bytes) {
    const pack = () => ({packs: {base: {"pack.json": bytes("{}")}}});
    const text = async (download) => (download ? await download.blob.text() : "");
    const stored = (opfs, name) => (opfs.logs && opfs.logs[name] ? Buffer.from(opfs.logs[name]).toString() : null);
    const wasmError = () => Object.assign(new Error("unreachable"), {name: "RuntimeError",
        stack: "RuntimeError: unreachable\n    at wasm-function[812]:0x1234\n    at https://openbeach.app/OpenBeach.js:1:99"});

    // An uncaught error out of the game's code: the main loop's frame trapped.
    const opfs = pack();
    const run = await runPreJs(source, opfs, {lock: "free"});
    let suspended = false;
    run.Module.SDL3 = {audioContext: {state: "running", suspend() {
        suspended = true;
    }}};
    run.FS.writeFile("/game/openbeach.log", bytes("boot line\n"));
    run.Module.printErr("stderr line");
    const event = {message: "Uncaught RuntimeError: unreachable", filename: "https://openbeach.app/OpenBeach.js", lineno: 1, error: wasmError()};
    run.listeners.error(event);
    run.listeners.error(event);
    await settle();
    check(run.failed.length === 1 && run.failed[0].kind === "error" && /unreachable/.test(run.failed[0].message),
        "failure: an error of the game reaches onGameFailed once");
    check(suspended, "failure: the music stops");
    const last = stored(opfs, "last.log") || "";
    check(last.startsWith("boot line\n") && last.includes("==== browser: error: Uncaught RuntimeError: unreachable") &&
        last.includes("wasm-function[812]") && last.includes("stderr line"), "failure: the log, the cause and the stack land in OPFS logs/last.log");
    run.Module.downloadLog();
    const log = await text(run.downloads[0]);
    check(run.downloads[0] && run.downloads[0].name === "openbeach.log" && log.startsWith("boot line\n") && log.includes("wasm-function[812]") &&
        log.includes("stderr line"), "failure: the downloaded log holds the game's log, the errors and the stack");

    // The page's own errors are noted, not failures: a page bug must not cover a running game.
    const page = await runPreJs(source, pack(), {lock: "free"});
    page.listeners.error({message: "Uncaught TypeError: x is null", filename: "https://openbeach.app/site.js", lineno: 7, error: new TypeError("x is null")});
    page.listeners.error({message: "Script error.", filename: "", lineno: 0, error: null});
    page.Module.downloadLog();
    check(page.failed.length === 0 && (await text(page.downloads[0])).includes("page error: Uncaught TypeError: x is null"),
        "failure: an error of the page is only noted in the log");

    // A rejection from the wasm; one from SDL's script glue (an AudioContext that does not resume) does not stop the game.
    const rejected = await runPreJs(source, pack(), {lock: "free"});
    rejected.listeners.unhandledrejection({reason: Object.assign(new Error("The AudioContext was not allowed to start"),
        {name: "NotAllowedError", stack: "NotAllowedError: The AudioContext was not allowed to start\n    at resume (https://openbeach.app/OpenBeach.js:1:5)"})});
    check(rejected.failed.length === 0, "failure: a rejection of the script glue is only noted");
    const reason = Object.assign(new Error("bad table"), {stack: "Error: bad table\n    at wasm-function[77]:0x99\n    at https://openbeach.app/OpenBeach.js:1:2"});
    rejected.listeners.unhandledrejection({reason});
    rejected.listeners.unhandledrejection({reason});
    check(rejected.failed.length === 1 && rejected.failed[0].kind === "rejection" && rejected.failed[0].message === "bad table",
        "failure: a rejection of the wasm reaches onGameFailed once");

    // An abort calls onAbort, then rethrows out of the frame, and a FatalError ends the runtime: one report, the first.
    const aborted = await runPreJs(source, pack(), {lock: "free"});
    aborted.Module.onAbort("RuntimeError: memory access out of bounds");
    aborted.listeners.error({message: "Uncaught RuntimeError: Aborted", filename: "https://openbeach.app/OpenBeach.js", lineno: 1, error: wasmError()});
    aborted.Module.onExit(1);
    check(aborted.failed.length === 1 && aborted.failed[0].kind === "abort" && aborted.failed[0].stack.length > 0,
        "failure: an abort reaches onGameFailed once, with a stack");
    const fatal = await runPreJs(source, pack(), {lock: "free"});
    fatal.Module.oblFailure("fatal", "sprite bank: no sprites");
    fatal.Module.onExit(1);
    check(fatal.failed.length === 1 && fatal.failed[0].kind === "fatal", "failure: a FatalError's exit reports nothing more");

    // The watchdog. Frames beat Module.oblFrameAt; every tick is 2 s.
    const tick = (target, beat) => {
        target.clock.now += 2000;
        if (beat) target.Module.oblFrameAt = target.clock.now;
        for (const fn of target.ticks) fn();
    };
    const idle = await runPreJs(source, pack(), {lock: "free"});
    for (let i = 0; i < 20; i++) tick(idle, false);
    check(idle.failed.length === 0, "watchdog: no report before the first frame (the start layer)");
    for (let i = 0; i < 20; i++) tick(idle, true);
    check(idle.failed.length === 0, "watchdog: no report while frames come");
    for (let i = 0; i < 7; i++) tick(idle, false);
    check(idle.failed.length === 0, "watchdog: 14 s without a frame are not yet a stop");
    tick(idle, false);
    check(idle.failed.length === 1 && idle.failed[0].kind === "stalled", "watchdog: 16 s without a frame in a visible tab are a stop");

    const hidden = await runPreJs(source, pack(), {lock: "free"});
    tick(hidden, true);
    hidden.events.document.visibilityState = "hidden";
    hidden.listeners.visibilitychange();
    for (let i = 0; i < 30; i++) tick(hidden, false);
    check(hidden.failed.length === 0, "watchdog: a hidden tab gets no frames and no report");
    hidden.events.document.visibilityState = "visible";
    hidden.listeners.visibilitychange();
    for (let i = 0; i < 7; i++) tick(hidden, false);
    check(hidden.failed.length === 0, "watchdog: a tab that became visible gets its time for the frames to come back");
    tick(hidden, false);
    check(hidden.failed.length === 1 && hidden.failed[0].kind === "stalled", "watchdog: then a missing frame is a stop");

    const busy = await runPreJs(source, pack(), {lock: "free"});
    tick(busy, true);
    busy.clock.now += 30000; // one frame held the main thread (a world loads): this tick comes late
    tick(busy, false);
    for (let i = 0; i < 7; i++) tick(busy, false);
    check(busy.failed.length === 0, "watchdog: a late tick is a busy main thread, not a stop");
    const ended = await runPreJs(source, pack(), {lock: "free"});
    tick(ended, true);
    ended.Module.oblFrameAt = 0; // the game ended (RunBrowserFrame)
    for (let i = 0; i < 20; i++) tick(ended, false);
    check(ended.failed.length === 0, "watchdog: a game that ended is no stop");

    // The next start keeps the last run's log as the previous one, under the lock only.
    const again = {...pack(), logs: {"last.log": bytes("the frozen run\n")}};
    const next = await runPreJs(source, again, {lock: "free"});
    check(stored(again, "previous.log") === "the frozen run\n" && stored(again, "last.log") === null,
        "previous log: the start moves last.log to previous.log");
    check(await next.Module.hasPreviousLog(), "previous log: offered");
    await next.Module.downloadPreviousLog();
    check(next.downloads[0] && next.downloads[0].name === "openbeach-previous.log" && (await text(next.downloads[0])) === "the frozen run\n",
        "previous log: the download holds it");
    const quiet = {...pack(), logs: {"previous.log": bytes("older run\n")}};
    await runPreJs(source, quiet, {lock: "free"});
    check(stored(quiet, "previous.log") === "older run\n", "previous log: a start without a last run keeps the previous one");
    check(!(await (await runPreJs(source, pack(), {lock: "free"})).Module.hasPreviousLog()), "previous log: none at the first start");
    const busyTab = {...pack(), logs: {"last.log": bytes("the other tab's run\n")}};
    await runPreJs(source, busyTab, {lock: "held"});
    check(stored(busyTab, "last.log") === "the other tab's run\n" && stored(busyTab, "previous.log") === null,
        "previous log: a second tab leaves the running tab's log alone");
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
