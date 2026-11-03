// Runs the site's page logic (site.js) under Node against a fake DOM, OPFS and Worker: which layer shows, when the game
// script is added, what the importer worker is sent. With a site directory (tools/web_site.sh's output) it also checks
// that every file the page loads is there. tools/wasm_linux.sh runs it.
//   node site_check.cjs [<site.js>] [--site <directory>]
// Exit code 1 on a failed check.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

let failures = 0;
function check(condition, what) {
    console.log(`${condition ? "PASS" : "FAIL"} ${what}`);
    if (!condition) failures++;
}

const args = process.argv.slice(2);
const siteIndex = args.indexOf("--site");
const siteDir = siteIndex >= 0 ? args.splice(siteIndex, 2)[1] : null;
const scriptPath = args[0] || path.join(__dirname, "site.js");
const source = fs.readFileSync(scriptPath, "utf8");

const languageSource = fs.readFileSync(path.join(__dirname, "..", "importer", "web", "language_ini.js"), "utf8");
const textSource = fs.readFileSync(path.join(__dirname, "site_text.js"), "utf8");

const notFound = () => Object.assign(new Error("not found"), {name: "NotFoundError"});

function fakeDirectory(tree) {
    return {
        async getDirectoryHandle(name) {
            const child = tree[name];
            if (!child || child instanceof Uint8Array) throw notFound();
            return fakeDirectory(child);
        },
    };
}

function fakeElement(id) {
    const listeners = {};
    const classes = new Set();
    return {
        classList: {
            add: (name) => classes.add(name),
            remove: (name) => classes.delete(name),
            contains: (name) => classes.has(name),
            toggle: (name, on) => (on ? classes.add(name) : classes.delete(name)),
        },
        dataset: {},
        id, hidden: false, disabled: false, textContent: "", value: "", max: 1, files: null, src: "", async: false, focused: false,
        children: [], width: 0, height: 0, style: {props: {}, setProperty(name, value) {
            this.props[name] = value;
        }},
        replaceChildren(...nodes) {
            this.children = nodes;
        },
        requestFullscreen() {
            this.fullscreenRequests = (this.fullscreenRequests || 0) + 1;
        },
        requestPointerLock() {
            this.lockRequests = (this.lockRequests || 0) + 1;
        },
        addEventListener(type, listener) {
            listeners[type] = listener;
        },
        dispatchEvent(event) {
            this.dispatched = this.dispatched || [];
            this.dispatched.push(event);
        },
        focus() {
            this.focused = true;
        },
        async fire(type, event = {}) {
            await listeners[type]({target: this, ...event});
        },
    };
}

const flush = () => new Promise((resolve) => setImmediate(resolve));

