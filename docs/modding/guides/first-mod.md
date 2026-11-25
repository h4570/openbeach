# Make your first mod

Copy [mod_template](../examples/mod_template/index.md) to `mods/mod_template/` in the game directory. Use the L0 pack
with English selected. The manifest targets L0 and uses world scope, which lets it replace an existing text key.

1. Open `text/en.patch.json` and set `L0_TITLE` to the title you want to see in the scenario picker.
2. In a terminal, run the checker against the copied source.

   ```sh
   python tools/check_mod.py path/to/mods/mod_template --engine path/to/OpenBeach --game-dir path/to/game --pack l0
   ```

3. Start OpenBeach with `OBL_PACK=l0`, open **Mods** and switch on **Text mod template**. Restart the game, then open the
   scenario picker to see the title.

Use unique ids in both the folder and manifest when you copy the template for another mod. For live iteration, see
[developer mode](developer-mode.md).
