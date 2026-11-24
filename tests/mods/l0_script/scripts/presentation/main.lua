-- The test mod of tests/l0_mod_script.txt and the replay l0_mod_script (docs/modding/reference/scripting.md): it changes the world
-- only through game commands, at fixed frames of gameplay, so a replay recorded with it plays back without it.
local frames = 0

-- Each step is one command at the gameplay frame it names.
local steps = {
  [5] = { "set-game-speed", { level = 2 } },
  [20] = { "set-paused", { paused = true } },
  [30] = { "set-paused", { paused = false } },
  [40] = { "set-game-speed", { level = 0 } },
}

function on_start()
  obl.log("started, api " .. obl.api)
end

function on_frame(dt)
  if not obl.game.in_world() then
    return
  end
  frames = frames + 1
  local step = steps[frames]
  if step then
    obl.command(step[1], step[2])
    obl.log("frame " .. frames .. ": " .. step[1])
  end
end

function on_input(event)
  if event.kind == "mouse" then
    obl.log("mouse " .. event.phase .. " " .. event.button)
  end
end

function on_stop()
  obl.log("stopped")
end
