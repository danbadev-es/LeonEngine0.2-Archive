# LeonHorror — the clinic

The level the game opens on: a small single-storey clinic of sixteen rooms, about 480 m², built to
show what the engine can do with an interior. This page says what is in it, how it is made and how
to change it. The older five-room level, `Slice`, is described in `LEONHORROR_PROTOTYPE.md`; it stays
in the project because the reference pictures and many tests run on it.

Everything in the level comes from scripts. Nothing was modelled, painted or placed by hand, and
nothing was downloaded: the scripts are the source and the files in `Content/` are their output.

## Quick reference

| To do this | Run this |
|---|---|
| Play it | `python Tools/run_project.py --project Games/LeonHorror/LeonHorror.lproject` |
| Open a particular map | `LeonHorror.exe --map=/Game/Maps/TestCorridor` |
| Rebuild textures, materials and meshes | `python Tools/build_art.py` (see "Art") |
| Rebuild the three characters | Blender, `Games/LeonHorror/Art/build_characters.py` (see its header) |
| Write the map again | `python Games/LeonHorror/Scripts/build_clinic_map.py` |
| Write the map and bake it | `python Games/LeonHorror/Scripts/build_clinic_map.py --bake` |
| Run the level's tests | `LeonHorrorTests.exe -ts="LeonHorror clinic map"` |
| A run that closes by itself | `LeonHorror.exe --validate-seconds=20` |
| The same, starting with a weapon | add `--validate-give=Weapon.Pipe` (or `Weapon.Pistol`, or several separated by commas) |
| The same, standing somewhere else | add `--validate-at=X,Z` (engine metres: plan east, minus plan north) |
| The same, writing the frame timings | add `--validate-report=perf.txt` (and `--width=1920 --height=1080 --no-vsync` to measure) |

A map that is written again has lost its bake: bake it before running the game, or the level is lit
by its lamps alone. Baking the clinic at Production quality takes three to four minutes. The bake
tool and the game must be built from the same sources, since both compute the hash that says
whether a bake still fits its map.

## Where things are

| Path under `Games/LeonHorror` | What it holds |
|---|---|
| `Scripts/clinic_layout.py` | The floor plan: rooms as rectangles, the openings between them, wall and door sizes. Read by the art build and by the map script, so walls and doors always agree |
| `Scripts/build_clinic_map.py` | The level: what stands where, room by room |
| `Scripts/leonmap.py` | Writing a `.lmap` from Python (meshes, lights, actors, probes) |
| `Art/build.py`, `Art/leonart/` | Materials, room shells, doors, props and items, built in headless Blender |
| `Art/build_characters.py`, `Art/leonart/char*.py` | The protagonist, the patient and the orderly |
| `Scripts/make_audio.py` | Every sound, synthesised (`Content/Audio/README.md` lists them) |
| `Scripts/make_animations.py` | Animation clips authored by script |
| `Content/Maps/Clinic.lmap`, `Content/Lightmaps/Clinic.llightmap` | The level and its bake |
| `Intermediate/Art/` | Source images, FBX files and review sheets. Not versioned: rebuild them |

## The building

Plan coordinates are metres east (X) and north (Y) of the south-west corner, as drawn in Blender.
The engine's are X east, Y up, Z south: a plan point (x, y) is at (x, 0, -y) in the map, and a turn
about Blender's Z is the same yaw in the engine.

```
 north
 +-----------+--------+---------+
 |  Theatre  | Morgue |  Plant  |
 +-----------+--------+---------+-------+
 |        Service corridor      |  Bay  |  -> the way out
 +-----------+--+---------------+-------+
 |   Staff   |  |    Storage    |
 +-----------+C +---------------+
 |  Pharmacy |o |    Toilets    |
 +-----------+r +---------------+
 | Consult 2 |r |     X-ray     |
 +-----------+i +---------------+
 | Consult 1 |d |     Ward      |
 +-----------+--+---------------+
 |    Reception    |   Waiting  |
 +-----------------+------------+
 south
```

