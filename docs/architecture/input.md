# Input object

The game reads keys, mouse buttons, the cursor position and the camera drag through its `Input` object (`world_data::Input`,
`g_Input`) and the `Input_*` entry points. One implementation serves both hosts; only the devices behind it differ.

## Layers
| Layer | File | Builds on |
|---|---|---|
| Pure state: construct, mouse init, reset, key states, the buffered-mouse state machine (`ApplyMouse`) | `src/game/ui/input_state.h` | everything; unit-tested (`tests/unit/input_state_test.cpp`, also on Linux) |
| The object: every `Input_*` function, `Input_Show/HideSystemCursor`, `Input_SetCursorShape`, the camera drag state, `RegisterInput` | `src/game/ui/input_ops.cpp` | Windows host and Linux `obl-sim` |
| Device interface `InputBackend`, `CurrentRelativeDrag`, `ResetInput` | `src/game/ui/input_backend.h` | both |
| SDL backend: SDL events into the mouse records, focus (SDL event watch), SDL relative drag, SDL cursors, test injection | `src/platform/input.cpp`, `cursor.cpp` (system cursor) | Windows only |
| No backend: `PlatformInputBackend()` returns null | `src/host_linux/headless_input.cpp` | Linux only |

`RegisterInput` picks the host's backend, or the built-in null backend when the run is headless (`config::Get().headless`) or
the host has none. With the null backend `Input_Update` resets the object every frame (no focus), so every frame reads as no
key, no button and no cursor (`-300, -300`); there is no mouse device, so `Input_UpdateMouse` answers false; a camera drag
request resets the object, as the SDL backend does when it cannot start a drag. The Windows headless run and the Linux
`obl-sim` therefore run the same code.

## Contract details
- **Keyboard:** `core/key_code.h` defines `obl::input::KeyCode`, the stable physical-key indices of the 256-byte
  state map. `SdlScancodeToKeyCode` maps layout-independent SDL scancodes to these codes; unsupported keys return
  `None`. States are 0 released, 1 pressed this update and 2 held. `KeyLatch` preserves a complete tap between
  updates for one pressed frame, ignores repeats and resets on focus loss. Hotkeys use the same codes on every host.
  Unit tests check mapped keys against SDL's independent PC scancode table, including keypad and extended keys.
- **Reset** (`ResetInput`): the camera drag ends first (the SDL backend returns the cursor to the grab point), then the
  backend drops queued events and its last cursor position, then the fields (`input_state::Reset`): a reset never leaves a
  cursor position. The cursor shape id survives.
- **Cursor position:** the SDL backend maps the client position into the letterboxed image (`ClientToGame`); a bar is no
  position (`-300, -300`), except the client's outermost column or row, which moves onto the image's edge
  (`SnapClientEdgeToImage`, `display_geometry.h`): there the screen edge stops the pointer, and the map scrolls only at the
  game's edge pixel. `[Display] ConfineMouse` keeps the pointer in the window while it has the keyboard focus, in gameplay
  only and not while the in-game options (Esc) are open or the player has paused with P (a forced pause keeps it) (`UpdateMouseConfinement`, `WantMouseConfined`; never in a hidden run), and Alt+Enter switches between the window and
  fullscreen (`TakeFullscreenToggle`; the game never sees that Enter).
- **Mouse update:** the backend reads up to 60 records into `m_records` and the cursor position in game coordinates; while the
  drag runs the object uses the drag anchor as the position and the X/Y records as deltas. A press and release in the same
  update are seen as a click: the release is deferred to the next update.
- **Cursor shape:** `Input_SetCursorShape` ignores the current shape, otherwise the backend loads the system cursor (resources
  106, 101-103, 107, 108, 111 for shapes 1-7) before the object stores the id at `m_cursorShape`.
- **Actions and key bindings:** the game's hotkeys read named actions, not key codes (`formats/key_actions.h`, the table of
  [actions.md](../modding/reference/actions.md)). `key_actions::Resolve` settles the layers once at start-up
  (`mods::ConfigureKeyBindings`, called from `game_start.cpp`): engine defaults, the active mods' `actions` and `bindings` in load
  order, then `[Keys]` of `openbeach.ini`; one key belongs to one action, the strongest layer keeps it. The sites ask
  `ui::ActionPressed/ActionHeld` (place and tool modes) or `world_view::AnyKey` (the world's hotkeys and camera keys), which loop over the
  keys bound to the action and give none while a mod has taken the action over. With no mod and no `[Keys]` line the table is the
  keys the game always had. A mod's own action fires `on_action(id)` from `mods::PresentationInput`, before the raw key event goes to
  `on_input`. Bindings are presentation data: they never reach the simulation.
- **Mod scripts:** after `Input_Update` the presentation scripts see the update's events and may take a consumed one back out
  of the object, or add a made-up click ([mods/index.md](mods/index.md#the-input-path)). Finger events go to the scripts
  (`mods::RouteTouch`) before `TouchGestures`; a finger whose down a script consumed, or every finger while a mod owns
  `input.touch`, never reaches the gestures.
- **Test injection** (`SetSyntheticCursor`, `InjectMouseButton`, `InjectRelativeMotion`, `InjectFinger`; `input.h`) feeds the
  SDL backend; `InjectFinger` takes the finger path of the browser build, gestures included, on any host. The Linux obl-sim
  stubs them (`headless_modules.cpp`), headless scripts refuse the directives that use them.
