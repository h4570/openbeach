-- The test mod of tests/l0_mod_settings.txt (docs/modding/reference/scripting.md): it reads its settings and reports each change.
-- It sends no command, so the world stays what it is without the mod.
local function show()
  return "wide=" .. tostring(obl.settings.get("wide")) .. " theme=" .. obl.settings.get("theme") .. " zoom=" .. obl.settings.get("zoom")
end

function on_start()
  obl.log("start " .. show())
end

function on_setting(id, value)
  obl.log("changed " .. id .. " " .. tostring(value) .. ", now " .. show())
end
