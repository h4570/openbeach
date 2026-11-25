-- The test mod of tests/l0_mod_script_touch.txt (docs/modding/reference/scripting.md): it takes over input.touch and turns a tap
-- into a right click where the finger went up. Finger 9 makes the script fail on purpose, which gives touch back to the engine.
function on_input(event)
  if event.kind ~= "touch" then
    return
  end
  if event.finger == 9 then
    error("on purpose")
  end
  if event.phase == "up" then
    obl.input.click(event.x, event.y, "right")
    obl.log("tap at " .. event.x .. " " .. event.y .. " as a right click")
  end
  event:consume()
end

function on_action(event)
  obl.log("action " .. event)
end
