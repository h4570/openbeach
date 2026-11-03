// The site's page logic (docs/architecture/browser-build.md, "Site"), loaded by index.html. The game script
// (OpenBeach.js, with browser_files.js as its pre-js) is added only once OPFS holds packs/base: its preRun takes the
// tab's Web Lock and copies the pack at once, so loading it without a pack would end in an error and keep the lock. Without
// a pack (or with ?import in the URL) the page shows the importer's picker, runs import_worker.js on the picked folder or
// disc image and then adds the game script. An import never starts once the game script is in: the worker replaces
// packs/base, which the game reads. web/site_check.cjs runs this file under Node against a fake DOM, OPFS and Worker.
"use strict";

const kGameScript = "OpenBeach.js";
const kImportWorker = "import_worker.js";
const kLanguageNames = {en: "English", fr: "Français", it: "Italiano", de: "Deutsch", es: "Español", pl: "Polski"};
const kLayers = ["import", "loading", "start", "ended"];

const element = (id) => document.getElementById(id);

// `null` is the running game: the page gives the frame the tab and shows the canvas.
function show(id) {
    for (const layer of kLayers) element(layer).hidden = layer !== id;
    const playing = id === null;
    document.body.classList.toggle("playing", playing);
    element("game-bar").hidden = !playing;
}

// ---- The page's language ----------------------------------------------------------------------------------------------

const kPageLanguageKey = "openbeach-page-language";
let pageLanguage = "en";

// A stored choice, else the browser's first language the page has, else English. Storage can be blocked: the page then only forgets.
function readStoredLanguage() {
    try {
        return localStorage.getItem(kPageLanguageKey);
    } catch (error) {
        return null;
    }
}

function choosePageLanguage() {
    const stored = readStoredLanguage();
    if (kLanguages.includes(stored)) return stored;
    const wanted = navigator.languages && navigator.languages.length > 0 ? navigator.languages : [navigator.language];
    return wanted.map(browserLanguage).find((code) => kLanguages.includes(code)) || "en";
}

function text(key, slots = {}) {
    return kSiteTexts[pageLanguage][key].replace(/\{(\w+)\}/g, (slot, name) => slots[name]);
}

// Dynamic lines remember how they were made, so a language change retells them.
const shownLines = new Map();
function setLine(id, key, slots) {
    shownLines.set(id, {key, slots});
    element(id).textContent = text(key, slots);
}

function clearLine(id) {
    shownLines.delete(id);
    element(id).textContent = "";
}

// Texts mark emphasis as **bold**. Nodes are built, never parsed as HTML, so a translation cannot inject markup.
function setRichText(node, value) {
    if (!value.includes("**")) {
        node.textContent = value;
        return;
    }
    node.replaceChildren(...value.split("**").map((part, i) => {
        if (i % 2 === 0) return part;
        const strong = document.createElement("strong");
        strong.textContent = part;
        return strong;
    }));
}

function applyPageLanguage(code) {
    pageLanguage = code;
    document.documentElement.lang = code;
    for (const node of document.querySelectorAll("[data-t]")) setRichText(node, text(node.dataset.t));
    for (const [id, {key, slots}] of shownLines) element(id).textContent = text(key, slots);
    if (lastProgress) showProgress(lastProgress);
}

function fillPageLanguages() {
    const select = element("site-language");
    select.replaceChildren(...kLanguages.map((code) => {
        const option = document.createElement("option");
        option.value = code;
        option.textContent = kLanguageNames[code];
        return option;
    }));
    select.value = pageLanguage;
    select.addEventListener("change", (event) => {
        applyPageLanguage(event.target.value);
        try {
            localStorage.setItem(kPageLanguageKey, event.target.value);
        } catch (error) {
            // Blocked storage: the choice lasts until the page closes.
        }
    });
}

async function hasPack() {
    let dir = await navigator.storage.getDirectory();
    try {
        for (const part of ["packs", "base"]) dir = await dir.getDirectoryHandle(part);
        return true;
    } catch (error) {
        if (error && error.name === "NotFoundError") return false;
        throw error;
    }
}

let gameAdded = false;

function addGame() {
    if (gameAdded) return;
    gameAdded = true;
    setLine("loading-text", "loadingFiles");
    show("loading");
    const script = document.createElement("script");
    script.src = kGameScript;
    script.async = true;
    document.body.appendChild(script);
}

// The share of a whole import per step (importer/src/import.cpp, then the worker's "storing"), measured roughly on a full copy.
const kStepWeights = {sounds: 12, images: 8, media: 8, maps: 10, sprites: 40, storing: 12};
const stepWeight = (step) => kStepWeights[step] || 1;
const kTotalWeight = kStepNames.reduce((sum, step) => sum + stepWeight(step), 0);