Each room is four meshes (`SM_Clinic_<Room>_Floor`, `_Walls`, `_Ceiling`, `_Skirting`) made from
the plan, with the door holes already cut, placed at the origin. Doors are separate: a frame, and
one or two leaves that turn on their hinge.

## The way through

| Step | Where | What |
|---|---|---|
| 1 | Reception | Wake up. A note, the typewriter (saves the game), a pipe |
| 2 | Corridor | A patient wanders it. Consulting room 1 and the staff room are open |
| 3 | Staff room | The key to consulting room 2, the handgun, a second typewriter |
| 4 | Consulting room 2 | The pharmacy key |
| 5 | Pharmacy | First aid and cartridges. Worth the detour, not required |
| 6 | Ward | Two patients among the beds; the store room key |
| 7 | Store room | The fuse |
| 8 | Service corridor | Dark until the fuse is fitted; an orderly walks it |
| 9 | Plant room | The fuse box: power for the north wing |
| 10 | Morgue | Its door has an electric lock. The key to the ambulance bay |
| 11 | Bay | The way out |

The theatre, the X-ray room and the toilets are off the route and hold supplies and notes.

## What the level is made of

- **Doors** (`ALeonHorrorDoor`): `Swing` in degrees makes a door turn on its hinge; `Partner` names
  the other leaf of a pair, which opens with it; `RequiredKey` locks it; `PowerGroup` gives it an
  electric lock that only works while that group has power; `Sound` chooses Wood, Swing or Metal.
  An open door blocks nothing.
- **Power** (`ALeonHorrorPower.hpp`): an actor with a `PowerGroup` property is dead until its group
  is switched on: lights go out, light fittings stop glowing, ambient sounds fall silent, electric
  doors stay shut. `ALeonHorrorFuseBox` switches its `Group` on when used with the `RequiredItem`.
  `Flicker` on a light makes it waver. Which groups are live is saved with the game.
- **Enemies** (`FLeonHorrorArchetypes`): a spawner's `Type` picks a kind. The patient is thin and
  quick and does not take many blows; the orderly is big and slow, shrugs a hit off and hits hard.
  Each kind uses its own clips when files named `/Game/Animations/<Kind><Clip>.lanim` exist
  (Idle, Walk, Attack, Hit, Death) and the mannequin's otherwise; both kinds have theirs. A walk is
  played faster as the enemy moves faster, so the feet keep up in a chase.
- **The player's gestures**: bending to the floor or reaching to a table for what is picked up,
  pushing a door, reloading, first aid; below a third of a life the player stands and walks bent
  over, and a blow from in front throws the body over backwards. The clips are authored by
  `Scripts/player_clips.py` and `Scripts/enemy_clips.py` (`import_animations.py --player`,
  `--enemies`).
- **Characters**: three bodies on one skeleton (the 52 bones of `YBot`), so every clip plays on all
  of them. The game scales each to its height.
- **In the hands** (`ALeonHorrorHeldItem`): the flashlight in the left hand, pointing where the
  player looks; the weapon in use in the right. Their meshes (`SM_Held_*`) have their origin where
  the hand closes. The beam itself still starts ahead of the eye: from the hand, the body's own
  shadow swallows it.
- **Sound** (`LeonHorrorSound`): every sound is played by cue name through one place. Footsteps
  come a stride at a time and follow the floor (tile or vinyl); a chase track comes in when
  something hunts the player and gives way a few seconds after it stops.
- **Navigation**: the level's nav bounds volume asks for a 0.25 m grid with a 0.25 m agent
  (`CellSize`, `AgentRadius`), because the default 0.75 m grid cannot find a 0.9 m doorway. A door
  that opens or shuts updates the cells around it only.
- **Light**: every ceiling fitting is a mesh with its own copy of the diffuser material and a
  Stationary point light under it. Some fittings are dead and some flicker; the north wing is on
  the `North` power group. A few lights cast shadows: only four can at a time.
