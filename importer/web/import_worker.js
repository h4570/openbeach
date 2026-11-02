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
//
// The converters run synchronously, so this worker answers their calls synchronously (Module.oblImport, read by
// worker_main.cpp): reads go through FileReaderSync, written files stay Blobs (the browser may keep them on disk) until
// the import has succeeded, and only then are they stored in OPFS, replacing an older pack. A failed import leaves the
// older pack as it was. A CD's files too large for the module's heap (the 467 MB sprite archive) are spooled the same
// way: spoolCreate/spoolAppend build a Blob piece by piece, spoolRead reads ranges of it, spoolRemove drops it.
"use strict";

importScripts("obl-import-web.js", "language_ini.js");

const reader = new FileReaderSync();

// Appended bytes are folded into the spool's Blob in pieces of this size, so few small arrays stay in memory.
const kSpoolPiece = 16 << 20;

// The calls of worker_main.cpp; `pending` holds the written files by storage key.
function makeBridge(inputs, pending, report) {
    const spools = new Map(); // name -> {blob, parts, size of parts}
    const fold = (spool) => {
        if (spool.parts.length === 0) return;
        spool.blob = new Blob([spool.blob, ...spool.parts]);
        spool.parts = [];
        spool.partBytes = 0;
    };
    return {
        read(index, offset, target) {
            const file = index < 0 ? inputs.image : inputs.files[index];
            if (!file || offset + target.length > file.size) return false;
            target.set(new Uint8Array(reader.readAsArrayBuffer(file.slice(offset, offset + target.length))));
            return true;
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
            if (offset + target.length > spool.blob.size) return false;
            target.set(new Uint8Array(reader.readAsArrayBuffer(spool.blob.slice(offset, offset + target.length))));
            return true;
        },
        spoolRemove(name) {
            spools.delete(name);
        },
        progress(step) {
            report({type: "progress", step, at: Date.now()});
        },
        count(step, done, total) {
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
    for (const [key, blob] of pending) {
        const parts = key.split("/");
        const dir = await directoryOf(opfs, parts.slice(0, -1), true);
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

self.onmessage = async (event) => {
    const request = event.data;
    const root = request.root || "packs/base";
    const pending = new Map();
    try {
        const module = await createOblImport();
        module.oblImport = makeBridge(request, pending, (message) => self.postMessage(message));
        // The player chooses on the start layer, among the pack's tables; the browser's language is only a hint for the first start.
        const plan = {source: request.source, root, preferredLanguage: browserLanguage(navigator.language)};
        if (request.source === "iso") {
            plan.isoSize = request.image.size;
        } else {
            plan.files = request.files.map((file, index) => ({
                path: (request.paths && request.paths[index]) || file.webkitRelativePath || file.name, size: file.size}));
        }
        if (module.ccall("obl_import", "number", ["string"], [JSON.stringify(plan)]) !== 0) {
            self.postMessage({type: "error", message: module.UTF8ToString(module._obl_import_error())});
            return;
        }
        await persist(root, pending, (done, total) => self.postMessage({type: "progress", step: "storing", done, total, at: Date.now()}));
        await writeLanguage(module.UTF8ToString(module._obl_import_language()));
        self.postMessage({type: "done", files: pending.size});
    } catch (error) {
        self.postMessage({type: "error", message: String(error)});
    }
};
