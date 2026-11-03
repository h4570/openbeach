// Runs the browser import's WebAssembly module (obl-import-web.js) under Node with the bridge import_worker.js gives it
// in the browser, backed by the local file system and a Map instead of File objects and OPFS. tools/wasm_linux.sh runs it.
//   node node_check.cjs <obl-import-web.js>
//       the synthetic checks: request errors and the bridge calls (read, progress), no game data
//   node node_check.cjs <obl-import-web.js> --game <installation> | --iso <image> [--compare <pack directory>] [--language <lang>] [--out <dir>]
//       a full import of the installation, a CD root (its folder of files) or a disc image; the long steps (maps, sprites, pack) must
//       report their counts up to the total. With --compare the files must equal the pack directory's byte for byte;
//       --out writes the imported pack to <dir> (e.g. for tools/browser_probe.cjs --pack)
//       --workers <n> also imports with n helper workers (worker_threads, one wasm instance each, the way import_worker.js runs
//       Web Workers: shard.js) and requires the very same files as the import by one worker; both timings are printed
// The synthetic checks include shard.js's logic and a sharded import of the fake copy, which must end as a single worker's does.
// Exit code 1 on a failed check.
"use strict";

const fs = require("fs");
const path = require("path");
const {Worker, isMainThread, parentPort, workerData} = require("worker_threads");
const shard = require("./shard.js");

// Unit bytes stay Uint8Arrays here (the browser keeps Blobs); a Uint8Array also goes to another thread by value.
const bytesCodec = {keep: (bytes) => bytes, read: (bytes) => bytes};

let failures = 0;
function check(condition, what) {
    console.log(`${condition ? "PASS" : "FAIL"} ${what}`);
    if (!condition) failures++;
}

// Every file under `root` as the browser lists a picked folder: "<folder name>/<relative path>".
function pickFolder(root) {
    const files = [];
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) walk(full);
            else if (entry.isFile()) files.push({full, path: path.join(path.basename(root), path.relative(root, full)).split(path.sep).join("/"), size: fs.statSync(full).size});
        }
    };
    walk(root);
    return files;
}

// The spool keeps its files in a temporary directory here instead of Blobs.
// `options.spoolDir`: the spool another worker made, shared (a worker of the browser is handed the same Blobs); `options.onCount`
// sees every count (a helper posts it to the coordinator); `options.mute` is the replay of steps the helpers already showed.
function makeBridge(files, store, steps, counts = new Map(), options = {}) {
    const spoolDir = options.spoolDir || fs.mkdtempSync(path.join(require("os").tmpdir(), "obl-import-spool-"));
    if (!options.spoolDir) process.on("exit", () => fs.rmSync(spoolDir, {recursive: true, force: true}));
    let mute = false;
    const spoolFile = (name) => path.join(spoolDir, name);
    return {
        read(index, offset, target) {
            const file = files[index];
            if (!file || offset + target.length > file.size) return false;
            const fd = fs.openSync(file.full, "r");
            try {
                return fs.readSync(fd, target, 0, target.length, offset) === target.length;
            } finally {
                fs.closeSync(fd);
            }
        },
        put(key, bytes) {
            store.set(key, bytes);
            return true;
        },
        get(key) {
            return store.get(key) || null;
        },
        delete(key) {
            store.delete(key);
            return true;
        },
        keys(prefix) {
            return [...store.keys()].filter((key) => key.startsWith(prefix));
        },
        spoolCreate(name) {
            fs.writeFileSync(spoolFile(name), new Uint8Array(0));
            return true;
        },
        spoolAppend(name, bytes) {
            if (!fs.existsSync(spoolFile(name))) return false;
            fs.appendFileSync(spoolFile(name), bytes);
            return true;
        },
        spoolRead(name, offset, target) {
            if (!fs.existsSync(spoolFile(name))) return false;
            const fd = fs.openSync(spoolFile(name), "r");
            try {
                return fs.readSync(fd, target, 0, target.length, offset) === target.length;
            } finally {
                fs.closeSync(fd);
            }
        },
        spoolRemove(name) {
            fs.rmSync(spoolFile(name), {force: true});
        },
        spoolHas: (name) => fs.existsSync(spoolFile(name)),
        spoolDir,
        muteReplay(on) {
            mute = on;
        },
        progress(step) {
            if (!(mute && shard.kQuietSteps.has(step))) steps.push(step);
        },
        count(step, done, total) {
            if (options.onCount) options.onCount(step, done, total);
            if (mute && shard.kQuietCounts.has(step)) return;
            const seen = counts.get(step) || {reports: 0, done: 0, total: 0};
            counts.set(step, {reports: seen.reports + 1, done, total});
        },
    };
}

