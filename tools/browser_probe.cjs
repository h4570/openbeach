#!/usr/bin/env node
// Runs the browser game in a headless Chromium (Edge or Chrome) on this PC and records what a tab shows: the page's
// console (the game's log), canvas screenshots at fixed times, and the AudioContext's state. A local diagnosis tool for
// the browser build (docs/architecture/browser-build.md, "Local probe"); CI has no browser and no game data.
//
//   node tools/browser_probe.cjs --site <dir> --pack <packs/base dir> [--out run/tmp/probe] [--seconds 40]
//                                [--browser <path>] [--script <steps.json>] [--port 8739] [--profile <dir>]
//                                [--game-script <autotest script>] [--prepare <js>] [--window-size 1024,768] [--screen 1920x1080]
//
// <dir> holds index.html, site.js and OpenBeach.js/.wasm (tools/web_site.sh, or the browser build's bin/). The pack is
// served next to the site and copied into the profile's OPFS once (as the importer would write it), so later runs
// start at "Click to start". The probe clicks it like a player (a trusted CDP mouse event, so audio may start), then
// follows the steps: {"at": seconds, "click": [x, y], "hold": ms} (canvas pixels, left button; the press lasts "hold" ms, 400 by default), {"at": s, "tap": [x, y]} (a touch tap), {"at": s, "drag": [x, y, dx, dy]}
// (right button), {"at": s, "shot": "name"}, {"at": s, "eval": "js"}, {"at": s, "memory": "label"}. Without --script it
// takes a screenshot every 2 s. --game-script runs a game test script (tests/*.txt) in the tab as --script does natively,
// e.g. one that boots a scenario. Memory samples (one after the file copy, one per "memory" step) go to console.log and
// memory.json: the wasm memory, MEMFS /game's bytes and V8's heap with its ArrayBuffer backing stores. --prepare runs
// a page expression (or a .js file of one) at the start layer, before the click (a settings change a player makes there); its result is logged
// as "PROBE prepare". "eval" steps log theirs as "PROBE eval", e.g. a language check before and after a reload. --screen
// sets the screen a headless browser reports (else 800x600, and the game clamps its window to that "monitor").
"use strict";

const fs = require("fs");
const http = require("http");
const path = require("path");
const {spawn} = require("child_process");

function parseArgs(argv) {
    const args = {out: "run/tmp/probe", seconds: 40, port: 8739, debugPort: 29339};
    for (let i = 0; i < argv.length; i += 2) args[argv[i].replace(/^--/, "")] = argv[i + 1];
    if (!args.site || !args.pack) {
        console.error("usage: browser_probe.cjs --site <dir> --pack <packs/base dir> [--out dir] [--seconds n] [--script steps.json]");
        process.exit(2);
    }
    args.seconds = Number(args.seconds);
    return args;
}

function findBrowser(explicit) {
    const candidates = [explicit, "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
        "C:/Program Files/Microsoft/Edge/Application/msedge.exe", "C:/Program Files/Google/Chrome/Application/chrome.exe",
        "/usr/bin/chromium", "/usr/bin/google-chrome"];
    return candidates.find((file) => file && fs.existsSync(file));
}

function listFiles(root, relative = "") {
    let files = [];
    for (const entry of fs.readdirSync(path.join(root, relative), {withFileTypes: true})) {
        const name = relative ? relative + "/" + entry.name : entry.name;
        files = files.concat(entry.isDirectory() ? listFiles(root, name) : [name]);
    }
    return files;
}

const kTypes = {".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json"};

// A fixed port keeps the origin, and with it the profile's OPFS copy of the pack, from run to run.
function serve(site, pack, port) {
    const packFiles = listFiles(pack);
    const server = http.createServer((request, response) => {
        const url = decodeURIComponent(request.url.split("?")[0]);
        if (url === "/probe-pack-list") {
            response.writeHead(200, {"Content-Type": "application/json"});
            response.end(JSON.stringify(packFiles));
            return;
        }
        const file = url.startsWith("/probe-pack/") ? path.join(pack, url.slice("/probe-pack/".length))
                                                    : path.join(site, url === "/" ? "index.html" : url);
        fs.readFile(file, (error, data) => {
            if (error) {
                response.writeHead(404);
                response.end();
                return;
            }
            response.writeHead(200, {"Content-Type": kTypes[path.extname(file)] || "application/octet-stream"});
            response.end(data);
        });
    });
    return new Promise((resolve) => server.listen(port, "127.0.0.1", () => resolve(server)));
}