// One page load: `opfs` is the OPFS tree (null: no OPFS), `search` the URL's query.
async function loadPage({opfs = {}, search = "", languages = ["pl-PL"], stored = {}} = {}) {
    const ids = ["canvas", "import", "import-folder", "import-iso", "import-status", "loading", "loading-text", "start", "start-button",
        "import-saves", "start-status", "ended", "import-progress", "import-main", "import-step", "import-percent", "import-sub",
        "import-count", "import-bars", "drop", "drop-status", "game-bar", "fullscreen", "site-language",
        "start-language", "start-language-row", "key-space", "key-escape"];
    const elements = Object.fromEntries(ids.map((id) => [id, fakeElement(id)]));
    elements["loading-text"].textContent = "Loading...";
    Object.assign(elements["key-space"].dataset, {code: "Space", key: " "});
    Object.assign(elements["key-escape"].dataset, {code: "Escape", key: "Escape"});
    const translated = fakeElement("translated");
    translated.dataset.t = "play";
    const body = fakeElement("body");
    body.appendChild = (child) => scripts.push(child);
    for (const id of ["import", "start", "ended", "start-language-row", "import-bars", "import-sub", "game-bar"]) elements[id].hidden = true;
    const scripts = [];
    const workers = [];
    const page = {reloads: 0, persistCalls: 0, elements, scripts, workers, translated, body, stored};
    class FakeMutationObserver {
        constructor(callback) {
            page.observed = callback;
        }
        observe(target, options) {
            page.observeOptions = options;
        }
    }
    class FakeWorker {
        constructor(url) {
            this.url = url;
            this.sent = [];
            this.terminated = false;
            workers.push(this);
        }
        postMessage(message) {
            this.sent.push(message);
        }
        terminate() {
            this.terminated = true;
        }
    }
    const context = {
        console,
        URLSearchParams,
        MutationObserver: FakeMutationObserver,
        KeyboardEvent: class {
            constructor(type, init) {
                this.type = type;
                Object.assign(this, init);
            }
        },
        Worker: FakeWorker,
        location: {search, pathname: "/", reload() {
            page.reloads++;
        }},
        history: {replaceState(state, title, url) {
            page.address = url;
        }},
        localStorage: {
            getItem: (key) => (key in stored ? stored[key] : null),
            setItem: (key, value) => {
                stored[key] = value;
            },
        },
        navigator: opfs ? {language: languages[0], languages, storage: {async getDirectory() {
            return fakeDirectory(opfs);
        }, async persist() {
            page.persistCalls++;
            return true;
        }}} : {language: languages[0], languages},
        document: {
            getElementById: (id) => elements[id],
            createElement: (tag) => fakeElement(tag),
            body,
            documentElement: {lang: "", requestFullscreen() {
                page.fullscreenRequests = (page.fullscreenRequests || 0) + 1;
            }},
            querySelectorAll: () => [translated],
            pointerLockElement: null,
            exitPointerLock() {
                this.pointerLockElement = null;
                page.lockExits = (page.lockExits || 0) + 1;
            },
            addEventListener(type, listener) {
                page.documentListeners = {...page.documentListeners, [type]: listener};
            },
        },
    };
    vm.createContext(context);
    vm.runInContext(languageSource, context, {filename: "language_ini.js"});
    vm.runInContext(textSource, context, {filename: "site_text.js"});
    vm.runInContext(source, context, {filename: scriptPath});
    await context.siteStarted;
    page.context = context;
    page.visible = () => ["import", "loading", "start", "ended"].filter((id) => !elements[id].hidden);
    return page;
}

const loaded = []; // every script and worker URL a page asked for, for the site directory check

