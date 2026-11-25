-- The test mod of tests/l0_mod_recipe.txt (docs/modding/reference/recipes.md): draws the overlay image built by
-- recipes/banner.json from the L0 pack's images/screens/l0_title.png.
function on_frame(dt)
  if not obl.game.in_world() then
    return
  end
  obl.overlay.image(160, 10, "banner.png")
end