let wasmPath = null; // obl-import-web.js, for the helper threads to load

// A helper as shard.js expects it, running this file on a worker thread.
function spawnHelper() {
    const worker = new Worker(__filename, {workerData: {wasm: wasmPath}});
    const helper = {postMessage: (message) => worker.postMessage(message), terminate: () => worker.terminate(), onmessage: null, onerror: null};
    worker.on("message", (data) => helper.onmessage && helper.onmessage({data}));
    worker.on("error", (error) => helper.onerror && helper.onerror(error));
    worker.on("exit", () => helper.onerror && helper.onerror(new Error("the helper ended")));
    return helper;
}

// The import as import_worker.js runs it: with `helpers` > 1 the sprites and maps are first converted on helper threads, each its
// own wasm instance, and the instance that runs the import takes their units over. `sharded` tells whether that happened (a
// failed helper sends it back to importing alone). `shardProgress` collects what the helpers reported, in total.
async function run(createOblImport, files, plan, store, steps, counts, helpers = 1) {
    const module = await createOblImport();
    const units = shard.makeUnitStore(bytesCodec);
    const bridge = makeBridge(files, store, steps, counts);
    module.oblImport = Object.assign(bridge, units.bridge);
    // The plan as given, so a request that does not parse reaches the module's own message.
    const call = (extra) => module.ccall("obl_import", "number", ["string"],
        [Object.keys(extra).length ? JSON.stringify({...JSON.parse(plan), ...extra}) : plan]);
    const shardProgress = [];
    let sharded = false;
    if (helpers > 1 && call({stage: "prepare"}) === 0) {
        const result = await shard.runHelpers({
            count: helpers, spawn: spawnHelper, store: units, report: (message) => shardProgress.push(message),
            jobFor: (index) => ({type: "shard", plan, shard: {index, count: helpers}, files: [...files], image: files[-1], spoolDir: bridge.spoolDir}),
        });
        sharded = result.ok;
        if (!sharded) units.clear();
    }
    if (sharded) bridge.muteReplay(true);
    const code = call(sharded ? {stage: "units"} : {});
    return {code, sharded, shardProgress, error: module.UTF8ToString(module._obl_import_error()), language: module.UTF8ToString(module._obl_import_language())};
}

// What a helper thread does: load the module and convert the share of each job it is handed.
function helperMain() {
    const createOblImport = require(workerData.wasm);
    parentPort.on("message", (job) => {
        const files = Object.assign([...job.files], {[-1]: job.image});
        shard.runShardJob(job, {
            codec: bytesCodec,
            createModule: () => createOblImport(),
            makeBridge: (shardJob, store, post) => makeBridge(files, new Map(), [], new Map(), {
                spoolDir: shardJob.spoolDir, onCount: (step, done, total) => post({type: "progress", step, done, total})}),
        }, (message) => parentPort.postMessage(message));
    });
}