// A minimal CDP client over Node's WebSocket.
class Cdp {
    constructor(url) {
        this.socket = new WebSocket(url);
        this.next = 1;
        this.pending = new Map();
        this.listeners = [];
        this.socket.onmessage = (event) => {
            const message = JSON.parse(event.data);
            if (message.id && this.pending.has(message.id)) {
                const {resolve, reject} = this.pending.get(message.id);
                this.pending.delete(message.id);
                message.error ? reject(new Error(message.error.message)) : resolve(message.result);
            } else if (message.method) {
                for (const listener of this.listeners) listener(message);
            }
        };
    }
    open() {
        return new Promise((resolve, reject) => {
            this.socket.onopen = resolve;
            this.socket.onerror = reject;
        });
    }
    send(method, params = {}) {
        const id = this.next++;
        this.socket.send(JSON.stringify({id, method, params}));
        return new Promise((resolve, reject) => this.pending.set(id, {resolve, reject}));
    }
    on(listener) {
        this.listeners.push(listener);
    }
}

// Run in the page: what the tab holds. MEMFS keeps file contents in JS typed arrays, outside the wasm memory.
const kMemory = `(() => {
    const wasm = typeof wasmMemory !== "undefined" ? wasmMemory.buffer.byteLength : -1;
    let memfs = 0, files = 0;
    const walk = (path) => {
        for (const name of FS.readdir(path)) {
            if (name === "." || name === "..") continue;
            const full = path + "/" + name, stat = FS.stat(full);
            if (FS.isDir(stat.mode)) walk(full); else { memfs += stat.size; ++files; }
        }
    };
    try { walk("/game"); } catch (e) {}
    const heap = performance.memory || {};
    return {wasm, memfs, files, jsUsed: heap.usedJSHeapSize || -1};
})()`;

