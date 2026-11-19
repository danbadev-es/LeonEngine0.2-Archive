# LeonHorror audio

Every `SFX_*`, `AMB_*`, `MUS_*` and `STG_*` file listed as generated below is synthesized by
`Games/LeonHorror/Scripts/make_audio.py` (no samples). Do not edit the WAVs by hand: change the
script and run it again; it is deterministic. This file is rewritten by the script as well
(`--readme`, or any full run).

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python Games/LeonHorror/Scripts/make_audio.py -- [--only Name,...] [--check]

All files are 44.1 kHz, 16-bit PCM. The engine resolves them by path without extension, for
example `/Game/Audio/SFX_Door_Open`.

- **mono one-shot**: positional effect. Peak about -3 dBFS.
- **mono loop**: positional emitter, loops seamlessly. Peak about -9 dBFS (a few sparse ones higher).
- **stereo loop**: non-positional bed, loops seamlessly. Peak about -9 dBFS.
- **stereo one-shot**: non-positional stinger.
- Numbered variants (`_01`, `_02`, ...) are takes of the same event: pick one at random, never the
  same twice in a row, and vary pitch by a few percent.
- *Volume* is a suggested playback gain (0..1) as a starting mix. *Radius* is the suggested audible
  radius in metres for positional playback; "- (2D)" means play it non-positionally.

The set was verified by measurement only (duration, peak, RMS, DC, spectral centroid, loop
closure). Nobody had listened to it when it was generated, so treat volumes and radii as first
guesses and expect to retune some sounds by ear.