// shard.js without the module: how many helpers, the unit store and the helpers' protocol with fake helpers.
async function shardLogic() {
    const choose = (hardwareConcurrency, deviceMemory) => shard.chooseHelperCount({hardwareConcurrency, deviceMemory});
    check(choose(undefined, undefined) === 1 && choose(1, undefined) === 1 && choose(2, undefined) === 1,
        "helpers: a machine with two cores or fewer imports alone");
    check(choose(4, 16) === 3 && choose(8, 16) === 7 && choose(64, 16) === shard.kMaxHelpers, "helpers: one per core but the page's, up to a cap");
    check(choose(16, 2) === 1 && choose(16, 4) === 2 && choose(16, 8) === 4, "helpers: fewer on a device with little memory");
    check(choose(8, undefined) === 3 && choose(64, 0) === 3 && choose(8, NaN) === 3 && choose(4, undefined) === 3 && choose(2, undefined) === 1,
        "helpers: at most 3 where the browser does not report its memory (Safari, Firefox)");

    const filled = shard.makeUnitStore(bytesCodec);
    filled.bridge.unitPut("sprites", 7, "a.png", Uint8Array.of(1, 2));
    filled.bridge.unitPut("sprites", 7, "a.json", Uint8Array.of(3));
    filled.bridge.unitEnd("sprites", 7, "");
    filled.bridge.unitEnd("maps", 2, "truncated BIZ tables");
    const received = shard.makeUnitStore(bytesCodec);
    received.addRecords(filled.exportRecords());
    check(received.bridge.unitOpen("sprites", 7) === 2 && received.bridge.unitPath(0) === "a.png" &&
            received.bridge.unitBytes(1)[0] === 3 && received.bridge.unitError() === "", "units: the files of a unit come back in order");
    check(received.bridge.unitOpen("sprites", 7) === -1, "units: a unit is taken once");
    check(received.bridge.unitOpen("maps", 2) === 0 && received.bridge.unitError() === "truncated BIZ tables", "units: a unit with no file keeps its error");
    check(received.bridge.unitOpen("maps", 3) === -1, "units: a unit nobody made is missing");

    // Fake helpers: each answers its job with a count and its units, or fails.
    const fakeHelpers = (behaviour) => {
        const ended = [];
        const spawn = () => {
            const index = ended.length;
            const helper = {
                terminated: false,
                postMessage(job) {
                    setTimeout(() => {
                        const act = behaviour(job.shard.index);
                        if (act === "die") return helper.onerror(new Error("crashed"));
                        helper.onmessage({data: {type: "progress", step: "maps", done: 1, total: 2}});
                        if (act === "fail") return helper.onmessage({data: {type: "shard-error", message: "no archive"}});
                        helper.onmessage({data: {type: "progress", step: "maps", done: 2, total: 2}});
                        helper.onmessage({data: {type: "units", records: [{step: "sprites", unit: job.shard.index, error: "", files: [["f.png", Uint8Array.of(job.shard.index)]]}]}});
                    }, 1);
                },
                terminate() {
                    helper.terminated = true;
                },
            };
            ended.push(helper);
            return helper;
        };
        return {spawn, ended};
    };
    const start = (behaviour, extra = {}) => {
        const fake = fakeHelpers(behaviour);
        const store = shard.makeUnitStore(bytesCodec);
        const reports = [];
        const done = shard.runHelpers({count: 3, spawn: fake.spawn, jobFor: (index) => ({shard: {index, count: 3}}), store, report: (m) => reports.push(m), ...extra});
        return {done, fake, store, reports};
    };
    let run1 = start(() => "ok");
    let result = await run1.done;
    check(result.ok && run1.store.records.size === 3 && run1.fake.ended.every((h) => h.terminated), "helpers: every helper's units are gathered and the helpers end");
    const last = run1.reports[run1.reports.length - 1];
    check(last.step === "maps" && last.done === 6 && last.total === 6, `helpers: their counts are added into one progress (${last.done} of ${last.total})`);
    run1 = start((index) => (index === 1 ? "fail" : "ok"));
    result = await run1.done;
    check(!result.ok && result.reason === "no archive" && run1.fake.ended.every((h) => h.terminated), "helpers: one that fails ends them all and the import goes on alone");
    run1 = start((index) => (index === 2 ? "die" : "ok"));
    result = await run1.done;
    check(!result.ok && run1.fake.ended.every((h) => h.terminated), "helpers: one that dies ends them all");
    result = await shard.runHelpers({count: 2, spawn: () => { throw new Error("no nested workers"); }, jobFor: () => ({}), store: shard.makeUnitStore(bytesCodec), report: () => {}});
    check(!result.ok, "helpers: a browser that cannot start a worker imports alone");
}