async function main() {
    const packTree = () => ({packs: {base: {"pack.json": new Uint8Array([1])}}});

    // No pack: the picker shows and the game script stays out.
    let page = await loadPage();
    check(page.visible().join() === "import", "no pack: only the importer shows");
    check(page.scripts.length === 0, "no pack: the game script is not added");

    // The page follows the browser's first language it has, and a stored choice wins.
    check(page.translated.textContent === "Graj" && page.context.document.documentElement.lang === "pl",
        "page language: the browser's language is picked and applied");
    check(page.elements["site-language"].children.map((o) => o.value).join() === "en,fr,it,de,es,pl" &&
        page.elements["site-language"].value === "pl", "page language: the switch lists all six");
    page.elements["site-language"].value = "de";
    await page.elements["site-language"].fire("change");
    check(page.translated.textContent === "Spielen" && page.stored["openbeach-page-language"] === "de",
        "page language: the switch retells the page and remembers the choice");
    const remembered = await loadPage({stored: {"openbeach-page-language": "fr"}});
    check(remembered.translated.textContent === "Jouer", "page language: a stored choice wins over the browser");
    const unknown = await loadPage({languages: ["ja-JP", "it-IT"]});
    check(unknown.translated.textContent === "Gioca", "page language: the first language the page has is taken");
    const none = await loadPage({languages: ["ja-JP"]});
    check(none.translated.textContent === "Play", "page language: English is the fallback");
    page.elements["site-language"].value = "pl";
    await page.elements["site-language"].fire("change");

    const folderFiles = [{name: "BeachLife.exe", webkitRelativePath: "Beach Life/BeachLife.exe", size: 3}];
    // A picked folder goes to the worker with its files; the game script follows the worker's "done".
    await page.elements["import-folder"].fire("change", {target: {files: folderFiles}});
    check(page.persistCalls === 1, "import: persistent storage is requested");
    let worker = page.workers[0];
    loaded.push(worker.url);
    check(worker && worker.url === "import_worker.js", "folder: the import worker starts");
    check(worker.sent.length === 1 && worker.sent[0].source === "files" && worker.sent[0].files[0] === folderFiles[0],
        "folder: the worker gets the picked files");
    check(page.elements["import-folder"].disabled && page.elements["import-iso"].disabled && page.elements.drop.hidden &&
        !page.elements["import-bars"].hidden, "folder: the bars replace the drop zone while it runs");
    worker.onmessage({data: {type: "progress", step: "gui-tables"}});
    check(page.elements["import-main"].value === 0 && page.elements["import-sub"].hidden &&
        page.elements["import-step"].textContent === "Krok 1 z 16: tabele interfejsu", "progress: the first step, no second bar without counts");
    worker.onmessage({data: {type: "progress", step: "sprites"}});
    const beforeSprites = page.elements["import-main"].value;
    check(beforeSprites > 0 && page.elements["import-sub"].hidden, "progress: finished steps fill the main bar");
    worker.onmessage({data: {type: "progress", step: "sprites", done: 25, total: 100}});
    const bar = page.elements["import-progress"];
    check(!page.elements["import-sub"].hidden && bar.value === 25 && bar.max === 100 && page.elements["import-count"].textContent === "25 / 100" &&
        page.elements["import-main"].value > beforeSprites, "progress: counts fill the second bar and move the main one");
    page.elements["site-language"].value = "de";
    await page.elements["site-language"].fire("change");
    check(page.elements["import-step"].textContent === "Schritt 9 von 16: Sprites", "progress: a language change retells the running step");
    page.elements["site-language"].value = "pl";
    await page.elements["site-language"].fire("change");
    check(!("language" in worker.sent[0]), "folder: the page names no language, the importer picks it from the copy");
    check(page.scripts.length === 0, "folder: no game script before the import is done");
    worker.onmessage({data: {type: "progress", step: "storing", done: 0, total: 5600}});
    check(page.elements["import-step"].textContent === "Krok 16 z 16: zapis w przeglądarce" && bar.max === 5600,
        "storing: the last step, with the file count");
    worker.onmessage({data: {type: "progress", step: "storing", done: 5600, total: 5600}});
    check(page.elements["import-main"].value === 1, "storing: the main bar ends full");

    // The canvas box follows the canvas's pixel size: 1280x720 is 16:9 (not forced to 4:3) and a resize updates it.
    const canvas = page.elements.canvas;
    check(page.observeOptions && page.observeOptions.attributeFilter.join() === "width,height",
        "resolution: the page watches the canvas's width and height");
    canvas.width = 1280;
    canvas.height = 720;
    page.observed();
    check(Math.abs(Number(canvas.style.props["--aspect"]) - 16 / 9) < 1e-9, "resolution: 1280x720 gives the 16:9 aspect");
    canvas.width = 800;
    canvas.height = 600;
    page.observed();
    check(Math.abs(Number(canvas.style.props["--aspect"]) - 4 / 3) < 1e-9, "resolution: back to 800x600 gives 4:3 again");
    const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
    check(html.includes("var(--aspect") && !/calc\(100v[hw] \* [34] \/ [34]\)/.test(html),
        "resolution: index.html sizes the canvas by --aspect, not a fixed 4:3");

    // Every text the page names exists in every language.
    const named = [...html.matchAll(/data-t="(\w+)"/g)].map((match) => match[1]);
    const texts = vm.runInContext("kSiteTexts", page.context);
    check(named.length > 0 && Object.values(texts).every((language) => named.every((key) => key in language)),
        "texts: every data-t key is in all six languages");

    // The right button locks the pointer inside its press; the left one does not, and the lock ends with the button.
    await canvas.fire("pointerdown", {button: 0, pointerType: "mouse", buttons: 1});
    check(!canvas.lockRequests, "drag lock: the left button does not lock");
    await canvas.fire("pointerdown", {button: 2, pointerType: "touch", buttons: 2});
    check(!canvas.lockRequests, "drag lock: a touch does not lock");
    await canvas.fire("pointerdown", {button: 2, pointerType: "mouse", buttons: 2});
    check(canvas.lockRequests === 1, "drag lock: the right button's press asks for the lock");
    const pageDocument = page.context.document;
    pageDocument.pointerLockElement = canvas;
    await canvas.fire("pointermove", {buttons: 2});
    check(!page.lockExits, "drag lock: holds while the button is down");
    await canvas.fire("pointerup", {buttons: 0});
    check(page.lockExits === 1 && pageDocument.pointerLockElement === null, "drag lock: ends with the button");
    await canvas.fire("pointerdown", {button: 2, pointerType: "mouse", buttons: 2});
    await canvas.fire("pointerup", {buttons: 0});
    pageDocument.pointerLockElement = canvas;
    page.documentListeners.pointerlockchange();
    check(page.lockExits === 2, "drag lock: a lock granted after a quick click ends at once");
    await canvas.fire("pointerdown", {button: 2, pointerType: "mouse", buttons: 2});
    pageDocument.pointerLockElement = canvas;
    await canvas.fire("pointermove", {buttons: 1});
    check(page.lockExits === 3, "drag lock: a release while the left button stays down ends it too");

    // The touch buttons type Space and Esc into the canvas: down with the press, up with the release, once each.
    const press = {preventDefault() {}};
    await page.elements["key-space"].fire("pointerdown", press);
    await page.elements["key-space"].fire("pointerdown", press);
    check(canvas.dispatched.length === 1 && canvas.dispatched[0].type === "keydown" && canvas.dispatched[0].code === "Space" &&
            canvas.dispatched[0].bubbles, "touch keys: the Space button's press is one keydown of Space on the canvas");
    await page.elements["key-space"].fire("pointerup");
    await page.elements["key-space"].fire("pointerleave");
    check(canvas.dispatched.length === 2 && canvas.dispatched[1].type === "keyup" && canvas.dispatched[1].code === "Space",
        "touch keys: the release is one keyup, and leaving the button after it adds none");
    await page.elements["key-escape"].fire("pointerdown", press);
    await page.elements["key-escape"].fire("pointercancel");
    check(canvas.dispatched.length === 4 && canvas.dispatched[2].code === "Escape" && canvas.dispatched[3].type === "keyup",
        "touch keys: Esc likewise, and a cancelled press does not leave the key down");

    worker.onmessage({data: {type: "done", files: 42}});
    check(worker.terminated, "folder: the worker ends after done");
    check(page.scripts.length === 1 && page.scripts[0].src === "OpenBeach.js", "folder: done adds the game script");
    check(page.visible().join() === "loading" && page.elements["loading-text"].textContent === "Ładowanie plików gry...",
        "folder: the loading line shows while the game copies its files");
    loaded.push(page.scripts[0].src);

    // Once the game script is in, no import starts: the worker would replace the pack the game reads.
    await page.elements["import-iso"].fire("change", {target: {files: [{name: "disc.iso", size: 9}]}});
    check(page.workers.length === 1, "after done: a second import does not start");

    // The pre-js's callbacks drive the layers; the click starts main.
    let started = 0;
    page.context.Module.startGame = () => started++;
    let ini = "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=1\r\n";
    let packCodes = ["en", "fr", "pl"];
    page.context.Module.packLanguages = async () => packCodes;
    page.context.Module.readSettings = () => ini;
    page.context.Module.writeSettings = async (text) => {
        ini = text;
    };

    // The first start picks a resolution for the tab; an INI that names one keeps it.
    vm.runInContext("importedNow = false", page.context);
    const firstResolution = async (width, height, ratio, text) => {
        page.context.innerWidth = width;
        page.context.innerHeight = height;
        page.context.devicePixelRatio = ratio;
        ini = text;
        page.context.Module.onGameFilesReady(10);
        await flush();
        await flush();
        return /ResWidth=(\d+)\r\nResHeight=(\d+)/.exec(ini).slice(1).join("x");
    };
    const iniBase = ini;
    check(await firstResolution(1920, 950, 1, iniBase) === "1280x720", "first resolution: Full HD tab");
    check(ini.includes("[Language]\r\nLanguage=1\r\n[Video]\r\n"), "first resolution: the other settings stay");
    check(await firstResolution(1896, 874, 1, iniBase) === "1280x720", "first resolution: a Full HD tab under bookmarks");
    check(await firstResolution(1366, 650, 1, iniBase) === "800x600", "first resolution: a small tab stays at 800x600");
    check(await firstResolution(1300, 1000, 1, iniBase) === "1024x768", "first resolution: a 4:3 tab keeps 4:3");
    check(await firstResolution(2560, 1300, 1.5, iniBase) === "1920x1080", "first resolution: a scaled 4K screen");
    check(await firstResolution(1440, 800, 2, iniBase) === "1280x720", "first resolution: a high-density laptop");
    const chosen = iniBase + "[Video]\r\nResWidth=1600\r\nResHeight=900\r\n";
    check(await firstResolution(1920, 950, 1, chosen) === "1600x900" && ini === chosen, "first resolution: the player's stays");
    page.context.innerWidth = 0;
    page.context.innerHeight = 0;
    ini = iniBase;

    page.context.Module.onGameFilesReady(10);
    await flush();
    const startSelect = page.elements["start-language"];
    check(page.visible().join() === "start", "game files ready: the ready layer shows");
    check(!page.elements["start-language-row"].hidden && startSelect.children.map((o) => o.value).join() === "en,fr,pl" &&
        startSelect.value === "fr" && !startSelect.disabled, "start language: the pack's tables are listed, the INI's language is shown");
    vm.runInContext("importedNow = true", page.context);
    page.context.Module.onGameFilesReady(10);
    await flush();
    check(startSelect.value === "pl" && ini.includes("Language=5"), "start language: right after an import the game follows the page");
    ini = iniBase;
    page.context.Module.onGameFilesReady(10);
    await flush();
    check(startSelect.value === "fr", "start language: later the INI's choice stays");
    startSelect.value = "pl";
    await startSelect.fire("change");
    check(ini === "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=5\r\n", "start language: a change rewrites the INI");
    packCodes = ["pl"];
    page.context.Module.onGameFilesReady(10);
    await flush();
    check(!page.elements["start-language-row"].hidden && startSelect.disabled && startSelect.value === "pl",
        "start language: a pack with one table shows its row, disabled");
    await page.elements["start-button"].fire("click");
    check(started === 1 && page.visible().length === 0 && page.elements.canvas.focused && page.body.classList.contains("playing") &&
        !page.elements["game-bar"].hidden, "click: main starts, the layers hide, the frame takes the tab and the canvas has focus");
    await page.elements.fullscreen.fire("click");
    check(page.fullscreenRequests === 1 && page.elements.canvas.fullscreenRequests === undefined,
        "click: the bar's button asks for fullscreen of the page, not of the canvas (SDL would size the canvas to the screen)");
    page.context.Module.onGameEnded();
    check(page.visible().join() === "ended" && page.elements.canvas.hidden && !page.body.classList.contains("playing"),
        "game ended: the ended layer replaces the canvas");

    // A saves file imports through the pre-js and reloads; a bad one shows the error and stays.
    page = await loadPage({opfs: packTree()});
    let importedText = null;
    page.context.Module.importSaves = async (text) => {
        if (text === "bad") throw new Error("not an OpenBeach saves file");
        importedText = text;
        return 2;
    };
    const savesFile = (text) => ({files: [{async text() {
        return text;
    }}]});
    await page.elements["import-saves"].fire("change", {target: savesFile("{}")});
    check(importedText === "{}" && page.reloads === 1, "saves: an exported file is imported and the page reloads");
    await page.elements["import-saves"].fire("change", {target: savesFile("bad")});
    check(page.reloads === 1 && page.elements["start-status"].textContent.includes("not an OpenBeach"),
        "saves: a wrong file shows the error without a reload");

    // A stored pack starts the game at once, without the picker.
    check(page.scripts.length === 1 && page.visible().join() === "loading", "pack stored: the game script is added at once");
    check(page.workers.length === 0, "pack stored: no import runs");

    // ?import shows the picker even with a pack; a disc image goes to the worker as the image.
    page = await loadPage({opfs: packTree(), search: "?import"});
    check(page.visible().join() === "import" && page.scripts.length === 0, "?import: the picker shows despite the pack");
    const image = {name: "disc.iso", size: 9};
    await page.elements["import-iso"].fire("change", {target: {files: [image]}});
    worker = page.workers[0];
    check(worker && worker.sent[0].source === "iso" && worker.sent[0].image === image, "iso: the worker gets the image");

    // A finished import drops ?import, so that a reload starts the game and not the importer.
    const reimport = await loadPage({opfs: packTree(), search: "?import"});
    await reimport.elements["import-iso"].fire("change", {target: {files: [image]}});
    reimport.workers[0].onmessage({data: {type: "done", files: 42}});
    check(reimport.address === "/", "?import: a finished import leaves the address clean");

    // A failed import keeps the picker, shows the message and allows another try.
    worker.onmessage({data: {type: "error", message: "unknown game build"}});
    check(worker.terminated && page.scripts.length === 0, "error: the worker ends and the game script stays out");
    check(page.elements["drop-status"].textContent.includes("unknown game build"), "error: the message is shown");
    check(!page.elements["import-folder"].disabled && !page.elements.drop.hidden, "error: the drop zone comes back");
    await page.elements["import-folder"].fire("change", {target: {files: []}});
    check(page.workers.length === 1, "an empty folder pick starts nothing");
    worker = null;

    // A worker that cannot load (missing importer files) is an error too.
    await page.elements["import-folder"].fire("change", {target: {files: folderFiles}});
    page.workers[1].onerror({});
    check(page.workers[1].terminated && page.elements["drop-status"].textContent.includes("Import się nie udał"),
        "worker error: reported as a failed import");

    // The game files error stays on the loading layer with the message.
    page.context.Module.onGameFilesError(new Error("the game is already open in another tab"));
    check(page.visible().join() === "loading" && page.elements["loading-text"].textContent.includes("another tab"),
        "game files error: the message replaces the loading line");

    // No OPFS: the picker shows, disabled, with the reason.
    page = await loadPage({opfs: null});
    check(page.visible().join() === "import" && page.elements["import-folder"].disabled && page.elements["drop-status"].textContent.includes("OPFS"),
        "no OPFS: the picker is disabled with the reason");

    // A dropped .iso goes as the image; a dropped folder goes file by file with its paths; anything else is refused.
    page = await loadPage();
    const fileEntry = (name, fullPath, file) => ({isFile: true, isDirectory: false, name, fullPath, file: (done) => done(file)});
    const dropItems = (entries) => ({preventDefault() {}, dataTransfer: {items: entries.map((entry) => ({webkitGetAsEntry: () => entry}))}});
    await page.elements.drop.fire("drop", dropItems([fileEntry("note.txt", "/note.txt", {name: "note.txt"})]));
    check(page.workers.length === 0 && page.elements["drop-status"].textContent === "Upuść plik .iso albo folder gry.",
        "drop: another file is refused with a line");
    const dropped = {name: "DISC.ISO", size: 9};
    await page.elements.drop.fire("drop", dropItems([fileEntry("DISC.ISO", "/DISC.ISO", dropped)]));
    check(page.workers[0].sent[0].source === "iso" && page.workers[0].sent[0].image === dropped, "drop: a disc image goes as the image");
    page = await loadPage();
    const inner = {name: "BeachLife.exe", size: 3};
    let batches = [[fileEntry("BeachLife.exe", "/Beach Life/BeachLife.exe", inner)], []];
    const folder = {isFile: false, isDirectory: true, name: "Beach Life", fullPath: "/Beach Life",
        createReader: () => ({readEntries: (done) => done(batches.shift())})};
    await page.elements.drop.fire("drop", dropItems([folder]));
    check(page.workers[0].sent[0].source === "files" && page.workers[0].sent[0].files[0] === inner &&
        page.workers[0].sent[0].paths[0] === "Beach Life/BeachLife.exe", "drop: a folder goes with each file's path");
    await page.elements.drop.fire("dragover", {preventDefault() {}});
    check(!page.elements.drop.classList.contains("over"), "drop: a running import does not light the zone");
    batches = [];

    // The worker's language handling (importer/web/language_ini.js): the copy's INI names it, the game's INI gets it.
    const {browserLanguage, languageOfIni, withLanguage} = require(path.join(__dirname, "..", "importer", "web", "language_ini.js"));
    check(browserLanguage("pl-PL") === "pl" && browserLanguage("") === "" && browserLanguage(undefined) === "",
        "language: a browser tag gives its code");
    check(languageOfIni("[Sound]\r\nVolume=3\r\n[Language]\r\nLanguage=1\r\n") === "fr", "language: read from the copy's INI");
    check(languageOfIni("[Sound]\nLanguage=1\n") === null && languageOfIni("[Language]\nLanguage=9\n") === null &&
        languageOfIni("") === null, "language: no value, other sections and out-of-range indexes give none");
    check(withLanguage("", "pl") === "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=5\r\n",
        "language: an empty INI gets the section and the version key the game requires");
    check(withLanguage("[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=0\r\n[Sound]\r\nVolume=3\r\n", "de") ===
        "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=3\r\n[Sound]\r\nVolume=3\r\n", "language: the value is replaced, other settings stay");
    check(withLanguage("[Beach Life Options]\r\nVersion=1\r\n[Language]\r\n[Sound]\r\nVolume=3\r\n", "it") === "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=2\r\n[Sound]\r\nVolume=3\r\n",
        "language: a section without the key gets it");
    check(withLanguage("[Sound]\r\nVolume=3\r\n", "es") ===
        "[Beach Life Options]\r\nVersion=1\r\n[Sound]\r\nVolume=3\r\n[Language]\r\nLanguage=4\r\n",
        "language: a missing section is appended");

    check(withLanguage("[Beach Life Options]\r\n[Language]\r\nLanguage=0\r\n", "fr") ===
        "[Beach Life Options]\r\nVersion=1\r\n[Language]\r\nLanguage=1\r\n", "language: a main section without Version gets it");

    if (siteDir) {
        // The page, its script, what it loads and the wasm beside each Emscripten script.
        const wanted = new Set(["index.html", "site.js", ...loaded, "OpenBeach.wasm", "obl-import-web.js",
            "obl-import-web.wasm"]);
        const html = fs.readFileSync(path.join(siteDir, "index.html"), "utf8");
        for (const match of html.matchAll(/<(?:script|img)[^>]*\ssrc="([^"]+)"/g)) wanted.add(match[1]);
        const worker = fs.readFileSync(path.join(siteDir, "import_worker.js"), "utf8");
        for (const match of worker.matchAll(/importScripts\(([^)]+)\)/g))
            for (const name of match[1].matchAll(/"([^"]+)"/g)) wanted.add(name[1]);
        // What crawlers and browsers ask for without being told, and every local file the head links to.
        for (const name of ["robots.txt", "sitemap.xml", "favicon.ico"]) wanted.add(name);
        for (const match of html.matchAll(/<link[^>]*\shref="([^"]+)"/g))
            if (!/^https?:/.test(match[1])) wanted.add(match[1]);
        const image = html.match(/property="og:image" content="https:\/\/openbeach\.app\/([^"]+)"/);
        check(image !== null, "site: the social preview image is an absolute URL on the site");
        if (image) wanted.add(image[1]);
        const description = html.match(/<meta name="description" content="([^"]+)"/);
        check(description !== null && description[1].length <= 160, "site: the description fits a search result");
        check(html.includes('<link rel="canonical" href="https://openbeach.app/">'), "site: the page names its canonical address");
        const siteName = html.match(/<script type="application\/ld\+json">(.+)<\/script>/);
        check(siteName !== null && JSON.parse(siteName[1]).name === "OpenBeach", "site: the structured data carries the site name");
        for (const name of wanted) check(fs.existsSync(path.join(siteDir, name)), `site: ${name} is in ${siteDir}`);
    }

    console.log(failures === 0 ? "site checks passed" : `${failures} site checks failed`);
    process.exit(failures === 0 ? 0 : 1);
}

main().catch((error) => {
    console.log(`FAIL ${error.stack}`);
    process.exit(1);
});