let lastProgress = null;

// The main bar counts the whole import; the second bar shows only inside a long step (done/total).
function showProgress(progress) {
    const {step, done, total} = progress;
    lastProgress = progress;
    const index = kStepNames.indexOf(step);
    const fraction = total > 0 ? done / total : 0;
    const before = kStepNames.slice(0, Math.max(index, 0)).reduce((sum, name) => sum + stepWeight(name), 0);
    const share = (before + (index >= 0 ? stepWeight(step) * fraction : 0)) / kTotalWeight;
    const name = index >= 0 ? kSiteTexts[pageLanguage].steps[index] : step;
    element("import-main").max = 1;
    element("import-main").value = share;
    element("import-percent").textContent = Math.floor(share * 100) + "%";
    element("import-step").textContent = text("stepOf", {step: index + 1, total: kStepNames.length, name});
    element("import-sub").hidden = !(total > 0);
    element("import-status").textContent = name;
    element("import-count").textContent = total > 0 ? done + " / " + total : "";
    element("import-progress").max = total > 0 ? total : 1;
    element("import-progress").value = total > 0 ? done : 0;
}

// Diagnostic for a stalled import: the worker's time per report against the page's time on receipt, with
// visibility and focus. Read it from the console as `oblImportTrace` (also logged with console.debug).
const importTrace = (globalThis.oblImportTrace = []);
function traceProgress({step, done, total, at}) {
    const entry = {
        step, done, total,
        worker: at,
        page: Date.now(),
        visibility: document.visibilityState,
        focus: document.hasFocus ? document.hasFocus() : null,
    };
    importTrace.push(entry);
    console.debug("import progress", JSON.stringify(entry));
}

let importRunning = false;

// The bars replace the drop zone while an import runs, and an error brings it back with the message.
function setImporting(running) {
    importRunning = running;
    element("drop").hidden = running;
    element("import-bars").hidden = !running;
    element("import-iso").disabled = running;
    element("import-folder").disabled = running;
    if (!running) lastProgress = null;
}

// Asks the browser to keep the converted pack from eviction; a refusal is harmless, the pack is only re-importable then.
function requestPersistence() {
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {});
}

function runImport(request) {
    if (gameAdded || importRunning) return;
    setImporting(true);
    clearLine("drop-status");
    requestPersistence();
    element("import-step").textContent = text("starting");
    const worker = new Worker(kImportWorker);
    const fail = (message) => {
        worker.terminate();
        setImporting(false);
        setLine("drop-status", "failed", {message});
    };
    worker.onmessage = (event) => {
        const message = event.data;
        if (message.type === "progress") {
            traceProgress(message);
            showProgress(message);
        } else if (message.type === "done") {
            worker.terminate();
            importedNow = true;
            // ?import has done its work: left in the address, every later reload would open the importer again.
            history.replaceState(null, "", location.pathname);
            addGame();
        } else if (message.type === "error") {
            fail(message.message);
        }
    };
    worker.onerror = (event) => fail(event.message || "the import worker did not start");
    worker.postMessage(request);
}

let importedNow = false;

element("import-folder").addEventListener("change", (event) => {
    const files = Array.from(event.target.files || []);
    if (files.length > 0) runImport({source: "files", files});
});

element("import-iso").addEventListener("change", (event) => {
    const image = event.target.files && event.target.files[0];
    if (image) runImport({source: "iso", image});
});

// ---- Drag and drop ------------------------------------------------------------------------------------------------------

const entryFile = (entry) => new Promise((resolve, reject) => entry.file(resolve, reject));

// readEntries gives a directory in batches; an empty batch ends it.
async function directoryEntries(directory) {
    const reader = directory.createReader();
    const entries = [];
    for (;;) {
        const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
        if (batch.length === 0) return entries;
        entries.push(...batch);
    }
}

// Every file under a dropped folder, with the path a picked folder would give it.
async function collectFiles(entry, files, paths) {
    if (entry.isDirectory) {
        for (const child of await directoryEntries(entry)) await collectFiles(child, files, paths);
    } else {
        files.push(await entryFile(entry));
        paths.push(entry.fullPath.replace(/^\/+/, ""));
    }
}

async function runDrop(entries) {
    const folder = entries.find((entry) => entry.isDirectory);
    const image = entries.find((entry) => entry.isFile && /\.iso$/i.test(entry.name));
    if (image) {
        runImport({source: "iso", image: await entryFile(image)});
    } else if (folder) {
        const files = [];
        const paths = [];
        await collectFiles(folder, files, paths);
        runImport({source: "files", files, paths});
    } else {
        setLine("drop-status", "dropWrong");
    }
}