async function synthetic(createOblImport) {
    await shardLogic();
    const dir = fs.mkdtempSync(path.join(require("os").tmpdir(), "obl-import-web-"));
    try {
        fs.mkdirSync(path.join(dir, "Beach Life"));
        fs.writeFileSync(path.join(dir, "Beach Life", "BeachLife.exe"), "not an executable");
        fs.mkdirSync(path.join(dir, "Beach Life", "Data"));
        fs.writeFileSync(path.join(dir, "Beach Life", "Data", "text_uk.ble"), "English only");
        const files = pickFolder(path.join(dir, "Beach Life"));
        const request = (extra) => JSON.stringify({source: "files", files: files.map(({path, size}) => ({path, size})), ...extra});

        let result = await run(createOblImport, files, "{", new Map(), []);
        check(result.code === 1 && result.error.startsWith("bad import request"), `malformed request: ${result.error}`);
        result = await run(createOblImport, [], JSON.stringify({source: "files", files: [{path: "x/readme.txt", size: 1}]}), new Map(), []);
        check(result.code === 1 && result.error.includes("no Beach Life installation"), `no game folder: ${result.error}`);
        result = await run(createOblImport, files, request({}), new Map(), []);
        check(result.code === 1 && result.error.includes("Language"), `no language: ${result.error}`);
        // A language the copy has no table for is refused before any step runs, with a message the page shows.
        const refusedSteps = [];
        result = await run(createOblImport, files, request({language: "fr"}), new Map(), refusedSteps);
        check(result.code === 1 && result.error.includes("no texts in the language \"fr\"") && refusedSteps.length === 0,
            `language the copy lacks: ${result.error}`);
        const steps = [];
        result = await run(createOblImport, files, request({language: "en"}), new Map(), steps);
        // The EXE was found and read through the bridge, then refused by the PE reader.
        check(result.code === 1 && result.error.startsWith("gui-tables: ") && result.error.includes("BeachLife.exe") &&
                !result.error.includes("not found"), `unreadable EXE: ${result.error}`);
        check(steps.join() === "gui-tables", `progress: ${steps.join()}`);
        await languageRule(createOblImport, dir);
        await shardedFakeCopy(createOblImport, dir);
        result = await run(createOblImport, files, JSON.stringify({source: "iso", isoSize: 4096}), new Map(), []);
        check(result.code === 1 && result.error.includes("ISO 9660"), `not a disc image: ${result.error}`);
    } finally {
        fs.rmSync(dir, {recursive: true, force: true});
    }
}

// The fake copy, with an archive to prepare, imported with three helper threads: each helper gets the job and its wasm instance
// refuses the copy, so the import falls back to one worker and ends exactly as it does without helpers.
async function shardedFakeCopy(createOblImport, dir) {
    const game = path.join(dir, "Sharded");
    fs.mkdirSync(path.join(game, "Data", "Sprites"), {recursive: true});
    fs.writeFileSync(path.join(game, "BeachLife.exe"), "not an executable");
    fs.writeFileSync(path.join(game, "Data", "text_uk.ble"), "English only");
    fs.writeFileSync(path.join(game, "Data", "Sprites", "BLSprite.dat"), "an archive");
    const files = pickFolder(game);
    const plan = JSON.stringify({source: "files", language: "en", files: files.map(({path, size}) => ({path, size}))});
    const aloneSteps = [], togetherSteps = [];
    const alone = await run(createOblImport, files, plan, new Map(), aloneSteps);
    const together = await run(createOblImport, files, plan, new Map(), togetherSteps, new Map(), 3);
    check(alone.code === 1 && together.code === alone.code && together.error === alone.error && togetherSteps.join() === aloneSteps.join(),
        `sharded import of a copy the helpers refuse ends as one worker does: ${together.error}`);
    check(!together.sharded, "sharded import: a failed helper sends the import back to one worker");
    // A request that is not a shard of the plan is refused with a message, not a crash.
    const refused = await run(createOblImport, files, JSON.stringify({...JSON.parse(plan), stage: "shard", shard: {index: 3, count: 3}}), new Map(), []);
    check(refused.code === 1 && refused.error.includes("shard out of range"), `a shard out of range: ${refused.error}`);
    const unknown = await run(createOblImport, files, JSON.stringify({...JSON.parse(plan), stage: "later"}), new Map(), []);
    check(unknown.code === 1 && unknown.error.includes("unknown stage"), `an unknown stage: ${unknown.error}`);
}

