// The browser game's --pre-js (docs/architecture/browser-build.md). Before main it copies the game files from the
// origin's private file system (OPFS) into MEMFS /game, so every loader keeps its synchronous reads, then main starts with
// --game-dir /game. The OPFS tree: packs/base/... as the browser importer (importer/web/import_worker.js) writes it, plus
// the player's BeachLife.ini and SaveGames/. src/launcher/browser_files_check.cjs runs it under Node against a fake OPFS.

// The runtime reads the arguments of main when its code loads, after this file.
Module["arguments"] = ["--game-dir", "/game"].concat(Module["arguments"] || []);

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
    const logFile = "openbeach.log";
    const flushIntervalMs = 2000;
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

    // The log goes to the console as the game writes it; the file stays downloadable.
    let logged = 0;
    function copyLogToConsole() {
        let bytes;
        try {
            bytes = FS.readFile(target + "/" + logFile);
        } catch (error) {
            return;
        }
        if (bytes.length < logged) logged = 0; // the game reopened the log
        if (bytes.length === logged) return;
        const text = new TextDecoder().decode(bytes.subarray(logged));
        logged = bytes.length;
        console.log(text.replace(/\n$/, ""));
    }

    Module["downloadLog"] = function oblDownloadLog() {
        copyLogToConsole();
        triggerDownload(logFile, FS.readFile(target + "/" + logFile), "text/plain");
    };

    function triggerDownload(name, bytes, type) {
        const url = URL.createObjectURL(new Blob([bytes], {type}));
        const link = document.createElement("a");
        link.href = url;
        link.download = name;
        link.click();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
    }

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
        triggerDownload("openbeach-saves.json", new TextEncoder().encode(Module["exportSaves"]()), "application/json");
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
            copyLogToConsole();
            Module["flushGameFiles"]();
        }, flushIntervalMs);
        document.addEventListener("visibilitychange", () => {
            if (document.visibilityState === "hidden") Module["flushGameFiles"]();
        });
        window.addEventListener("pagehide", () => {
            copyLogToConsole();
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
    acquireLock().then(load).then((files) => {
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
