-- The test mod of tests/l0_mod_action_takeover.txt: it owns game.pause, so the engine's P does nothing; the mod pauses on F8 itself.
function on_input(event)
  if event.kind == "key" and event.phase == "down" and event.key == "F8" then
    obl.command("set-paused", { paused = true })
    obl.log("paused by F8")
  end
end