// The browser's pick among the copy's tables; every case stops at the fake executable, after the language is settled.
async function languageRule(createOblImport, dir) {
    const pick = async (name, tables, ini, preferredLanguage) => {
        const game = path.join(dir, name);
        fs.mkdirSync(path.join(game, "Data"), {recursive: true});
        fs.writeFileSync(path.join(game, "BeachLife.exe"), "not an executable");
        for (const table of tables) fs.writeFileSync(path.join(game, "Data", table), "table");
        if (ini) fs.writeFileSync(path.join(game, "BeachLife.ini"), ini);
        const files = pickFolder(game);
        const plan = {source: "files", files: files.map(({path, size}) => ({path, size})), preferredLanguage};
        return run(createOblImport, files, JSON.stringify(plan), new Map(), []);
    };
    const noEnglish = ["text_french.ble", "text_polish.ble"];
    const polishIni = "[Language]\nLanguage=5\n";
    let result = await pick("ini", noEnglish, polishIni, "fr");
    check(result.language === "pl", `the copy's INI wins: ${result.language}`);
    result = await pick("ini-table-missing", ["text_french.ble"], polishIni, "de");
    check(result.language === "fr", `an INI language without its table is skipped: ${result.language}`);
    result = await pick("browser", noEnglish, null, "pl");
    check(result.language === "pl", `the browser's language next: ${result.language}`);
    result = await pick("first", noEnglish, null, "en");
    check(result.language === "fr" && result.error.startsWith("gui-tables: "),
        `a copy without English in an English browser imports with the first table: ${result.language}, ${result.error}`);
    result = await pick("unknown", noEnglish, null, "");
    check(result.language === "fr", `an unsupported browser language: ${result.language}`);
    result = await pick("no-tables", [], null, "en");
    check(result.code === 1 && result.error.includes("no text tables"), `no text table stays an error: ${result.error}`);
}

// The manifest's differing fields, so a mismatch names its cause instead of only the file.
function printJsonDifference(actual, expected, where) {
    if (actual && expected && typeof actual === "object" && typeof expected === "object") {
        for (const key of new Set([...Object.keys(actual), ...Object.keys(expected)]))
            printJsonDifference(actual[key], expected[key], `${where}/${key}`);
    } else if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        console.log(`    ${where || "/"}: ${JSON.stringify(actual)} (expected ${JSON.stringify(expected)})`);
    }
}

