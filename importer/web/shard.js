// Sharing the browser import between Web Workers (docs/architecture/data-formats/converter/index.md, "Sharded conversion").
// The site is not cross-origin isolated, so there are no threads and no shared memory: the import worker (the coordinator)
// starts helper workers, each with its own wasm instance, and the only thing that crosses between them is postMessage. Each
// helper converts its share of the units of the "maps" and "sprites" steps (worker_main.cpp, stage "shard") and posts the
// finished units back; the coordinator then runs the whole import in its own instance and takes those units instead of
// converting them (stage "units"), so the pack, its first error and its progress are those of an import by one worker.
//
// This file is the part that does not depend on where it runs: import_worker.js loads it with importScripts and
// node_check.cjs with require, with Web Workers and with worker_threads as the helpers.
"use strict";

// Few helpers cost more in copies than they save; more than this stops paying (the coordinator, the page and the disk share
// the machine) and each one holds a wasm heap of its own.
const kMinHelpers = 2;
const kMaxHelpers = 8;

// How many helpers to start: one per core but the page's (the coordinator mostly waits), fewer on a device that reports little
// memory (`deviceMemory` in GB, rounded down to a power of two by the browser and absent in some). 1 means no helpers: the
// worker imports alone, exactly as before.
function chooseHelperCount({hardwareConcurrency, deviceMemory}) {
    let helpers = Math.min(Math.floor(hardwareConcurrency || 1) - 1, kMaxHelpers);
    if (deviceMemory > 0) {
        const byMemory = deviceMemory <= 2 ? 1 : deviceMemory <= 4 ? 2 : deviceMemory <= 8 ? 4 : kMaxHelpers;
        helpers = Math.min(helpers, byMemory);
    }
    return helpers >= kMinHelpers ? helpers : 1;
}

// The finished units of the shards: unit `unit` of step `step` is the files a serial run writes for it, in order, and the
// error that ended it. `codec` turns bytes into what is kept (a Blob in the browser, the bytes under Node) and back. The
// bridge calls below are what worker_main.cpp's UnitPutJs ... UnitErrorJs reach.
function makeUnitStore(codec) {
    const records = new Map(); // "step/unit" -> {step, unit, error, files: [[path, kept]]}
    let building = null;
    let taken = null;
    const keyOf = (step, unit) => `${step}/${unit}`;
    const recordFor = (step, unit) => {
        if (!building || building.step !== step || building.unit !== unit) building = {step, unit, error: "", files: []};
        return building;
    };
    return {
        records,
        bridge: {
            unitPut(step, unit, path, bytes) {
                recordFor(step, unit).files.push([path, codec.keep(bytes)]);
            },
            unitEnd(step, unit, error) {
                const record = recordFor(step, unit);
                record.error = error;
                records.set(keyOf(step, unit), record);
                building = null;
            },
            // The files of a unit, taken once (its bytes are then the caller's), or -1 when no shard made it.
            unitOpen(step, unit) {
                taken = records.get(keyOf(step, unit)) || null;
                records.delete(keyOf(step, unit));
                return taken ? taken.files.length : -1;
            },
            unitPath: (index) => taken.files[index][0],
            unitBytes: (index) => codec.read(taken.files[index][1]),
            unitError: () => taken.error,
        },
        // What a helper posts back, and what the coordinator adds to its own store.
        exportRecords: () => [...records.values()],
        addRecords(list) {
            for (const record of list) records.set(keyOf(record.step, record.unit), record);
        },
        clear() {
            records.clear();
            building = null;
            taken = null;
        },
    };
}

// The page's progress while the shards run: their counts, added, as one stream of units under the step "maps" (the first of
// the two). A shard reports each of its steps from 0 of its share, so the total grows when the sprites start.
function makeShardProgress(report) {
    const shares = new Map(); // "shard/step" -> {done, total}
    return (shard, step, done, total) => {
        shares.set(`${shard}/${step}`, {done, total});
        let sumDone = 0, sumTotal = 0;
        for (const share of shares.values()) {
            sumDone += share.done;
            sumTotal += share.total;
        }
        report({type: "progress", step: "maps", done: sumDone, total: sumTotal, at: Date.now()});
    };
}

// What the coordinator's progress shows of the replay that takes the units over: the steps the shards did are not shown
// again (their bar already ran), so the bar never goes back.
const kQuietSteps = new Set(["gui-tables", "game-parameters", "scripts", "maps"]);
const kQuietCounts = new Set(["maps", "sprites"]);

// Starts `count` helpers, hands each `jobFor(index)` and gathers their units into `store`. `spawn()` makes a helper: an object
// with postMessage(message), terminate() and the properties onmessage(event) and onerror(error) it is given. A helper answers
// {type: "progress", step, done, total}, then {type: "units", records} or {type: "shard-error", message}.
// Resolves {ok: true} when every helper delivered, {ok: false, reason} when one failed or died: the coordinator then imports
// alone, which reports whatever a failed source is (a helper's own message would differ from a single worker's).
function runHelpers({count, spawn, jobFor, store, report}) {
    return new Promise((resolve) => {
        const helpers = [];
        const progress = makeShardProgress(report);
        let waiting = count;
        let finished = false;
        const finish = (result) => {
            if (finished) return;
            finished = true;
            for (const helper of helpers) helper.terminate();
            resolve(result);
        };
        for (let index = 0; index < count; ++index) {
            let helper;
            try {
                helper = spawn();
            } catch (error) {
                finish({ok: false, reason: String(error)});
                return;
            }
            helpers.push(helper);
            helper.onmessage = (event) => {
                const message = event.data;
                if (message.type === "progress") {
                    progress(index, message.step, message.done || 0, message.total || 0);
                } else if (message.type === "units") {
                    store.addRecords(message.records);
                    if (--waiting === 0) finish({ok: true});
                } else if (message.type === "shard-error") {
                    finish({ok: false, reason: message.message});
                }
            };
            helper.onerror = (error) => finish({ok: false, reason: String((error && error.message) || error)});
            helper.postMessage(jobFor(index));
        }
    });
}

// The helper's side: converts its share and posts the units. `env.createModule()` makes the wasm instance, `env.makeBridge(job,
// store, post)` its calls for the job's inputs (reads, spools adopted from the job, progress posted as it counts) and
// `env.codec` keeps unit bytes. The job is {plan: the import request as JSON, shard: {index, count}, ...what makeBridge reads}.
async function runShardJob(job, env, post) {
    try {
        const store = makeUnitStore(env.codec);
        const instance = await env.createModule();
        instance.oblImport = Object.assign(env.makeBridge(job, store, post), store.bridge);
        const plan = Object.assign(JSON.parse(job.plan), {stage: "shard", shard: job.shard});
        if (instance.ccall("obl_import", "number", ["string"], [JSON.stringify(plan)]) !== 0) {
            post({type: "shard-error", message: instance.UTF8ToString(instance._obl_import_error())});
            return;
        }
        post({type: "units", records: store.exportRecords()});
    } catch (error) {
        post({type: "shard-error", message: String(error)});
    }
}

const exported = {chooseHelperCount, makeUnitStore, makeShardProgress, runHelpers, runShardJob, kQuietSteps, kQuietCounts, kMinHelpers, kMaxHelpers};
if (typeof module !== "undefined") module.exports = exported;
else self.oblShard = exported;
