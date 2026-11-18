# LeonHorror — Vertical Slice Prototype

A five-minute third-person survival horror slice built on LeonEngine2: a dark start room, a
foggy corridor with one enemy, an office, a locked storage room and a locked exit. It exists to
prove the gameplay loop (explore, find keys, fight or avoid, save, die, reload) and to show
what the engine still lacks for this kind of game.

Project folder: `Projects/LeonHorror`. Game classes are prefixed `ALeonHorror*` / `ULeonHorror*`.

## Build, run, test

Run everything from the repository root.

| What | Command |
| :--- | :--- |
| Build and run the game | `python Scripts/run_project.py --project Projects/LeonHorror/LeonHorror.lproject` |
| Build only | `python Scripts/build_project.py --project Projects/LeonHorror/LeonHorror.lproject --target LeonHorror` |
| Build the tests | `python Scripts/build_project.py --project Projects/LeonHorror/LeonHorror.lproject --target LeonHorrorTests` |
| Run the tests | `out/Projects/LeonHorror/_project/LeonHorrorTests.exe` |
| Smoke run (exits by itself) | `out/Projects/LeonHorror/_project/LeonHorror.exe --validate-seconds=10` |
| Open in the editor | `python Scripts/run_editor.py`, then pick LeonHorror in the Project Hub |

After adding a source or test file to `Projects/LeonHorror/Sources.cmake`, reconfigure once:
`cmake -S . -B out/Projects/LeonHorror`.

## Controls

| Action | Keyboard and mouse | Gamepad |
| :--- | :--- | :--- |
| Move (the body turns to face where it walks) | W A S D | Left stick |
| Look | Mouse | Right stick |
| Run (uses stamina) | Shift | Left bumper |
| Interact / confirm | E | A |
| Flashlight | F | Y |
| Steel pipe / handgun | 1 / 2 | — |
| Aim (handgun) | Right mouse button | Left trigger |
| Attack / fire | Left mouse button | Right trigger |
| Reload | R | X |
| First aid kit | H | D-pad up |
| Inventory | Tab | Back |
| Pause | Esc | Start |
| In the pause menu | L loads the last save, Q quits | — |

## What to do in the slice

1. Start room: read the note, use the save point on the desk, take the steel pipe by the door.
2. Corridor: one enemy wanders here. It sees in a narrow cone, needs a clear line of sight, and
   hears running and gunshots.
3. Office (left): storage key, handgun, six rounds.
4. Storage room (right, locked): first aid kit, four rounds, exit key.
5. Exit at the far end of the corridor: ends the slice.

Dying returns to the last save, or restarts the map if there is none.

## How it is put together

- **Game module** — `Leon::RegisterLeonHorrorModule` registers every class. The executable, the
  tests and the editor all go through it.
- **Input** — hardware is sampled into `FLeonHorrorInputState` (character) and
  `FLeonHorrorMenuInput` (screens); `ApplyInputState` / `ApplyMenuInput` hold the logic, so tests
  drive the game without a window.
- **Level data** — interactables are ordinary actors in the `.lmap` configured through the
  per-actor `Properties:` block (`Item`, `Count`, `RequiredKey`, `Title`, `Text`,
  `WanderRadius`, `SlideX/Y/Z`, `Open`). Enemies are not stored in the map: an
  `ALeonHorrorEnemySpawn` creates them at begin play.
- **Save game** — `Saved/SaveGames/Slot0.lsav`, line-based text. World state is stored as
  differences from the map keyed by actor GUID (collected pickups, door states, dead enemies by
  spawner GUID, read notes). Loading reloads the map and applies the differences on the first
  tick.
- **HUD** — `ALeonHorrorHUD::BuildModel` turns the game state into plain data
  (`FLeonHorrorHudModel`); drawing only lays that out. Tests check the model.
- **Level** — `Projects/LeonHorror/Scripts/build_slice_map.py` generates `Content/Maps/Slice.lmap`
  from boxes. Edit the script, not the map; pass `--bake` to rebake Draft lightmaps. Rebuild
  `AssetTool` and `LightmassTool` first if the map serializer changed.
- **Animations** — `Scripts/make_animations.py` (run by Blender, headless) authors the clips
  that Mixamo did not provide; `Scripts/import_animations.py` imports them.

Engine features added for this prototype: per-actor map properties, world pause with
tick-when-paused actors, distance and height fog, line-of-sight and noise in AI perception,
one-shot animations with an upper-body layer.

## Tests

`LeonHorrorTests` runs headless (no window, no GPU). Besides unit tests per system, the
`LeonHorror slice map` suite loads the shipped map, checks that every lock has a key and that
GUIDs are unique, and plays the slice from the first room to the exit by teleporting the player
in front of each interactable and using it.

What the tests cannot see: anything visual (lighting, fog, animation quality, HUD layout) and
the feel of movement and combat. Those need a person playing.

## Known limits

- **Animations are scripted, not hand-animated.** They are stiff. Gameplay timing comes from
  timers, not from the clips.
- **Level art is boxes with flat materials.** No props, decals or sound design beyond a few
  reused effects; no ambient audio or music.
- **Enemy AI is a small state machine** (wander, investigate, chase, attack, staggered, dead),
  not a behaviour tree. One enemy type, no hit zones.
- **Navigation is a grid** rebuilt whenever a door changes; the game mode builds it at start
  because the engine does not. Doorways are 1.8 m wide so the grid always finds a way through.
- **Loading a save and restarting need the standalone game.** In the editor's Play mode the
  death screen and "load last save" cannot reload the map.
- **One save slot**, written only at the save point. The UI is ASCII English.
- **No character-to-character blocking in the engine**: the game keeps the enemy from walking
  through the player by itself.
- **Flat floors only.** Slopes and stair meshes are out of scope; steps up to 0.4 m work.
- Lightmaps are baked at Draft quality.
- **The flashlight casts no shadows.** On Intel UHD graphics the spot shadow map comes out empty
  (everything reads as shadowed), so the flashlight opts out with `FSpotLightComponent::bCastShadows`.
  The cause in the renderer is still to be found.
- **Light intensities look large** (a room light is 6 to 16, the flashlight 40) because the engine
  attenuates as `1 / (distance^2 + 1)`.
