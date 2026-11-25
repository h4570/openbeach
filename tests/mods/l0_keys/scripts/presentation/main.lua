-- The test mod of tests/l0_mod_keys.txt (docs/modding/reference/actions.md#rebinding-from-a-script): each press of its action `probe`
-- (F9) runs the next step of obl.actions, so the test decides when with a key and a fixed number of frames.
local steps = {}

local function find(name)
  for _, action in ipairs(obl.actions.list()) do
    if action.name == name then return action end
  end
end

local function show(name)
  local action = find(name)
  obl.log(name .. " keys=" .. table.concat(action.keys, "+") .. " defaults=" .. table.concat(action.defaults, "+"))
end

steps[1] = function()
  local list = obl.actions.list()
  obl.log("listed " .. #list .. " has_probe=" .. tostring(find("l0_keys.probe") ~= nil) .. " pause_held=" .. tostring(find("game.pause").held))
  show("game.pause")
  show("camera.left")
end
steps[2] = function()
  obl.log("bind pause F10: " .. tostring((obl.actions.bind("game.pause", "F10"))))
  show("game.pause")
  -- F10 is the pause action's now; the zoom action cannot have it.
  local ok, why = obl.actions.bind("game.speed_up", "F10")
  obl.log("bind speed_up F10: " .. tostring(ok) .. " " .. why)
  local ok2, why2 = obl.actions.bind("game.speed_up", { "Equals", "NoSuchKey" })
  obl.log("bind speed_up unknown: " .. tostring(ok2) .. " " .. why2)
  local ok3, why3 = obl.actions.bind("no.such", "F11")
  obl.log("bind no.such: " .. tostring(ok3) .. " " .. why3)
  show("game.speed_up")
end
steps[3] = function()
  obl.log("bind pause default: " .. tostring((obl.actions.bind("game.pause", "default"))))
  show("game.pause")
  obl.log("bind music none: " .. tostring((obl.actions.bind("panel.music", "none"))))
  show("panel.music")
  obl.log("bind music list: " .. tostring((obl.actions.bind("panel.music", { "F6", "F7" }))))
  show("panel.music")
  obl.actions.bind("panel.music", "default")
  obl.log("bind pause for restart: " .. tostring((obl.actions.bind("game.pause", "F10"))))
end
steps[4] = function()
  show("game.pause")
end

local pressed = 0
function on_action(id)
  if id ~= "probe" then return end
  pressed = pressed + 1
  if steps[pressed] then steps[pressed]() end
end
