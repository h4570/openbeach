-- The test mod of tests/l0_mod_script_state.txt (docs/modding/reference/scripting.md): it reads the whole game state every frame
-- and logs a summary now and then. Reading must not change the world, so a replay with this mod keeps its world hashes.
local frames = 0

function on_frame(dt)
  local state = obl.game.state()
  if not state then
    return
  end
  local buildings = obl.game.buildings()
  local selected = obl.game.selected()
  local hovered = obl.game.hovered()
  local w, h = obl.screen.size()
  frames = frames + 1
  if frames % 250 == 1 then
    obl.log(string.format("frame %d: money %.0f day %d speed %d %s guests %d buildings %d selected %s hovered %s screen %dx%d",
      frames, state.money, state.day, state.speed, state.weather, state.guests, #buildings,
      selected and selected.key or "none", hovered and hovered.kind or "none", w, h))
  end
end
