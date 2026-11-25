-- The test mod of tests/l0_mod_actions.txt (docs/modding/reference/actions.md): its action `ping` is bound to F9 and calls
-- on_action; its binding moves game.pause from P to F10.
function on_action(id)
  obl.log("action " .. id)
end
