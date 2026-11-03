// The browser import (docs/architecture/converter-review/importer-boundary.md, "Browser adapter"): a dedicated Web
// Worker that converts the player's copy of Beach Life into the base pack, entirely on the player's machine. Nothing is
// uploaded; the pack ends up in the origin private file system (OPFS) under `root`.
//
// The page starts it with `new Worker("import_worker.js")` next to obl-import-web.js/.wasm and posts one of
//   {source: "files", files: [File, ...], paths?, root?}  the files of a folder: `paths` for a dropped one, else webkitRelativePath
//   {source: "iso", image: File, root?}            a disc image
// (the importer picks the pack's language among the copy's tables; on success the worker also sets Language in OPFS
// BeachLife.ini to its index in the game's numbering, so the game and the converted texts agree) and receives
// {type: "progress", step, done?, total?} (done/total count the items of a long step), then {type: "done", files} or {type: "error", message}.
// `workers` (optional) is the number of helper workers to start, for tests; by default shard.js chooses it from the machine.
//
// With helpers (shard.js), this worker is the coordinator: it starts them as nested workers running this same script, each
// with its own wasm instance and no shared memory, hands them the picked files (File objects) and the spooled CD archive
// (Blobs), and they convert their shares of the sprites and maps while this worker only gathers their progress. It then runs
// the whole import itself, taking their finished units instead of converting them, so the pack is the one a single worker
// writes. A helper that fails ends the helpers, and this worker imports alone: its error is then a single worker's.
//
// The converters run synchronously, so this worker answers their calls synchronously (Module.oblImport, read by
// worker_main.cpp): reads go through FileReaderSync, written files stay Blobs (the browser may keep them on disk) until
// the import has succeeded, and only then are they stored in OPFS, replacing an older pack. A failed import leaves the
// older pack as it was. A CD's files too large for the module's heap (the 467 MB sprite archive) are spooled the same
// way: spoolCreate/spoolAppend build a Blob piece by piece, spoolRead reads ranges of it, spoolRemove drops it.
"use strict";

importScripts("obl-import-web.js", "language_ini.js", "shard.js");

const reader = new FileReaderSync();
const {chooseHelperCount, runHelpers, runShardJob, kQuietSteps, kQuietCounts} = self.oblShard;

// Unit bytes are kept as Blobs (the browser may keep them on disk), and a Blob goes to another worker without being copied.
const kBlobCodec = {
    keep: (bytes) => new Blob([bytes]),
    read: (blob) => new Uint8Array(reader.readAsArrayBuffer(blob)),
};

// Appended bytes are folded into the spool's Blob in pieces of this size, so few small arrays stay in memory.
const kSpoolPiece = 16 << 20;

// The converters read in small steps (a cabinet chunk is a 2-byte length and then up to 64 KB, a sprite block a few KB),
// and each FileReaderSync call has a fixed cost that dominates on a phone, where a picked file is served by the system's
// storage provider. A miss therefore reads this much ahead, and the following reads are answered from memory.
const kReadAhead = 4 << 20;

// One window of `blob`; a different blob (another file, or a spool that was folded) simply misses.
function makeWindowReader() {
    let blob = null, start = 0, bytes = null;
    return (source, offset, target) => {
        const end = offset + target.length;
        if (end > source.size) return false;
        if (target.length >= kReadAhead) {
            target.set(new Uint8Array(reader.readAsArrayBuffer(source.slice(offset, end))));
            return true;
        }
        if (source !== blob || offset < start || end > start + bytes.length) {
            blob = source;
            start = offset;
            bytes = new Uint8Array(reader.readAsArrayBuffer(source.slice(offset, Math.min(source.size, offset + kReadAhead))));
        }
        target.set(bytes.subarray(offset - start, end - start));
        return true;
    };
}

