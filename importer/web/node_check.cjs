// Runs the browser import's WebAssembly module (obl-import-web.js) under Node with the bridge import_worker.js gives it
// in the browser, backed by the local file system and a Map instead of File objects and OPFS. tools/wasm_linux.sh runs it.
//   node node_check.cjs <obl-import-web.js>
//       the synthetic checks: request errors and the bridge calls (read, progress), no game data
//   node node_check.cjs <obl-import-web.js> --game <installation> [--compare <pack directory>] [--language <lang>] [--out <dir>]
//       a full import of the installation (or a CD root: its folder of files); the long steps (maps, sprites, pack) must
//       report their counts up to the total. With --compare the files must equal the pack directory's byte for byte;
//       --out writes the imported pack to <dir> (e.g. for tools/browser_probe.cjs --pack)
// Exit code 1 on a failed check.
"use strict";

const fs = require("fs");
const path = require("path");

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
function makeBridge(files, store, steps, counts = new Map()) {
    const spoolDir = fs.mkdtempSync(path.join(require("os").tmpdir(), "obl-import-spool-"));
    process.on("exit", () => fs.rmSync(spoolDir, {recursive: true, force: true}));
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
        progress(step) {
            steps.push(step);
        },
        count(step, done, total) {
            const seen = counts.get(step) || {reports: 0, done: 0, total: 0};
            counts.set(step, {reports: seen.reports + 1, done, total});
        },
    };
}

async function run(createOblImport, files, plan, store, steps, counts) {
    const module = await createOblImport();
    module.oblImport = makeBridge(files, store, steps, counts);
    const code = module.ccall("obl_import", "number", ["string"], [plan]);
    return {code, error: module.UTF8ToString(module._obl_import_error()), language: module.UTF8ToString(module._obl_import_language())};
}

async function synthetic(createOblImport) {
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
        result = await run(createOblImport, files, JSON.stringify({source: "iso", isoSize: 4096}), new Map(), []);
        check(result.code === 1 && result.error.includes("ISO 9660"), `not a disc image: ${result.error}`);
    } finally {
        fs.rmSync(dir, {recursive: true, force: true});
    }
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

async function full(createOblImport, game, compare, language, out) {
    const files = pickFolder(game);
    const plan = {source: "files", root: "packs/base", files: files.map(({path, size}) => ({path, size}))};
    if (language) plan.language = language;
    const store = new Map();
    const steps = [];
    const counts = new Map();
    const started = Date.now();
    const result = await run(createOblImport, files, JSON.stringify(plan), store, steps, counts);
    check(result.code === 0, `import (${steps.join(", ")}; ${Math.round((Date.now() - started) / 1000)} s)${result.error ? ": " + result.error : ""}`);
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

async function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) throw new Error("usage: node node_check.cjs <obl-import-web.js> [--game <dir> --compare <pack dir> [--language <lang>]]");
    const createOblImport = require(path.resolve(args[0]));
    const option = (name) => {
        const at = args.indexOf(name);
        return at > 0 ? args[at + 1] : undefined;
    };
    if (option("--game")) await full(createOblImport, option("--game"), option("--compare"), option("--language"), option("--out"));
    else await synthetic(createOblImport);
    process.exit(failures ? 1 : 0);
}

main().catch((error) => {
    console.log(`FAIL ${error.stack || error}`);
    process.exit(1);
});