// The game script reaches the tab's MEMFS through a preRun added to the page's Module before the runtime reads it.
function gameScriptInjection(text) {
    return `(() => {
    let module;
    Object.defineProperty(window, "Module", {configurable: true, get: () => module, set: (value) => {
        module = value;
        if (value.oblProbe) return; // the runtime assigns the same object again
        value.oblProbe = true;
        value.arguments = ["--script", "/game/probe-script.txt"].concat(value.arguments || []);
        value.preRun = [() => { FS.mkdirTree("/game"); FS.writeFile("/game/probe-script.txt", ${JSON.stringify(text)}); }]
            .concat(value.preRun || []);
    }});
})()`;
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// The probe's own tab: the page on the probe's origin (another browser or an extension page may answer as well).
async function pageTarget(debugPort, origin) {
    for (let attempt = 0; attempt < 100; ++attempt) {
        try {
            const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json();
            const page = targets.find((target) => target.type === "page" && target.url.startsWith(origin));
            if (page) return page.webSocketDebuggerUrl;
        } catch (error) {
            // the browser is still starting
        }
        await sleep(200);
    }
    throw new Error("no page target");
}

// Copies the served pack into OPFS packs/base (in the page, so it is the profile's storage).
const kFillOpfs = `(async () => {
    const root = await navigator.storage.getDirectory();
    try { await (await root.getDirectoryHandle("packs")).getDirectoryHandle("base"); return "present"; } catch (e) {}
    const files = await (await fetch("/probe-pack-list")).json();
    let copied = 0;
    for (const file of files) {
        let dir = await (await root.getDirectoryHandle("packs", {create: true})).getDirectoryHandle("base", {create: true});
        const parts = file.split("/");
        for (const part of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(part, {create: true});
        const writable = await (await dir.getFileHandle(parts[parts.length - 1], {create: true})).createWritable();
        await writable.write(await (await fetch("/probe-pack/" + file)).arrayBuffer());
        await writable.close();
        ++copied;
    }
    return "copied " + copied;
})().catch((error) => "failed: " + error.name + ": " + error.message)`;

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const browser = findBrowser(args.browser);
    if (!browser) throw new Error("no Edge/Chrome found; pass --browser");
    fs.mkdirSync(args.out, {recursive: true});
    const server = await serve(path.resolve(args.site), path.resolve(args.pack), Number(args.port));
    const origin = `http://127.0.0.1:${server.address().port}`;
    const profile = path.resolve(args.profile || path.join(args.out, "..", "probe-profile"));
    const child = spawn(browser, ["--headless=new", `--remote-debugging-port=${args.debugPort}`, `--user-data-dir=${profile}`,
        "--no-first-run", "--disable-extensions", "--enable-unsafe-swiftshader", `--window-size=${args["window-size"] || "1024,768"}`,
        ...(args.screen ? [`--screen-info={${args.screen}}`] : []), origin + "/?probe"], {stdio: "ignore"});
    const started = Date.now();
    const stamp = () => ((Date.now() - started) / 1000).toFixed(2).padStart(7);
    const log = fs.createWriteStream(path.join(args.out, "console.log"));
    try {
        const cdp = new Cdp(await pageTarget(args.debugPort, origin));
        await cdp.open();
        cdp.on((message) => {
            if (message.method === "Runtime.consoleAPICalled") {
                log.write(`${stamp()} ${message.params.args.map((a) => a.value ?? a.description ?? "").join(" ")}\n`);
            } else if (message.method === "Runtime.exceptionThrown") {
                log.write(`${stamp()} EXCEPTION ${JSON.stringify(message.params.exceptionDetails.exception?.description)}\n`);
            }
        });
        await cdp.send("Runtime.enable");
        await cdp.send("Page.enable");
        // Before the page has loaded, the tab's document may not yet be the secure origin (no navigator.storage).
        for (let i = 0; i < 100; ++i) {
            const ready = await cdp.send("Runtime.evaluate", {expression: 'document.readyState === "complete" && !!navigator.storage', returnByValue: true});
            if (ready.result.value) break;
            await sleep(200);
        }
        const fill = await cdp.send("Runtime.evaluate", {expression: kFillOpfs, awaitPromise: true, returnByValue: true});
        console.log("OPFS pack:", fill.result.value ?? JSON.stringify(fill.exceptionDetails));
        if (args["game-script"]) {
            const text = fs.readFileSync(args["game-script"], "utf8");
            await cdp.send("Page.addScriptToEvaluateOnNewDocument", {source: gameScriptInjection(text)});
        }
        await cdp.send("Page.reload");
        await sleep(1500); // the old document must not answer the start check
        const evaluate = async (expression) => {
            const reply = await cdp.send("Runtime.evaluate", {expression, returnByValue: true, awaitPromise: true});
            return reply.exceptionDetails ? "exception: " + reply.exceptionDetails.exception?.description : reply.result.value;
        };
        for (let i = 0; i < 600 && !(await evaluate(`!document.getElementById("start").hidden`)); ++i) await sleep(500);
        const samples = [];
        const sampleMemory = async (label) => {
            const page = await evaluate(kMemory);
            const v8 = await cdp.send("Runtime.getHeapUsage");
            const sample = {label, at: Number(stamp()), ...page, v8Used: v8.usedSize, v8Total: v8.totalSize,
                backingStores: v8.backingStorageSize ?? -1};
            samples.push(sample);
            fs.writeFileSync(path.join(args.out, "memory.json"), JSON.stringify(samples, null, 1));
            const mb = (n) => (n < 0 ? "?" : (n / 1048576).toFixed(0) + " MB");
            log.write(`${stamp()} PROBE memory ${label}: wasm ${mb(sample.wasm)}, memfs ${mb(sample.memfs)} in ${sample.files} files, ` +
                `js heap ${mb(sample.v8Used)}, array buffers ${mb(sample.backingStores)}
`);
        };
        await sampleMemory("files-copied");
        if (args.prepare) {
            const code = args.prepare.endsWith(".js") && fs.existsSync(args.prepare) ? fs.readFileSync(args.prepare, "utf8") : args.prepare;
            log.write(`${stamp()} PROBE prepare ${JSON.stringify(await evaluate(code))}
`);
        }
        const box = await evaluate(`(() => { const r = document.getElementById("start-button").getBoundingClientRect();
                                             return [r.x + r.width / 2, r.y + r.height / 2]; })()`);
        if (!box) throw new Error("no start button");
        const mouse = (type, x, y, button = "left") =>
                cdp.send("Input.dispatchMouseEvent", {type, x, y, button, clickCount: 1, buttons: type === "mouseReleased" ? 0 : button === "left" ? 1 : 2});
        await mouse("mousePressed", box[0], box[1]);
        await mouse("mouseReleased", box[0], box[1]);
        const clickAt = Date.now();
        log.write(`${stamp()} PROBE clicked start\n`);
        const canvasPoint = async (x, y) => evaluate(`(() => { const c = document.getElementById("canvas");
            const r = c.getBoundingClientRect(); return [r.x + ${x} * r.width / c.width, r.y + ${y} * r.height / c.height]; })()`);
        let steps = args.script ? JSON.parse(fs.readFileSync(args.script, "utf8")) : [];
        if (!args.script) for (let t = 2; t <= args.seconds; t += 2) steps.push({at: t, shot: `t${String(t).padStart(3, "0")}`});
        steps.sort((a, b) => a.at - b.at);
        for (const step of steps) {
            const wait = clickAt + step.at * 1000 - Date.now();
            if (wait > 0) await sleep(wait);
            const audio = await evaluate(`(Module.SDL3 && Module.SDL3.audioContext) ? Module.SDL3.audioContext.state : "none"`);
            if (step.shot) {
                const shot = await cdp.send("Page.captureScreenshot", {format: "png"});
                fs.writeFileSync(path.join(args.out, step.shot + ".png"), Buffer.from(shot.data, "base64"));
                log.write(`${stamp()} PROBE shot ${step.shot} audio=${audio}\n`);
            } else if (step.click) {
                const [x, y] = await canvasPoint(step.click[0], step.click[1]);
                await mouse("mouseMoved", x, y);
                await sleep(400); // the hover first, as a player's pointer arrives before the press
                await mouse("mousePressed", x, y);
                if (step.hold !== 0) await sleep(step.hold ?? 400); // the game must see the press and the release in different frames (headless frames are slow)
                await mouse("mouseReleased", x, y);
                log.write(`${stamp()} PROBE click ${step.click} audio=${audio}\n`);
            } else if (step.tap) {
                const [x, y] = await canvasPoint(step.tap[0], step.tap[1]);
                await cdp.send("Emulation.setTouchEmulationEnabled", {enabled: true, maxTouchPoints: 5});
                await cdp.send("Input.dispatchTouchEvent", {type: "touchStart", touchPoints: [{x, y, id: 1}]});
                await sleep(120);
                await cdp.send("Input.dispatchTouchEvent", {type: "touchEnd", touchPoints: []});
                log.write(`${stamp()} PROBE tap ${step.tap}
`);
            } else if (step.drag) {
                const [x, y] = await canvasPoint(step.drag[0], step.drag[1]);
                const [dx, dy] = [step.drag[2], step.drag[3]];
                await mouse("mouseMoved", x, y);
                await mouse("mousePressed", x, y, "right");
                for (let i = 1; i <= 10; ++i) {
                    await sleep(50);
                    await cdp.send("Input.dispatchMouseEvent", {type: "mouseMoved", x: x + dx * i / 10, y: y + dy * i / 10, button: "right", buttons: 2});
                }
                await sleep(100);
                await mouse("mouseReleased", x + dx, y + dy, "right");
                log.write(`${stamp()} PROBE drag ${step.drag}\n`);
            } else if (step.memory) {
                await sampleMemory(step.memory);
            } else if (step.eval) {
                log.write(`${stamp()} PROBE eval ${JSON.stringify(await evaluate(step.eval))}\n`);
            }
        }
    } finally {
        child.kill();
        server.close();
        log.end();
    }
}

main().catch((error) => {
    console.error("browser_probe:", error.message);
    process.exit(1);
});