// The calls of worker_main.cpp; `pending` holds the written files by storage key.
// `options.spools`: [name, Blob] pairs another worker extracted, which this one's CD source adopts instead of extracting again.
function makeBridge(inputs, pending, report, options = {}) {
    const spools = new Map(); // name -> {blob, parts, size of parts}
    for (const [name, blob] of options.spools || []) spools.set(name, {blob, parts: [], partBytes: 0});
    let mute = null; // steps whose progress is not reported: the replay of what the helpers already showed
    const readInput = makeWindowReader();
    const readSpool = makeWindowReader();
    const fold = (spool) => {
        if (spool.parts.length === 0) return;
        spool.blob = new Blob([spool.blob, ...spool.parts]);
        spool.parts = [];
        spool.partBytes = 0;
    };
    return {
        read(index, offset, target) {
            const file = index < 0 ? inputs.image : inputs.files[index];
            return !!file && readInput(file, offset, target);
        },
        put(key, bytes) {
            pending.set(key, new Blob([bytes]));
            return true;
        },
        get(key) {
            const blob = pending.get(key);
            return blob ? new Uint8Array(reader.readAsArrayBuffer(blob)) : null;
        },
        delete(key) {
            pending.delete(key);
            return true;
        },
        keys(prefix) {
            return [...pending.keys()].filter((key) => key.startsWith(prefix));
        },
        spoolCreate(name) {
            spools.set(name, {blob: new Blob([]), parts: [], partBytes: 0});
            return true;
        },
        spoolAppend(name, bytes) {
            const spool = spools.get(name);
            if (!spool) return false;
            spool.parts.push(bytes);
            spool.partBytes += bytes.length;
            if (spool.partBytes >= kSpoolPiece) fold(spool);
            return true;
        },
        spoolRead(name, offset, target) {
            const spool = spools.get(name);
            if (!spool) return false;
            fold(spool);
            return readSpool(spool.blob, offset, target);
        },
        spoolRemove(name) {
            spools.delete(name);
        },
        spoolHas: (name) => spools.has(name),
        exportSpools() {
            const exported = [];
            for (const [name, spool] of spools) {
                fold(spool);
                exported.push([name, spool.blob]);
            }
            return exported;
        },
        muteReplay(on) {
            mute = on ? {steps: kQuietSteps, counts: kQuietCounts} : null;
        },
        progress(step) {
            if (mute && mute.steps.has(step)) return;
            report({type: "progress", step, at: Date.now()});
        },
        count(step, done, total) {
            if (mute && mute.counts.has(step)) return;
            report({type: "progress", step, done, total, at: Date.now()});
        },
    };
}

async function directoryOf(rootHandle, parts, create) {
    let dir = rootHandle;
    for (const part of parts) dir = await dir.getDirectoryHandle(part, {create});
    return dir;
}

// Replaces the pack under `root` in OPFS with the pending files.
// `report(done, total)` is called every kStoreReport files and at the end, for the page's "Saving" bar.
const kStoreReport = 25;

async function persist(root, pending, report) {
    const opfs = await navigator.storage.getDirectory();
    const rootParts = root.split("/");
    const parent = await directoryOf(opfs, rootParts.slice(0, -1), true);
    await parent.removeEntry(rootParts[rootParts.length - 1], {recursive: true}).catch(() => {});
    let done = 0;
    report(0, pending.size);
    // A pack has a few hundred directories for thousands of files, so each directory is looked up once.
    const directories = new Map();
    const directoryFor = async (parts) => {
        const name = parts.join("/");
        let dir = directories.get(name);
        if (!dir) {
            dir = parts.length === 0 ? opfs : await (await directoryFor(parts.slice(0, -1))).getDirectoryHandle(parts[parts.length - 1], {create: true});
            directories.set(name, dir);
        }
        return dir;
    };
    for (const [key, blob] of pending) {
        const parts = key.split("/");
        const dir = await directoryFor(parts.slice(0, -1));
        const handle = await (await dir.getFileHandle(parts[parts.length - 1], {create: true})).createSyncAccessHandle();
        try {
            handle.truncate(0);
            handle.write(new Uint8Array(reader.readAsArrayBuffer(blob)), {at: 0});
            handle.flush();
        } finally {
            handle.close();
        }
        if (++done % kStoreReport === 0 || done === pending.size) report(done, pending.size);
    }
}