const dropZone = element("drop");
for (const type of ["dragenter", "dragover"]) {
    dropZone.addEventListener(type, (event) => {
        event.preventDefault();
        if (!importRunning) dropZone.classList.add("over");
    });
}
dropZone.addEventListener("dragleave", () => dropZone.classList.remove("over"));
dropZone.addEventListener("drop", (event) => {
    event.preventDefault();
    dropZone.classList.remove("over");
    if (importRunning) return;
    // The list is valid only during this event, so the entries are taken before anything waits.
    const entries = Array.from(event.dataTransfer.items || []).map((item) => item.webkitGetAsEntry()).filter(Boolean);
    return runDrop(entries).catch((error) => setLine("drop-status", "failed", {message: error.message}));
});

// Module.importSaves (browser_files.js) writes to OPFS; the game copied its files already, so a reload starts it on them.
element("import-saves").addEventListener("change", async (event) => {
    const file = event.target.files && event.target.files[0];
    if (!file) return;
    try {
        const count = await Module.importSaves(await file.text());
        setLine("start-status", "savesImported", {count});
        location.reload();
    } catch (error) {
        setLine("start-status", "cannotImportSaves", {message: error.message});
    }
});

// The canvas keeps the aspect of its pixel size (SDL resizes it with the game's resolution); index.html's CSS fits the
// largest box of that aspect into the tab, so a 16:9 game is not stretched into 4:3 and the pointer keeps hitting where
// it points (SDL maps the mouse through the box).
function fitCanvas() {
    const canvas = element("canvas");
    if (canvas.width > 0 && canvas.height > 0) canvas.style.setProperty("--aspect", String(canvas.width / canvas.height));
}

fitCanvas();
if (typeof MutationObserver !== "undefined") {
    new MutationObserver(fitCanvas).observe(element("canvas"), {attributes: true, attributeFilter: ["width", "height"]});
}

// A right drag locks the pointer, so the map keeps moving past the tab's edge and the release stays in the page, where the
// canvas swallows the context menu; outside the tab it would open the menu of whatever is under the pointer. A browser
// grants the lock only inside the press's own event and the game starts a drag a frame later, so the page asks for it.
function lockPointerForRightDrag() {
    const canvas = element("canvas");
    let held = false;
    canvas.addEventListener("pointerdown", (event) => {
        if (event.button !== 2 || event.pointerType !== "mouse") return;
        held = true;
        const lock = canvas.requestPointerLock();
        if (lock && lock.catch) lock.catch(() => {}); // refused: the game drags by the visible pointer
    });
    const release = (event) => {
        held = held && (event.buttons & 2) !== 0;
        if (!held && document.pointerLockElement === canvas) document.exitPointerLock();
    };
    canvas.addEventListener("pointerup", release);
    canvas.addEventListener("pointermove", release);
    // A quick click can end before the browser grants the lock.
    document.addEventListener("pointerlockchange", () => {
        if (!held && document.pointerLockElement === canvas) document.exitPointerLock();
    });
}

lockPointerForRightDrag();

// Space turns what is being placed and Esc leaves the mode, but a touch screen has no keyboard: the page's two buttons send the
// keys to the canvas as typed (SDL listens on the window, which the event reaches). A key is down while the button is.
function wireTouchKeys() {
    const canvas = element("canvas");
    for (const id of ["key-space", "key-escape"]) {
        const button = element(id);
        let down = false;
        const send = (type) => canvas.dispatchEvent(new KeyboardEvent(type, {
            code: button.dataset.code, key: button.dataset.key, bubbles: true, cancelable: true,
        }));
        button.addEventListener("pointerdown", (event) => {
            event.preventDefault(); // no focus, no click, no text selection
            if (!down) send("keydown");
            down = true;
        });
        const release = () => {
            if (down) send("keyup");
            down = false;
        };
        for (const type of ["pointerup", "pointercancel", "pointerleave"]) button.addEventListener(type, release);
    }
}

wireTouchKeys();

// The sizes the options list offers in the browser (src/game/ui/resolutions.cpp), smallest first. 640x480 is left out: it
// has the small bottom bar.
const kResolutions = [[800, 600], [1024, 768], [1280, 720], [1280, 960], [1600, 900], [1920, 1080]];
// The GUI has a fixed pixel size, so a resolution near the tab's own size leaves its text small.
const kMinResolutionScale = 1.15;

