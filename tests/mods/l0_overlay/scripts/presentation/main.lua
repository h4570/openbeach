-- The test mod of tests/l0_mod_script_overlay.txt (docs/modding/reference/scripting.md): it draws a red box, a blended box, an
-- outline, text and its own image over the world every frame.
function on_frame(dt)
  if not obl.game.in_world() then
    return
  end
  obl.overlay.rect(10, 10, 40, 30, 0xFFFF0000)
  obl.overlay.rect(60, 10, 40, 30, 0x800000FF)
  obl.overlay.frame(110, 10, 40, 30, 0xFF00FF00)
  local w, h = obl.overlay.measure("overlay", "guifont8")
  obl.overlay.rect(10, 50, w + 4, h + 4, 0xFF000000)
  obl.overlay.text(12, 52, "overlay", "guifont8", 0xFFFFFF00)
  obl.overlay.image(160, 10, "checker.png")
  obl.overlay.image(200, 10, "checker.png", 16, 16)
end