async function writeLanguage(language) {
    const opfs = await navigator.storage.getDirectory();
    let old = "";
    try {
        old = await (await (await opfs.getFileHandle("BeachLife.ini")).getFile()).text();
    } catch (error) {
        if (!error || error.name !== "NotFoundError") throw error;
    }
    const handle = await (await opfs.getFileHandle("BeachLife.ini", {create: true})).createSyncAccessHandle();
    try {
        const bytes = new TextEncoder().encode(withLanguage(old, language));
        handle.truncate(0);
        handle.write(bytes, {at: 0});
        handle.flush();
    } finally {
        handle.close();
    }
}

// The plan the module reads: where the files come from and where the pack goes.
function planOf(request, root) {
    // The player chooses on the start layer, among the pack's tables; the browser's language is only a hint for the first start.
    const plan = {source: request.source, root, preferredLanguage: browserLanguage(navigator.language)};
    if (request.source === "iso") {
        plan.isoSize = request.image.size;
    } else {
        plan.files = request.files.map((file, index) => ({
            path: (request.paths && request.paths[index]) || file.webkitRelativePath || file.name, size: file.size}));
    }
    return plan;
}

// The helper's side (shard.js): converts a share of the units and posts them back.
function runHelper(job) {
    const post = (message) => self.postMessage(message);
    runShardJob(job, {
        codec: kBlobCodec,
        createModule: () => createOblImport(),
        makeBridge: (shardJob, store, report) => makeBridge(shardJob.request, new Map(), report, {spools: shardJob.spools}),
    }, post);
}

// Converts the sprites and maps on helper workers; true when `units` holds what they made. False: the caller imports alone.
async function convertOnHelpers(request, plan, module, bridge, units, helpers) {
    const run = (extra) => module.ccall("obl_import", "number", ["string"], [JSON.stringify({...plan, ...extra})]);
    // A CD's sprite archive is extracted once, here; the helpers adopt it from the spool instead of each extracting it.
    if (run({stage: "prepare"}) !== 0) return false;
    const spools = bridge.exportSpools();
    const result = await runHelpers({
        count: helpers,
        spawn: () => new Worker(self.location.href),
        jobFor: (index) => ({type: "shard", plan: JSON.stringify(plan), shard: {index, count: helpers}, request, spools}),
        store: units,
        report: (message) => self.postMessage(message),
    });
    if (!result.ok) units.clear();
    return result.ok;
}

async function importPack(request) {
    const root = request.root || "packs/base";
    const pending = new Map();
    const module = await createOblImport();
    const units = self.oblShard.makeUnitStore(kBlobCodec);
    const bridge = makeBridge(request, pending, (message) => self.postMessage(message));
    module.oblImport = Object.assign(bridge, units.bridge);
    const plan = planOf(request, root);
    const helpers = typeof Worker === "undefined" ? 1 : request.workers || chooseHelperCount({
        hardwareConcurrency: navigator.hardwareConcurrency, deviceMemory: navigator.deviceMemory});
    const shared = helpers > 1 && await convertOnHelpers(request, plan, module, bridge, units, helpers);
    if (shared) {
        plan.stage = "units";
        bridge.muteReplay(true);
    }
    if (module.ccall("obl_import", "number", ["string"], [JSON.stringify(plan)]) !== 0) {
        self.postMessage({type: "error", message: module.UTF8ToString(module._obl_import_error())});
        return;
    }
    await persist(root, pending, (done, total) => self.postMessage({type: "progress", step: "storing", done, total, at: Date.now()}));
    await writeLanguage(module.UTF8ToString(module._obl_import_language()));
    self.postMessage({type: "done", files: pending.size});
}

self.onmessage = async (event) => {
    const request = event.data;
    if (request.type === "shard") {
        runHelper(request);
        return;
    }
    try {
        await importPack(request);
    } catch (error) {
        self.postMessage({type: "error", message: String(error)});
    }
};