// The size that fills the tab best while the picture stays at least kMinResolutionScale screen pixels per game pixel and
// no smaller than the tab's own pixels. `width` and `height` are the tab's CSS pixels, which follow the system's scaling.
function resolutionForTab(width, height, pixelRatio) {
    const minScale = Math.max(1, kMinResolutionScale / pixelRatio);
    let best = kResolutions[0];
    let bestArea = 0;
    for (const size of kResolutions) {
        const scale = Math.min(width / size[0], height / size[1]);
        const area = scale * size[0] * scale * size[1];
        if (scale >= minScale && area >= bestArea * 0.999) { // equal boxes: the larger size is sharper
            best = size;
            bestArea = area;
        }
    }
    return best;
}

// The first start picks a resolution for the tab; afterwards the INI holds the player's.
async function seedResolution() {
    if (!(globalThis.innerWidth > 0 && globalThis.innerHeight > 0)) return;
    const size = resolutionForTab(globalThis.innerWidth, globalThis.innerHeight, globalThis.devicePixelRatio || 1);
    const settings = Module.readSettings();
    const seeded = withDefaultResolution(settings, size[0], size[1]);
    if (seeded !== settings) await Module.writeSettings(seeded);
}

// The start layer's language select: the pack's text tables, showing the language BeachLife.ini names. A change rewrites the
// INI before main reads it (the click starts main), so texts and layouts follow it. Without the pack's tables it stays hidden.
async function fillStartLanguages() {
    const select = element("start-language");
    const codes = (await Module.packLanguages()).filter((code) => kLanguages.includes(code));
    element("start-language-row").hidden = codes.length === 0;
    select.disabled = codes.length < 2; // shown anyway: the note says why a one-language edition has no choice
    select.replaceChildren(...codes.map((code) => {
        const option = document.createElement("option");
        option.value = code;
        option.textContent = kLanguageNames[code];
        return option;
    }));
    // Right after an import the game follows the page when the copy has its language; later the player's choice stays.
    if (importedNow && codes.includes(pageLanguage)) await Module.writeSettings(withLanguage(Module.readSettings(), pageLanguage));
    importedNow = false;
    const current = languageOfIni(Module.readSettings());
    select.value = codes.includes(current) ? current : codes.includes(pageLanguage) ? pageLanguage : codes[0] || "";
}

element("start-language").addEventListener("change", async (event) => {
    try {
        await Module.writeSettings(withLanguage(Module.readSettings(), event.target.value));
    } catch (error) {
        setLine("start-status", "cannotChange", {message: error.message});
    }
});

element("start-button").addEventListener("click", () => {
    show(null);
    element("canvas").focus();
    Module.startGame(); // main runs inside this click, so SDL's AudioContext starts unsuspended
});

// The page goes fullscreen, not the canvas: SDL takes a fullscreen element that is its canvas for a fullscreen window and
// sizes the canvas to the screen, so the game would be resampled again and keep the wrong size after leaving. The CSS fits
// the canvas into the bigger tab as into any other, and the touch keys and the bar stay beside it.
element("fullscreen").addEventListener("click", () => {
    const request = document.documentElement.requestFullscreen();
    if (request && request.catch) request.catch(() => {}); // refused: the game stays in its frame
});

var Module = {
    canvas: element("canvas"),
    onGameFilesReady() {
        // The layer shows even if the language list cannot be read: the game then starts in the INI's language.
        fillStartLanguages().catch((error) => console.error("start language: " + error.message))
            .then(seedResolution).catch((error) => console.error("start resolution: " + error.message))
            .then(() => show("start"));
    },
    onGameFilesError(error) {
        setLine("loading-text", "cannotLoad", {message: error.message});
        show("loading");
    },
    onFlushError(error) {
        console.error("saves not stored: " + error.message);
    },
    onGameEnded() {
        element("canvas").hidden = true;
        show("ended");
    },
};

async function startSite() {
    if (!navigator.storage || !navigator.storage.getDirectory) {
        setLine("drop-status", "noOpfs");
        element("import-iso").disabled = true;
        element("import-folder").disabled = true;
        importRunning = true; // nothing can be stored, so a drop starts nothing either
        show("import");
        return;
    }
    const forceImport = new URLSearchParams(location.search).has("import");
    if (!forceImport && (await hasPack())) {
        addGame();
    } else {
        show("import");
    }
}

// The check awaits this.
applyPageLanguage(choosePageLanguage());
fillPageLanguages();
var siteStarted = startSite().catch((error) => {
    setLine("drop-status", "storageError", {message: error.message});
    show("import");
});