- **Reflections**: one reflection probe a room (`Docs/RENDERER.md`, "Reflection probes").
- **Light on what moves**: the bake leaves light probes every 1.5 m; characters and other things
  without a lightmap take their ambient light from them.
- **Decals**: stains, damp, blood and claw marks are quads with a decal material laid over floors
  and walls (`Art/leonart/materials_decals.py`, `decals.py`; `Docs/RENDERER.md`, "Decals"). The map
  script places them by their middle (`floor_decal`, `wall_decal`) and refuses one that would leave
  its room or turn a corner; keep them off doorways by eye. They are Movable: adding, moving or
  removing one needs no new bake.
- **Signs**: each door has an enamelled plate beside it naming the room (`Art/leonart/signs.py`);
  the lettering is geometry.

## Art

`python Tools/build_art.py` runs Blender on `Art/build.py`, imports what it wrote and leaves review
sheets in `Intermediate/Art/Sheets`. `--materials` or `--meshes` do half of it; `--only A,B` names
what to rebuild.

- **Materials** are procedural: each is a function that fills colour, normal and a packed
  occlusion-roughness-metal image from tileable noise, 1 m of surface to a tile. Images have row 0 at
  v = 0.
- **Meshes** are written with a small builder (`leonart/mesh.py`: boxes, panels, cylinders, lathes,
  tubes). Each piece is exported at the origin with two sets of UVs: the first projected in world
  metres so textures keep their scale, the second unwrapped for the lightmap. A `.json` beside the
  FBX gives the lightmap resolution and the material slots, which the map script reads.
- **Conventions**: props face -Y in Blender; a door leaf has its origin on the hinge at floor level
  and extends along +X; a frame has its origin in the middle of the threshold; a pickup lies on its
  origin as on a table.
- Importing a mesh never overwrites a material that exists: materials belong to the material build.

## Changing the level

Edit `build_clinic_map.py` (never the `.lmap`), run it with `--bake`, run the tests. Places are
given from each room's south-west inner corner (`room.at(east, north, height)`), or along a wall
(`room.wall("north", along, height)`).

Keep furniture out of doorways: an enemy needs a clear way in. `ClinicMapTests` checks, with every
door open, that each room can be walked to from reception, and plays the level from the first room
to the exit. It also checks that every lock has exactly one key, that the fuse and both weapons
exist, and that every mesh and sound the map names is in the project.

Moving a wall means changing `clinic_layout.py`, rebuilding the room shells
(`Tools/build_art.py --meshes`) and then the map.

## Performance

Measured on the development laptop (RTX 5060 Laptop, Release build, 1920 x 1080, no vertical sync,
20 s standing in a room with `--validate-report`):

| Room | Frame, median | GPU | CPU, rendering | Draw calls (of them, shadows) |
|---|---|---|---|---|
| Reception | 7.9 ms | 3.9 ms | 6.7 ms | 2574 (1845) |
| Corridor | 9.4 ms | 5.8 ms | 8.0 ms | 2414 (1798) |
| Ward | 9.1 ms | 4.7 ms | 7.7 ms | 2413 (1789) |

The game is bound by the CPU, and most of it is submitting shadow maps of everything in the level.
Gameplay (enemies, the player, sound) takes under 1 ms a frame.

Known: on this laptop `Present` sometimes blocks for about half a second (10 to 25 times in 20 s,
on the Slice map too, with or without vertical sync, and with a tearing swapchain). The frame
before and after it is fast, so it is not the game's work; it looks like the hybrid graphics
(Intel display, NVIDIA rendering). It has not been looked at on another machine.

## Known limits

- One storey: navigation has a single height.
- Light probes are blended across walls without a visibility test: a little light can come
  through a thin wall from the next room.
- The dying clips still clip a little: the orderly's apron sinks through the floor while he kneels,
  the patient's hand passes through the head for a few frames of the fall.
- The sounds are synthesised and have not been mixed by ear.
