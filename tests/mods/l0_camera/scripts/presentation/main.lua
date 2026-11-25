-- The test mod of tests/l0_mod_script_camera.txt (docs/modding/reference/scripting.md): at fixed gameplay frames it places the
-- camera, pans it and zooms out, and logs what it reads back.
local frames = 0

local function report(what)
  local x, y = obl.camera.position()
  obl.log(what .. ": camera " .. x .. " " .. y .. " zoom " .. obl.camera.zoom())
end

function on_frame(dt)
  if not obl.game.in_world() then
    return
  end
  frames = frames + 1
  if frames == 5 then
    obl.camera.set_position(300, 200)
    report("placed")
  elseif frames == 10 then
    obl.camera.pan(40, -20)
    report("panned")
  elseif frames == 15 then
    obl.camera.set_zoom(0.4)
  elseif frames == 20 then
    report("zoomed")
  end
end