| File | Purpose | Channels | Type | Duration (s) | Volume | Radius (m) |
|---|---|---|---|---|---|---|
| `SFX_Step_Vinyl_01` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Vinyl_02` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Vinyl_03` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Vinyl_04` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Vinyl_05` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Vinyl_06` | Player footstep, hard shoe on vinyl floor (pick a variant at random) | mono | one-shot | 0.35 | 0.50 | 9 |
| `SFX_Step_Tile_01` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_Step_Tile_02` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_Step_Tile_03` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_Step_Tile_04` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_Step_Tile_05` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_Step_Tile_06` | Player footstep on ceramic tile: brighter, with ring | mono | one-shot | 0.35 | 0.55 | 11 |
| `SFX_EnemyStep_01` | Enemy bare foot: wet slap and drag | mono | one-shot | 0.65 | 0.70 | 14 |
| `SFX_EnemyStep_02` | Enemy bare foot: wet slap and drag | mono | one-shot | 0.66 | 0.70 | 14 |
| `SFX_EnemyStep_03` | Enemy bare foot: wet slap and drag | mono | one-shot | 0.65 | 0.70 | 14 |
| `SFX_EnemyStep_04` | Enemy bare foot: wet slap and drag | mono | one-shot | 0.66 | 0.70 | 14 |
| `SFX_Door_Open` | Wooden door opens: handle click, latch, hinge creak | mono | one-shot | 1.49 | 0.80 | 16 |
| `SFX_Door_Close` | Door swings shut: air, latch slam, corridor tail | mono | one-shot | 1.42 | 0.85 | 20 |
| `SFX_Door_Locked` | Locked door: the handle rattles twice against the bolt | mono | one-shot | 1.05 | 0.80 | 14 |
| `SFX_Door_Unlock` | Key goes in, turns, the bolt withdraws | mono | one-shot | 1.46 | 0.80 | 14 |
| `SFX_Door_Swing` | Double swing ward door: two soft flaps on rubber edges | mono | one-shot | 1.59 | 0.75 | 14 |
| `SFX_Door_Metal` | Heavy security door: steel groan, then the clunk | mono | one-shot | 2.54 | 0.90 | 24 |
| `SFX_Pickup_Item` | Generic pickup: soft cloth and plastic handling | mono | one-shot | 0.42 | 0.60 | - (2D) |
| `SFX_Pickup_Key` | Key pickup: small metal jingle | mono | one-shot | 0.64 | 0.65 | - (2D) |
| `SFX_Pickup_Ammo` | Ammo pickup: cartridges shifting in a cardboard box | mono | one-shot | 0.40 | 0.65 | - (2D) |
| `SFX_Paper` | Reading a note: paper rustle | mono | one-shot | 0.94 | 0.60 | - (2D) |
| `SFX_Save` | Saving: an old typewriter clack and a soft tone | mono | one-shot | 1.35 | 0.75 | - (2D) |
| `SFX_Medkit` | Using a medkit: plastic case, velcro, an exhale | mono | one-shot | 1.86 | 0.70 | - (2D) |
| `SFX_Flashlight_On` | Flashlight switch on: press and spring return | mono | one-shot | 0.08 | 0.60 | - (2D) |
| `SFX_Flashlight_Off` | Flashlight switch off: lower, softer return | mono | one-shot | 0.08 | 0.60 | - (2D) |
| `SFX_Fuse_Insert` | Ceramic fuse pushed home into its clips, contact spark | mono | one-shot | 0.60 | 0.80 | 10 |
| `SFX_Power_On` | Power restored: breaker thrown, relays, tubes striking into a rising hum | mono | one-shot | 3.54 | 0.90 | 30 |
| `SFX_Pistol_Fire` | 9 mm pistol indoors: crack, body, tiled room tail | mono | one-shot | 1.17 | 1.00 | 45 |
| `SFX_Pistol_Reload` | Pistol reload: magazine out, magazine in, slide racked | mono | one-shot | 1.64 | 0.80 | 10 |
| `SFX_Pistol_Empty` | Pistol dry fire: the hammer falls on nothing | mono | one-shot | 0.08 | 0.70 | 6 |
| `SFX_Pipe_Swing` | Pipe swing: whoosh | mono | one-shot | 0.43 | 0.70 | 8 |
| `SFX_Pipe_Hit_Flesh` | Pipe hits a body: dull wet impact | mono | one-shot | 0.37 | 0.90 | 14 |
| `SFX_Pipe_Hit_Wall` | Pipe hits plaster: metal ring and crumbs | mono | one-shot | 1.06 | 0.90 | 22 |
| `SFX_Player_Hurt_01` | Player hurt: short breathy grunt | mono | one-shot | 0.46 | 0.85 | 12 |
| `SFX_Player_Hurt_02` | Player hurt: short breathy grunt | mono | one-shot | 0.51 | 0.85 | 12 |
| `SFX_Player_Hurt_03` | Player hurt: short breathy grunt | mono | one-shot | 0.42 | 0.85 | 12 |
| `SFX_Player_Death` | Player death: a falling groan that runs out of air | mono | one-shot | 2.26 | 0.90 | 14 |
| `SFX_Heartbeat` | Low health heartbeat, 70 bpm, non-positional loop (4 beats) | mono | loop | 3.43 | 0.70 | - (2D) |
| `SFX_Breath_Tired` | Out of stamina: fast open-mouth breathing, non-positional loop (3 breaths) | mono | loop | 4.80 | 0.60 | - (2D) |
| `SFX_Patient_Idle_01` | Patient idle: wheeze and throat clicks | mono | one-shot | 1.61 | 0.70 | 14 |
| `SFX_Patient_Idle_02` | Patient idle: wheeze and throat clicks | mono | one-shot | 1.35 | 0.70 | 14 |
| `SFX_Patient_Idle_03` | Patient idle: wheeze and throat clicks | mono | one-shot | 1.70 | 0.70 | 14 |
| `SFX_Patient_Alert` | Patient notices the player: rising rasp | mono | one-shot | 1.70 | 0.85 | 22 |
| `SFX_Patient_Attack_01` | Patient attacks: shriek-rasp | mono | one-shot | 1.22 | 0.95 | 24 |
| `SFX_Patient_Attack_02` | Patient attacks: shriek-rasp | mono | one-shot | 1.29 | 0.95 | 24 |
| `SFX_Patient_Hurt` | Patient hit: a torn yelp | mono | one-shot | 0.84 | 0.85 | 20 |
| `SFX_Patient_Death` | Patient dies: the rasp falls apart into clicks and air | mono | one-shot | 2.35 | 0.90 | 22 |
| `SFX_Orderly_Idle_01` | Orderly idle: low laboured breathing through a mask | mono | one-shot | 2.32 | 0.75 | 16 |
| `SFX_Orderly_Idle_02` | Orderly idle: low laboured breathing through a mask | mono | one-shot | 2.46 | 0.75 | 16 |
| `SFX_Orderly_Idle_03` | Orderly idle: low laboured breathing through a mask | mono | one-shot | 2.35 | 0.75 | 16 |
| `SFX_Orderly_Alert` | Orderly notices the player: low growl | mono | one-shot | 2.03 | 0.90 | 24 |
| `SFX_Orderly_Attack_01` | Orderly attacks: grunt with effort | mono | one-shot | 1.19 | 0.95 | 24 |
| `SFX_Orderly_Attack_02` | Orderly attacks: grunt with effort | mono | one-shot | 1.29 | 0.95 | 24 |
| `SFX_Orderly_Hurt` | Orderly hit: an abrupt low grunt | mono | one-shot | 0.96 | 0.90 | 20 |
| `SFX_Orderly_Death` | Orderly dies: a groan that sinks into a long masked exhale | mono | one-shot | 2.76 | 0.95 | 24 |
| `AMB_Fluorescent_Hum` | Fluorescent tube: mains hum with harmonics and a faint buzz | mono | loop | 5.00 | 0.35 | 6 |
| `AMB_Fluorescent_Flicker` | Failing tube: hum with irregular ticks and dropouts (pair with a flickering light) | mono | loop | 8.00 | 0.40 | 7 |
| `AMB_Ventilation` | Air duct: rumble, hiss and a faint fan tone | mono | loop | 6.00 | 0.45 | 10 |
| `AMB_Drip` | Water dripping into a puddle, small tiled room | mono | loop | 12.00 | 0.50 | 8 |
| `AMB_Fridge` | Fridge compressor hum (morgue, pharmacy) | mono | loop | 6.00 | 0.40 | 6 |
| `AMB_Generator` | Idling generator in the utility room | mono | loop | 6.00 | 0.60 | 14 |
| `AMB_Wind_Window` | Wind through a cracked window: gusts and a thin whistle | mono | loop | 8.00 | 0.50 | 9 |
| `AMB_Pipes` | Plumbing in the walls: occasional knocks and a groan | mono | loop | 10.00 | 0.55 | 12 |
| `AMB_Static_Radio` | Radio static with faint ghostly carrier tones | mono | loop | 6.00 | 0.45 | 7 |
| `MUS_Clinic_Ambience` | Main ambience: dark evolving drones, distant metallic resonances, no melody | stereo | loop | 32.00 | 0.50 | - (2D) |
| `MUS_Chase` | Chase: low throb at 150 bpm under endlessly rising dissonant layers | stereo | loop | 16.00 | 0.60 | - (2D) |
| `MUS_Safe_Room` | Save room: calm, warm low pad | stereo | loop | 20.00 | 0.50 | - (2D) |
| `STG_Enemy_Spotted` | Enemy spotted: reverse swell into a sharp dissonant hit | stereo | one-shot | 3.17 | 0.80 | - (2D) |
| `STG_Death` | Player died: a low hit and a cluster sinking an octave | stereo | one-shot | 3.78 | 0.80 | - (2D) |
| `STG_Completed` | Demo completed: a suspended chord resolves; relief | stereo | one-shot | 4.41 | 0.80 | - (2D) |
| `SFX_DamageTaken` | Earlier prototype: player damage (not generated by the script) | mono | one-shot | 0.20 | - | - |
| `SFX_EmptyClick` | Earlier prototype: empty weapon click (not generated by the script) | mono | one-shot | 0.06 | - | - |
| `SFX_Footstep` | Earlier prototype: footstep (not generated by the script) | mono | one-shot | 0.10 | - | - |
| `SFX_HitConfirm` | Earlier prototype: hit confirmation (not generated by the script) | mono | one-shot | 0.11 | - | - |
| `SFX_RifleFire` | Earlier prototype: rifle shot (not generated by the script) | mono | one-shot | 0.28 | - | - |
| `SFX_RifleReload` | Earlier prototype: rifle reload (not generated by the script) | mono | one-shot | 0.55 | - | - |
| `SFX_UIClick` | Earlier prototype: UI click (not generated by the script) | mono | one-shot | 0.20 | - | - |
