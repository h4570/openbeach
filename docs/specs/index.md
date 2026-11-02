# Behavior specs

Short specs of what each module family does, written by hand from the unit tests (`tests/unit/`), the game test scripts
(`tests/*.txt`) and the current behavior. They are what the code is reviewed against, so they name no addresses and no
layouts of any other program. Contracts of the implementation (files, classes, save fields) stay in
[../architecture/](../architecture/index.md); a spec says what an observer sees.

Format: **Purpose**, **Vocabulary** where needed, numbered **Rules** that each name the test that
pins them, **Edge cases**, **Not covered by tests** (what the code does but no test asserts; nothing is invented), **Tests**.

Every listed module family has a spec, including building breakdown, queue, drawing, thoughts,
update, teardown and variable text. World managers were re-verified against their tests; vehicle and performer specs
retain test references for their detailed per-state sequences.

| Topic | What it covers |
|---|---|
| [ferry.md](ferry.md) | The ferry schedule: boat order and cursor, arrivals, performance crowds, leavers, time queries. |
| [cleaning.md](cleaning.md) | The cleaning queue: requests, urgency, claims, the urgent search and the scored search for litter, bins and toilets. |
| [construction.md](construction.md) | The construction manager: jobs, the builder table, assignment and reallocation, starting work, progress and its end. |
| [event.md](event.md) | The event (disaster) manager: schedule, start and end times, effects per event type, repeating schedules, load. |
| [power.md](power.md) | The power grid: generators, supply and demand, outages. |
| [staff-calls.md](staff-calls.md) | The script functions of staff members (builder, cleaner, lifeguard, mechanic, rep, security guard) and the shared dispatcher. |
| [text-table.md](text-table.md) | The text table: lookup, languages and fallbacks. |
| [world-params.md](world-params.md) | The world parameters: adding, processing and querying values. |
| [weather.md](weather.md) | The weather manager: schedule, rain loop, daylight and tint, thunder and strikes, manual colour, rain and lightning drawing. |
| [message-handler.md](message-handler.md) | The message handler: immediate and delayed delivery, pause behavior, the per-frame update, clearing a receiver's queue. |
| [repair.md](repair.md) | The repair job queue: priorities, urgency, scans, claims, switching jobs, watering, job status. |
| [guest-lifecycle.md](guest-lifecycle.md) | Guest bed time, the leave decision, the last-day leave hour and the attraction slot. |
| [staff-duties.md](staff-duties.md) | Staff shifts, the builder number, facing clamp and the walk point to a bin. |
| [guest-pool.md](guest-pool.md) | Guest pool facing, goals, next pool tile, slide and swim sprites, palette and delay. |
| [guest-selection.md](guest-selection.md) | Guest hovering, clicking, panel and camera, and the screen-point hit test. |
| [guest-opinions.md](guest-opinions.md) | Guest opinion update, happiness, walk mood, thoughts and their penalties, desire misses and satisfaction. |
| [staff-visibility.md](staff-visibility.md) | Hiding and showing staff, waiters and show actors. |
| [staff-state-update.md](staff-state-update.md) | Staff sacking and state entry, the per-frame update and happiness drift, feedback texts and the thought list. |
| [moving-object-events.md](moving-object-events.md) | Moving objects: reaching a target, route end, sea, follower and building walk, and the per-frame update. |
| [guest-walk.md](guest-walk.md) | Guest walkable tiles, wandering, boarding and surf rays, dance floor, unload goals and bin choice. |
| [guest-animation.md](guest-animation.md) | Guest animation name resolution, fidgets, animation type, duration and end messages. |
| [guest-placement.md](guest-placement.md) | Guest snog steps, random surface candidates, clear entry points and hotel room. |
| [moving-object-motion.md](moving-object-motion.md) | Moving objects: route building, target update and re-send, stepping behaviours and steering physics. |
| [building-open-state.md](building-open-state.md) | Building open state: opening hours, forced closures, power shedding and power output. |
| [building-construction.md](building-construction.md) | What a newly created building object starts with: defaults, sprites, commodities, beer. |
| [building-events.md](building-events.md) | Building show slots: start and end, the loaded cursor, casts and guest timing. |
| [building-spawn.md](building-spawn.md) | Building spawning: vehicles, tables, initial staff, hiring and dismissing. |
| [building-breakdown.md](building-breakdown.md) | Building wear, lightning damage, failure rolls and preventive inspections. |
| [building-queue.md](building-queue.md) | Building queues: capacity, standing points, movement, clearing, boarding and reload. |
| [building-draw.md](building-draw.md) | Building selection, previews, overlays, lighting and visual effects. |
| [building-thoughts.md](building-thoughts.md) | Building comments: copied text, newest-eight retention, removal and indexed access. |
| [building-update.md](building-update.md) | Building update order, cleaning, sound loops and waiting demolition. |
| [building-teardown.md](building-teardown.md) | Building resource release, thought cleanup, sound stop and deletion. |
| [variable-text.md](variable-text.md) | Building, guest and staff dialog text: labels, status, numbers, activity and fallbacks. |
| [shark.md](shark.md) | Sharks: states, alerting guests, victim choice and swim targets. |
| [scenery.md](scenery.md) | Scenery: states, seats, bins, repair, lamp hours, refunds and placement bookkeeping. |
| [vehicle.md](vehicle.md) | Vehicles: type by key, construction and destruction, messages, states, update, paths, animation, saving. |
| [performer.md](performer.md) | Show performers: states, looks, saving, joining the show, per-act sequences. |
