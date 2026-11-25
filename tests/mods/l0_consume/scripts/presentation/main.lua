-- The test mod of tests/l0_mod_script_consume.txt (docs/modding/reference/scripting.md): in the world it takes the build tool for
-- the toilet block and consumes the first left click, so that click places nothing and the next one builds.
local tool = false
local consumed = 0

function on_frame(dt)
  if not tool and obl.game.in_world() then
    tool = obl.gui.place("building", "loo")
    obl.log("build tool " .. tostring(tool))
  end
end

function on_input(event)
  if event.kind == "mouse" and event.button == "left" and (event.phase == "down" or event.phase == "up") and consumed < 2 then
    event:consume()
    consumed = consumed + 1
    obl.log("consumed left " .. event.phase)
  end
end

function on_input_observe(event)
  if event.kind == "mouse" and event.phase == "down" then
    obl.log("observed left down, consumed by " .. tostring(event.consumed_by))
  end
end