async function full(createOblImport, game, iso, compare, language, out, workers) {
    // The image is file -1 of the read callback (worker_main.cpp).
    const files = iso ? Object.assign([], {[-1]: {full: iso, size: fs.statSync(iso).size}}) : pickFolder(game);
    const plan = iso ? {source: "iso", root: "packs/base", isoSize: files[-1].size}
        : {source: "files", root: "packs/base", files: files.map(({path, size}) => ({path, size}))};
    if (language) plan.language = language;
    const store = new Map();
    const steps = [];
    const counts = new Map();
    const started = Date.now();
    const result = await run(createOblImport, files, JSON.stringify(plan), store, steps, counts);
    const aloneSeconds = (Date.now() - started) / 1000;
    check(result.code === 0, `import (${steps.join(", ")}; ${Math.round(aloneSeconds)} s)${result.error ? ": " + result.error : ""}`);
    if (result.code !== 0) return;
    for (const step of ["maps", "sprites", "pack"]) {
        const seen = counts.get(step);
        check(seen && seen.total > 0 && seen.done === seen.total && seen.reports > 1,
            `progress bar: ${step} counts to its total (${seen ? `${seen.reports} reports, ${seen.done} of ${seen.total}` : "no report"})`);
    }
    if (language) {
        const pack = JSON.parse(Buffer.from(store.get("packs/base/pack.json")));
        check(pack.language === language && store.has(`packs/base/text/${language}.json`), `the pack's language is ${language}`);
    }
    if (workers > 1) await sharded(createOblImport, files, plan, store, steps, aloneSeconds, workers);
    if (out) {
        for (const [key, bytes] of store) {
            const target = path.join(out, key.slice("packs/base/".length));
            fs.mkdirSync(path.dirname(target), {recursive: true});
            fs.writeFileSync(target, bytes);
        }
        console.log(`wrote ${store.size} files to ${out}`);
    }
    if (!compare) return;
    const expected = pickFolder(compare).map((file) => ({...file, relative: file.path.slice(path.basename(compare).length + 1)}));
    let differing = 0;
    for (const file of expected) {
        const bytes = store.get(`packs/base/${file.relative}`);
        if (!bytes || !Buffer.from(bytes).equals(fs.readFileSync(file.full))) {
            if (differing++ < 10) console.log(`  differs or missing: ${file.relative}`);
            if (bytes && file.relative === "pack.json") printJsonDifference(JSON.parse(Buffer.from(bytes)), JSON.parse(fs.readFileSync(file.full)), "");
        }
    }
    const extra = store.size - expected.length;
    check(differing === 0 && extra === 0, `${store.size} files against ${expected.length} of ${compare}: ${differing} differ, ${extra} extra`);
}

// The same import with helper threads: the same steps and the very same files, pack.json included, as the import by one worker.
async function sharded(createOblImport, files, plan, alone, aloneSteps, aloneSeconds, workers) {
    const store = new Map();
    const steps = [];
    const counts = new Map();
    const started = Date.now();
    const result = await run(createOblImport, files, JSON.stringify(plan), store, steps, counts, workers);
    const seconds = (Date.now() - started) / 1000;
    check(result.code === 0 && result.sharded, `import with ${workers} helpers took the helpers' units (${result.error})`);
    console.log(`timing: 1 worker ${aloneSeconds.toFixed(1)} s, ${workers} helpers ${seconds.toFixed(1)} s (${(aloneSeconds / seconds).toFixed(2)}x)`);
    if (result.code !== 0) return;
    let differing = 0;
    for (const [key, bytes] of alone) {
        const other = store.get(key);
        if (!other || !Buffer.from(bytes).equals(Buffer.from(other))) {
            if (differing++ < 10) console.log(`  differs or missing with helpers: ${key}`);
        }
    }
    check(differing === 0 && store.size === alone.size, `${store.size} files with ${workers} helpers against ${alone.size} with one worker: ${differing} differ`);
    // The page's bar: the helpers' counts add up to the whole, and the steps after them are announced as before.
    const last = result.shardProgress[result.shardProgress.length - 1];
    check(last && last.total > 0 && last.done === last.total, `progress bar: the helpers' units count to their total (${last ? `${last.done} of ${last.total}` : "no report"})`);
    check(steps.join() === aloneSteps.filter((step) => !shard.kQuietSteps.has(step)).join(),
        "progress bar: the steps announced after the helpers' share are those of one worker, without the ones the helpers showed");
    const pack = counts.get("pack");
    check(pack && pack.done === pack.total, "progress bar: pack counts to its total with helpers");
}

async function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) throw new Error("usage: node node_check.cjs <obl-import-web.js> [--game <dir> | --iso <image>] [--compare <pack dir>] [--language <lang>]");
    wasmPath = path.resolve(args[0]);
    const createOblImport = require(wasmPath);
    const option = (name) => {
        const at = args.indexOf(name);
        return at > 0 ? args[at + 1] : undefined;
    };
    if (option("--game") || option("--iso")) await full(createOblImport, option("--game"), option("--iso"), option("--compare"), option("--language"), option("--out"), Number(option("--workers") || 1));
    else await synthetic(createOblImport);
    process.exit(failures ? 1 : 0);
}

if (isMainThread) {
    main().catch((error) => {
        console.log(`FAIL ${error.stack || error}`);
        process.exit(1);
    });
} else {
    helperMain();
}
