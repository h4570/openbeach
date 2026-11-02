#pragma once

#include <string>
#include <vector>

namespace obl::test {

// Test-script directives of the command layer (docs/testing/game-tests.md, "Commands"):
//   command <text>       submits one command in its text form (docs/architecture/commands.md) to the game loop's queue; it runs at the start
//                        of the next gameplay game_api::Update. A text that does not parse is FAILED.
//   command-report       `command-report tick= applied= rejected= dropped= pending= last-applied= in-world=` (totals of the process)
//   object-find building|scenery <KEY> [n] / object-find staff <Class> [n]   the n-th (default 0) such object: `object-find id= count=`
//                        (id -1 when there is none); Class is Lifeguard, Mechanic, Rep, Builder, Cleaner or SecurityGuard
//   place-find building|scenery <KEY> [dir]   the first footprint origin (top row first) where the place mode's fit check passes:
//                        `place-find found= x= y=` (x/y -1 when none), for `command place-building key=<KEY> x=$x y=$y dir=<dir>`
//   place-find surface <KEY> / place-find dozer   the first tile the surface mode of the path or grass <KEY> can paint, or the dozer clear
//   object-report <id>   `object-report exists= id= type= state= waiting= pos-x= pos-y=` plus, for a building, `open= user-close= build-job= demolish-job=
//                        repair-interval= called-repair= called-clean= staff= price0..4= beer=` (with a show schedule also `events=1 event-price0..2= event-id0..2=`)
//                        and, for a staff member, `wage= start= end= home= direct= direct-x= direct-y= direct-object= goto=` (the send-staff order)
//   tile-report <x> <y>  `tile-report flags= grass= path=` (grass/path 1 when all bits of that surface are set)
// `$key` in a directive's arguments -> the value of the live probe or latest report value `key` (e.g. `object=$id` after `object-find`):
// whole values as integers, others as the shortest form of their float. False, with the key in `missing`, when a value is missing.
bool SubstituteValues(const std::string& text, std::string& out, std::string& missing);

bool ExecuteCommandLayerCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
