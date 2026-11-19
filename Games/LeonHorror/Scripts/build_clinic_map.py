#!/usr/bin/env python3
"""
LeonHorror - builds Content/Maps/Clinic.lmap, the demo level.

    python Games/LeonHorror/Scripts/build_clinic_map.py            write the map
    python Games/LeonHorror/Scripts/build_clinic_map.py --bake     and bake its lightmaps (Production)

The floor plan is clinic_layout.py; the meshes come from the art build (Tools/build_art.py, run it
first). Edit this script, not the map.

The way through, for whoever changes it:
  Reception    wake here; a note, the typewriter (save), a pipe
  Corridor     a patient wanders; Consult 1 and the staff room are open
  Staff room   the key to Consult 2, the handgun, a second typewriter
  Consult 2    the pharmacy key
  Pharmacy     first aid and cartridges (worth the detour, not required)
  Ward         two patients among the beds; the store room key; a door through to X-ray
  Store room   the fuse
  Service      dark until the fuse is fitted in the plant room; an orderly walks it
  Plant room   the fuse box: power for the north wing
  Morgue       its door has an electric lock; the key to the ambulance bay is on a tray
  Theatre      an orderly, and what is left of the cartridges
  Bay          the way out

Everything is placed in plan coordinates (X east, Y north, Z up, metres) and converted on writing.
"""

from __future__ import annotations

import argparse
import math
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT_DIR = HERE.parent
ROOT = PROJECT_DIR.parent.parent
sys.path.insert(0, str(HERE))

import clinic_layout as plan  # noqa: E402
from leonmap import Level, from_blender, num  # noqa: E402

PROJECT = "Games/LeonHorror/LeonHorror.lproject"
T = plan.WALL_THICKNESS * 0.5
H = plan.ROOM_HEIGHT
DIFFUSER = "ClinicLightDiffuser"

# Yaw that puts a wall-hung prop's back on that wall, and one that makes a prop face away from it.
BACK_ON = {"north": 0.0, "south": 180.0, "east": -90.0, "west": 90.0}

WARM = (1.0, 0.93, 0.80)
COLD = (0.82, 0.92, 1.0)
SICK = (0.80, 1.0, 0.84)


class Room:
    """A room of the plan, with coordinates measured from its south-west inner corner."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.x0, self.y0, self.x1, self.y1 = plan.ROOMS[name]["rect"]
        self.width = self.x1 - self.x0
        self.depth = self.y1 - self.y0
        self.ceiling_tiles = plan.ROOMS[name]["ceiling"] == "ClinicCeilingTile"

    def at(self, dx: float, dy: float, z: float = 0.0):
        """A point dx metres east and dy metres north of the south-west corner of the room."""
        return (self.x0 + dx, self.y0 + dy, z)

    def wall(self, side: str, along: float, z: float = 0.0):
        """A point on the inner face of a wall, `along` metres from its west or south end."""
        if side == "north":
            return (self.x0 + along, self.y1 - T, z)
        if side == "south":
            return (self.x0 + along, self.y0 + T, z)
        if side == "east":
            return (self.x1 - T, self.y0 + along, z)
        return (self.x0 + T, self.y0 + along, z)

    @property
    def centre(self):
        return ((self.x0 + self.x1) * 0.5, (self.y0 + self.y1) * 0.5, 0.0)


def rotate(vector, yaw: float):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (vector[0] * c - vector[1] * s, vector[0] * s + vector[1] * c)


class Clinic:
    def __init__(self, quality: str) -> None:
        self.lv = Level("Clinic", PROJECT_DIR, quality)
        self.rooms = {name: Room(name) for name in plan.ROOMS}
        self.counts: dict[str, int] = {}

    # ------------------------------------------------------------------ helpers
    def name(self, stem: str) -> str:
        self.counts[stem] = self.counts.get(stem, 0) + 1
        return f"{stem}_{self.counts[stem]:02d}"

    def prop(self, mesh: str, point, yaw: float = 0.0, *, collision: bool = True, cast_shadows: bool = True) -> None:
        """A fixed piece of furniture: baked, solid."""
        self.lv.static_mesh(self.name(mesh.replace("SM_Prop_", "").replace("SM_", "")), mesh, from_blender(point), yaw, collision=collision,
                            cast_shadows=cast_shadows)

    def wall_prop(self, room: str, mesh: str, side: str, along: float, z: float = 0.0, *, collision: bool = False) -> None:
        self.prop(mesh, self.rooms[room].wall(side, along, z), BACK_ON[side], collision=collision)

    def pickup(self, name: str, mesh: str, point, item: str, count: int = 1, yaw: float = 0.0) -> None:
        props = {"Item": item}
        if count != 1:
            props["Count"] = str(count)
        self.lv.movable_mesh(name, mesh, from_blender(point), yaw, cls="ALeonHorrorPickup", props=props, collision=False)

    def note(self, name: str, point, title: str, text: str, yaw: float = 0.0) -> None:
        self.lv.movable_mesh(name, "SM_Item_Note", from_blender(point), yaw, cls="ALeonHorrorNote", props={"Title": title, "Text": text},
                             collision=False)

    def enemy(self, name: str, point, kind: str, wander: float) -> None:
        self.lv.actor(name, "ALeonHorrorEnemySpawn", from_blender(point), props={"Type": kind, "WanderRadius": num(wander)})

    def sound(self, name: str, point, sound: str, volume: float, radius: float, group: str = "") -> None:
        props = {"Sound": f"/Game/Audio/{sound}", "Volume": num(volume), "Radius": num(radius)}
        if group:
            props["PowerGroup"] = group
        self.lv.actor(name, "ALeonHorrorAmbientSound", from_blender(point), props=props)

    def light(self, room: str, dx: float, dy: float, *, color=WARM, intensity: float = 4.5, radius: float = 6.5, state: str = "on",
              group: str = "", shadows: bool = False) -> None:
        """
        A ceiling fitting and the light it gives. state: "on", "flicker" (a tube on its way out) or
        "dead" (the fitting is there and gives nothing). `group` puts it on a power group.
        """
        r = self.rooms[room]
        mesh = "SM_Light_Panel" if r.ceiling_tiles else "SM_Light_Tube"
        index = self.name(f"Light_{room}")
        point = r.at(dx, dy, H)
        # Each fitting has its own copy of the diffuser material, so it can go dark by itself.
        fitting_group = "Dead" if state == "dead" else group
        fitting_props = {"PowerGroup": fitting_group} if fitting_group else None
        self.lv.movable_mesh(index + "_Fitting", mesh, from_blender(point), props=fitting_props,
                             overrides={self.lv.material_slot(mesh, DIFFUSER): DIFFUSER}, collision=False)
        if state == "dead":
            return
        props = {}
        if group:
            props["PowerGroup"] = group
        if state == "flicker":
            props["Flicker"] = "0.8"
        self.lv.point_light(index, from_blender((point[0], point[1], H - 0.28)), color, intensity, radius, cast_shadows=shadows,
                            props=props or None)
        hum = "AMB_Fluorescent_Flicker" if state == "flicker" else "AMB_Fluorescent_Hum"
        self.sound(index + "_Hum", (point[0], point[1], H - 0.3), hum, 0.35 if state == "on" else 0.55, 5.0, group)

    # ------------------------------------------------------------------ the building
    def shells(self) -> None:
        for room in plan.ROOMS:
            for part in ("Floor", "Walls", "Ceiling", "Skirting"):
                self.lv.static_mesh(f"{room}_{part}", f"SM_Clinic_{room}_{part}", collision=part != "Skirting")

    def probes(self) -> None:
        """One reflection probe a room: its box is the room, seen from head height near the middle."""
        # Where the middle of the room is inside furniture, the room is seen from beside it (east, north).
        aside = {"Theatre": (1.4, -1.2), "XRay": (-1.4, 0.0), "Reception": (0.0, -1.4), "Ward": (0.0, 0.3)}
        for name, room in self.rooms.items():
            centre = (room.centre[0], room.centre[1], H * 0.5)
            east, north = aside.get(name, (0.0, 0.0))
            # Engine axes: X east, Y up, Z south.
            self.lv.reflection_probe(f"Probe_{name}", from_blender(centre), (room.width * 0.5, H * 0.5, room.depth * 0.5),
                                     capture_offset=(east, 1.55 - H * 0.5, -north))

    def doors(self) -> None:
        locks = {"Consult2": "Key.Consult", "Pharmacy": "Key.Pharmacy", "Storage": "Key.Storage", "Bay": "Key.Bay"}
        powered = {"Morgue": "North"}
        for opening in plan.OPENINGS:
            kind = opening["kind"]
            if kind == "open":
                continue
            name = opening["name"]
            axis, value, _, _ = plan.shared_edge(*opening["rooms"])
            width, _ = plan.opening_size(opening)
            centre = (value, opening["at"], 0.0) if axis == "x" else (opening["at"], value, 0.0)
            yaw = 90.0 if axis == "x" else 0.0
            across = rotate((1.0, 0.0), yaw)

            frame = "SM_DoorFrame_Double" if kind == "double" else "SM_DoorFrame_Single"
            self.lv.static_mesh(f"Door_{name}_Frame", frame, from_blender(centre), yaw, collision=False)

            # Leaves open into the second room of the opening (away from the corridor, as a rule).
            target = self.rooms[opening["rooms"][1]].centre
            into = (target[0] - centre[0], target[1] - centre[1])

            def leaf(suffix: str, hinge, leaf_yaw: float, leaf_width: float, mesh: str, swing: float, partner: str = "") -> None:
                back = rotate((0.0, 1.0), leaf_yaw)
                sign = 1.0 if back[0] * into[0] + back[1] * into[1] > 0.0 else -1.0
                props = {"Swing": num(sign * swing), "Width": num(leaf_width), "Thickness": "0.05",
                         "Sound": {"single": "Wood", "double": "Swing", "metal": "Metal"}[kind]}
                if partner:
                    props["Partner"] = partner
                if name in locks:
                    props["RequiredKey"] = locks[name]
                if name in powered:
                    props["PowerGroup"] = powered[name]
                self.lv.movable_mesh(f"Door_{name}{suffix}", mesh, from_blender(hinge), leaf_yaw, cls="ALeonHorrorDoor", props=props)

            left = (centre[0] - across[0] * width * 0.5, centre[1] - across[1] * width * 0.5, 0.0)
            right = (centre[0] + across[0] * width * 0.5, centre[1] + across[1] * width * 0.5, 0.0)
            if kind == "double":
                leaf("_A", left, yaw, width * 0.5, "SM_Door_Swing", 88.0, f"Door_{name}_B")
                leaf("_B", right, yaw + 180.0, width * 0.5, "SM_Door_Swing", 88.0, f"Door_{name}_A")
            else:
                leaf("", left, yaw, width, "SM_Door_Metal" if kind == "metal" else "SM_Door_Single", 95.0)

    # ------------------------------------------------------------------ rooms
    def reception(self) -> None:
        r = self.rooms["Reception"]
        self.lv.player_start(from_blender(r.at(2.2, 2.3, 1.0)))
        # The counter faces the entrance; staff sit north of it.
        self.prop("SM_Prop_ReceptionDesk", r.at(3.6, 3.4), 0.0)
        self.prop("SM_Prop_Chair", r.at(3.2, 4.3), 20.0)
        self.prop("SM_Prop_FilingCabinet", r.at(0.45, 5.55), 90.0)
        self.prop("SM_Prop_FilingCabinet", r.at(0.45, 5.0), 90.0)
        self.wall_prop("Reception", "SM_Prop_Clock", "north", 2.0, 2.2)
        self.wall_prop("Reception", "SM_Prop_Extinguisher", "west", 2.4, 0.25)
        self.wall_prop("Reception", "SM_Prop_Radiator", "south", 5.6, collision=True)
        self.prop("SM_Prop_Bench", r.at(1.0, 0.45), 180.0)
        self.prop("SM_Prop_Boxes", r.at(7.85, 0.6), 30.0)
        self.prop("SM_Prop_Bin", r.at(5.3, 3.9), 0.0)
        self.prop("SM_Prop_Typewriter", r.at(2.9, 3.72, 0.74), 180.0, collision=False)
        self.lv.actor("SavePoint_Reception", "ALeonHorrorSavePoint", from_blender(r.at(2.9, 3.75, 0.86)))
        self.note("Note_Reception", r.at(4.3, 3.28, 1.115), "Night log",
                  "22:40 - Power to the north wing has gone again. The fuse will not hold.\\n"
                  "Dr. Varga says leave the theatre and the cold room shut until morning.\\n"
                  "23:15 - The ward will not settle. Something is walking the corridor.\\n"
                  "I have locked the front. I am not going back down there.", yaw=12.0)
        self.pickup("Pickup_Pipe", "SM_Item_Pipe", r.at(6.9, 0.5, 0.0), "Weapon.Pipe", yaw=25.0)
        self.light("Reception", 2.4, 1.8, intensity=4.0)
        self.light("Reception", 6.0, 1.8, state="dead")
        self.light("Reception", 2.4, 4.4, state="flicker", intensity=4.5, shadows=True)
        self.light("Reception", 6.0, 4.4, state="dead")

    def waiting(self) -> None:
        r = self.rooms["Waiting"]
        for i, x in enumerate((1.3, 3.2)):
            self.prop("SM_Prop_Bench", r.at(x, 5.55), 0.0)
            self.prop("SM_Prop_Bench", r.at(x, 0.45), 180.0)
        self.prop("SM_Prop_Wheelchair", r.at(4.9, 2.6), 115.0)
        self.prop("SM_Prop_Bin", r.at(5.5, 5.5), 0.0)
        self.wall_prop("Waiting", "SM_Prop_Radiator", "east", 3.0, collision=True)
        self.prop("SM_Prop_Boxes", r.at(5.3, 0.6), -20.0)
        self.light("Waiting", 1.8, 3.0, state="dead")
        self.light("Waiting", 4.2, 3.0, color=SICK, intensity=2.6)
        self.sound("Sound_Waiting_Wind", r.at(5.6, 3.0, 1.6), "AMB_Wind_Window", 0.5, 9.0)

    def corridor(self) -> None:
        r = self.rooms["Corridor"]
        states = [("on", 4.2, True), ("dead", 0.0, False), ("flicker", 4.0, False), ("dead", 0.0, False), ("on", 3.4, True), ("dead", 0.0, False)]
        for i, (state, intensity, shadows) in enumerate(states):
            self.light("Corridor", 1.2, 1.5 + i * 3.0, state=state, intensity=intensity or 4.0, color=WARM if i < 3 else SICK, shadows=shadows)
        # Furniture left along the walls, clear of every doorway.
        self.prop("SM_Prop_Gurney", r.at(0.55, 4.5), 4.0)
        self.prop("SM_Prop_Wheelchair", r.at(1.85, 14.5), -160.0)
        self.prop("SM_Prop_IVStand", r.at(0.4, 9.6), 0.0)
        self.prop("SM_Prop_Trolley", r.at(0.42, 16.9), 80.0)
        self.wall_prop("Corridor", "SM_Prop_Extinguisher", "east", 11.4, 0.25)
        self.wall_prop("Corridor", "SM_Prop_Clock", "west", 4.6, 2.2)
        self.prop("SM_Prop_Boxes", r.at(1.95, 6.2), 60.0)
        self.enemy("EnemySpawn_Corridor", r.at(1.2, 12.5), "Patient", 5.0)

    def consult(self, room: str, mirrored: bool) -> None:
        r = self.rooms[room]
        self.prop("SM_Prop_Desk", r.at(1.6, r.depth - 0.9), 0.0)
        self.prop("SM_Prop_Chair", r.at(1.5, r.depth - 0.35), 0.0)
        self.prop("SM_Prop_Chair", r.at(1.8, r.depth - 2.0), 170.0)
        self.prop("SM_Prop_MedicineCabinet", r.at(0.3, 1.6), 90.0)
        self.prop("SM_Prop_FilingCabinet", r.at(0.45, r.depth - 0.5), 90.0)
        self.wall_prop(room, "SM_Prop_Sink", "south", 4.4, collision=True)
        self.wall_prop(room, "SM_Prop_XRayViewer", "west", 3.0, 1.35)
        # The examination couch along the south wall, with its curtain.
        self.prop("SM_Prop_Gurney", r.at(2.3, 0.55), 90.0)
        self.prop("SM_Prop_CurtainRail", r.at(2.3, 1.25), 0.0, collision=False)
        self.prop("SM_Prop_Bin", r.at(5.3, 0.5), 0.0)

    def consults(self) -> None:
        self.consult("Consult1", False)
        r = self.rooms["Consult1"]
        self.light("Consult1", 3.0, 2.25, intensity=3.6)
        self.pickup("Pickup_Medkit_Consult1", "SM_Item_Medkit", r.at(1.2, r.depth - 0.95, 0.75), "Medkit", yaw=15.0)
        self.note("Note_Consult1", r.at(1.95, r.depth - 0.9, 0.752), "Referral, unsent",
                  "Patient 14 no longer responds to his name. Reflexes exaggerated, pupils fixed.\\n"
                  "He does not sleep. He stands at the door of the ward and listens.\\n"
                  "Sedation has no effect at any dose. Recommend transfer. No reply from the county.", yaw=-8.0)

        self.consult("Consult2", True)
        r = self.rooms["Consult2"]
        self.light("Consult2", 3.0, 2.25, state="dead")
        self.pickup("Pickup_KeyPharmacy", "SM_Item_Key", r.at(1.25, r.depth - 0.92, 0.752), "Key.Pharmacy", yaw=40.0)
        self.pickup("Pickup_Ammo_Consult2", "SM_Item_Ammo", r.at(2.0, r.depth - 0.95, 0.752), "Ammo.Pistol", 6)
        self.enemy("EnemySpawn_Consult2", r.at(4.2, 2.4), "Patient", 2.0)

    def pharmacy(self) -> None:
        r = self.rooms["Pharmacy"]
        for x in (1.0, 2.2, 3.4):
            self.prop("SM_Prop_ShelfMetal", r.at(x, r.depth - 0.32), 0.0)
        self.prop("SM_Prop_MedicineCabinet", r.at(0.3, 1.0), 90.0)
        self.prop("SM_Prop_MedicineCabinet", r.at(0.3, 2.0), 90.0)
        self.prop("SM_Prop_Desk", r.at(4.6, 0.6), 180.0)
        self.prop("SM_Prop_Trolley", r.at(3.0, 2.0), 30.0)
        self.prop("SM_Prop_Boxes", r.at(5.3, 3.3), -15.0)
        self.light("Pharmacy", 1.8, 2.0, color=COLD, intensity=3.4)
        self.light("Pharmacy", 4.2, 2.0, state="dead")
        self.pickup("Pickup_Medkit_Pharmacy", "SM_Item_Medkit", r.at(4.3, 0.6, 0.752), "Medkit")
        self.pickup("Pickup_Medkit_Pharmacy2", "SM_Item_Medkit", r.at(3.0, 2.0, 0.846), "Medkit", yaw=30.0)
        self.pickup("Pickup_Ammo_Pharmacy", "SM_Item_Ammo", r.at(5.0, 0.62, 0.752), "Ammo.Pistol", 8, yaw=-20.0)

    def staff(self) -> None:
        r = self.rooms["Staff"]
        self.prop("SM_Prop_Locker", r.at(0.8, r.depth - 0.33), 0.0)
        self.prop("SM_Prop_Locker", r.at(1.5, r.depth - 0.33), 0.0)
        self.prop("SM_Prop_Locker", r.at(2.2, r.depth - 0.33), 0.0)
        self.prop("SM_Prop_Desk", r.at(1.4, 0.6), 180.0)
        self.prop("SM_Prop_Chair", r.at(1.5, 1.3), 190.0)
        self.prop("SM_Prop_Chair", r.at(4.0, 2.6), 60.0)
        self.prop("SM_Prop_Bench", r.at(0.45, 2.8), 90.0)
        self.wall_prop("Staff", "SM_Prop_Sink", "south", 4.6, collision=True)
        self.wall_prop("Staff", "SM_Prop_Radiator", "west", 1.6, collision=True)
        self.wall_prop("Staff", "SM_Prop_Clock", "south", 2.8, 2.2)
        self.prop("SM_Prop_Typewriter", r.at(1.05, 0.62, 0.75), 180.0, collision=False)
        self.lv.actor("SavePoint_Staff", "ALeonHorrorSavePoint", from_blender(r.at(1.05, 0.65, 0.87)))
        self.pickup("Pickup_KeyConsult", "SM_Item_Key", r.at(1.8, 0.6, 0.752), "Key.Consult", yaw=-30.0)
        self.pickup("Pickup_Pistol", "SM_Item_Pistol", r.at(0.45, 2.8, 0.44), "Weapon.Pistol", yaw=70.0)
        self.pickup("Pickup_Ammo_Staff", "SM_Item_Ammo", r.at(0.45, 3.2, 0.44), "Ammo.Pistol", 6)
        self.note("Note_Staff", r.at(1.45, 0.72, 0.752), "Taped to the desk",
                  "Marta - the spare for consulting room 2 is in my drawer, take it.\\n"
                  "The store room key went up to the ward with the night nurse and never came back.\\n"
                  "If the lights go in the north wing: fuses are in the store room, board is in the plant room.", yaw=6.0)
        self.light("Staff", 3.0, 2.5, intensity=3.8)
        self.sound("Sound_Staff_Radio", r.at(2.0, 0.6, 0.9), "AMB_Static_Radio", 0.45, 6.0)

    def ward(self) -> None:
        r = self.rooms["Ward"]
        # Three beds along each long wall, heads to the wall.
        for i, y in enumerate((1.3, 3.5, 5.7)):
            self.prop("SM_Prop_HospitalBed", r.at(r.width - 1.15, y), -90.0)
            self.prop("SM_Prop_IVStand", r.at(r.width - 0.5, y + 0.75), 0.0)
        for i, y in enumerate((3.9, 5.9)):
            self.prop("SM_Prop_HospitalBed", r.at(1.2, y), 90.0)
        self.prop("SM_Prop_CurtainRail", r.at(r.width - 1.3, 2.4), 0.0, collision=False)
        self.prop("SM_Prop_CurtainRail", r.at(r.width - 1.3, 4.6), 0.0, collision=False)
        self.prop("SM_Prop_Trolley", r.at(2.9, 5.0), 10.0)
        self.prop("SM_Prop_Monitor", r.at(2.9, 5.0, 0.846), 200.0, collision=False)
        self.prop("SM_Prop_Wheelchair", r.at(3.0, 1.2), 40.0)
        self.prop("SM_Prop_OxygenTank", r.at(0.4, 6.6), 0.0)
        self.wall_prop("Ward", "SM_Prop_Radiator", "east", 2.4, collision=True)
        self.light("Ward", 3.0, 1.6, state="dead")
        self.light("Ward", 3.0, 3.6, state="flicker", color=SICK, intensity=3.6, shadows=True)
        self.light("Ward", 3.0, 5.6, state="dead")
        self.pickup("Pickup_KeyStorage", "SM_Item_Key", r.at(r.width - 1.0, 5.7, 0.63), "Key.Storage", yaw=10.0)
        self.pickup("Pickup_Medkit_Ward", "SM_Item_Medkit", r.at(1.2, 5.9, 0.63), "Medkit", yaw=80.0)
        self.enemy("EnemySpawn_Ward_A", r.at(2.6, 2.6), "Patient", 3.0)
        self.enemy("EnemySpawn_Ward_B", r.at(3.4, 6.0), "Patient", 2.5)

    def xray(self) -> None:
        r = self.rooms["XRay"]
        self.prop("SM_Prop_OperatingTable", r.at(3.0, 2.2), 90.0)
        self.wall_prop("XRay", "SM_Prop_XRayViewer", "north", 2.0, 1.35)
        self.wall_prop("XRay", "SM_Prop_XRayViewer", "north", 3.0, 1.35)
        self.prop("SM_Prop_Desk", r.at(5.1, 3.7), 0.0)
        self.prop("SM_Prop_Chair", r.at(5.0, 3.1), 175.0)
        self.prop("SM_Prop_Monitor", r.at(5.3, 3.75, 0.75), 180.0, collision=False)
        self.prop("SM_Prop_FilingCabinet", r.at(0.45, 4.0), 90.0)
        self.prop("SM_Prop_Locker", r.at(0.33, 0.9), 90.0)
        self.light("XRay", 3.0, 2.25, color=COLD, intensity=2.4)
        self.pickup("Pickup_Ammo_XRay", "SM_Item_Ammo", r.at(4.7, 3.7, 0.752), "Ammo.Pistol", 6, yaw=35.0)
        self.note("Note_XRay", r.at(4.3, 3.72, 0.752), "Radiographer's note",
                  "Plates for patient 14 attached. I do not know what I am looking at.\\n"
                  "The long bones have thickened since Tuesday. The jaw is not the jaw on his admission plate.\\n"
                  "I showed the orderly. He laughed, and he has not spoken since.", yaw=-10.0)

    def toilets(self) -> None:
        r = self.rooms["Toilets"]
        for x in (0.8, 1.9, 3.0):
            self.wall_prop("Toilets", "SM_Prop_Toilet", "north", x, collision=True)
        for x in (4.3, 5.2):
            self.wall_prop("Toilets", "SM_Prop_Sink", "north", x, collision=True)
        self.prop("SM_Prop_Bin", r.at(5.6, 0.5), 0.0)
        self.light("Toilets", 3.0, 1.25, state="flicker", color=COLD, intensity=3.0)
        self.sound("Sound_Toilets_Drip", r.at(4.8, 2.0, 0.9), "AMB_Drip", 0.7, 8.0)
        self.pickup("Pickup_Ammo_Toilets", "SM_Item_Ammo", r.at(0.5, 0.5, 0.0), "Ammo.Pistol", 4, yaw=50.0)

    def storage(self) -> None:
        r = self.rooms["Storage"]
        for y in (0.9, 2.1, 3.3):
            self.prop("SM_Prop_ShelfMetal", r.at(r.width - 0.33, y), -90.0)
        for x in (1.6, 2.8):
            self.prop("SM_Prop_ShelfMetal", r.at(x, r.depth - 0.33), 0.0)
        self.prop("SM_Prop_Boxes", r.at(1.2, 1.0), 10.0)
        self.prop("SM_Prop_Boxes", r.at(2.6, 0.7), 100.0)
        self.prop("SM_Prop_Boxes", r.at(4.0, 2.2), -40.0)
        self.prop("SM_Prop_OxygenTank", r.at(0.4, 3.5), 0.0)
        self.prop("SM_Prop_OxygenTank", r.at(0.75, 3.55), 30.0)
        self.light("Storage", 3.0, 2.0, color=WARM, intensity=2.6)
        self.pickup("Pickup_Fuse", "SM_Item_Fuse", r.at(2.8, r.depth - 0.36, 0.992), "Fuse", yaw=20.0)
        self.pickup("Pickup_Medkit_Storage", "SM_Item_Medkit", r.at(1.6, r.depth - 0.36, 0.572), "Medkit")
        self.sound("Sound_Storage_Pipes", r.at(3.0, 2.0, 2.4), "AMB_Pipes", 0.6, 10.0)

    def service(self) -> None:
        r = self.rooms["Service"]
        for i, x in enumerate((1.8, 5.4, 9.0, 12.6)):
            self.light("Service", x, 1.2, color=COLD, intensity=3.4, group="North", state="flicker" if i == 2 else "on", shadows=i in (0, 3))
        self.prop("SM_Prop_Gurney", r.at(3.8, 0.5), 92.0)
        self.prop("SM_Prop_Trolley", r.at(10.6, 1.9), -30.0)
        self.prop("SM_Prop_Boxes", r.at(12.4, 0.5), 20.0)
        self.prop("SM_Prop_OxygenTank", r.at(0.4, 0.5), 0.0)
        self.wall_prop("Service", "SM_Prop_Extinguisher", "south", 9.6, 0.25)
        self.sound("Sound_Service_Vent", r.at(7.2, 1.2, 2.5), "AMB_Ventilation", 0.6, 12.0, "North")
        self.enemy("EnemySpawn_Service", r.at(9.5, 1.2), "Orderly", 4.0)

    def theatre(self) -> None:
        r = self.rooms["Theatre"]
        self.prop("SM_Prop_OperatingTable", r.at(3.0, 3.0), 0.0)
        self.prop("SM_Prop_SurgicalLamp", r.at(2.6, 3.0, H), 0.0, collision=False)
        self.prop("SM_Prop_Trolley", r.at(4.1, 3.4), 80.0)
        self.prop("SM_Prop_Trolley", r.at(1.8, 2.2), -15.0)
        self.prop("SM_Prop_IVStand", r.at(3.9, 4.2), 0.0)
        self.prop("SM_Prop_Monitor", r.at(4.1, 3.4, 0.846), -100.0, collision=False)
        self.prop("SM_Prop_MedicineCabinet", r.at(0.3, 4.6), 90.0)
        self.prop("SM_Prop_MedicineCabinet", r.at(0.3, 3.6), 90.0)
        self.wall_prop("Theatre", "SM_Prop_Sink", "north", 1.2, collision=True)
        self.wall_prop("Theatre", "SM_Prop_Sink", "north", 2.1, collision=True)
        self.prop("SM_Prop_OxygenTank", r.at(5.5, 5.4), 0.0)
        self.prop("SM_Prop_Bin", r.at(5.4, 0.6), 0.0)
        self.light("Theatre", 1.5, 3.0, color=COLD, intensity=3.0, group="North")
        self.light("Theatre", 4.5, 3.0, color=COLD, intensity=3.0, group="North", state="flicker")
        # The operating lamp throws a hard pool of light on the table once there is power.
        self.lv.spot_light("Light_Theatre_Lamp", from_blender(r.at(3.0, 3.0, H - 1.0)), (0, -1, 0), (0.9, 0.97, 1.0), 26.0, 5.0, 26.0, 40.0)
        self.pickup("Pickup_Ammo_Theatre", "SM_Item_Ammo", r.at(1.8, 2.2, 0.846), "Ammo.Pistol", 10, yaw=15.0)
        self.pickup("Pickup_Medkit_Theatre", "SM_Item_Medkit", r.at(4.1, 3.4, 0.24), "Medkit")
        self.enemy("EnemySpawn_Theatre", r.at(4.6, 1.6), "Orderly", 2.5)

    def morgue(self) -> None:
        r = self.rooms["Morgue"]
        self.wall_prop("Morgue", "SM_Prop_MorgueDrawers", "north", 2.0, collision=True)
        self.prop("SM_Prop_Gurney", r.at(1.0, 2.6), 5.0)
        self.prop("SM_Prop_Gurney", r.at(3.0, 2.0), -12.0)
        self.wall_prop("Morgue", "SM_Prop_Sink", "west", 1.2, collision=True)
        self.prop("SM_Prop_Bin", r.at(3.5, 0.5), 0.0)
        self.light("Morgue", 2.0, 2.0, color=COLD, intensity=2.8, group="North")
        self.light("Morgue", 2.0, 4.4, color=COLD, intensity=2.2, group="North", state="flicker")
        self.sound("Sound_Morgue_Fridge", r.at(2.0, 5.6, 1.2), "AMB_Fridge", 0.7, 9.0, "North")
        # On the tray that was left pulled out.
        self.pickup("Pickup_KeyBay", "SM_Item_Key", r.at(2.0, 5.2, 0.255), "Key.Bay", yaw=60.0)
        self.note("Note_Morgue", r.at(1.0, 2.6, 0.872), "Tag, hand written",
                  "No. 14. Time of death 02:10. Cause: withheld on Dr. Varga's instruction.\\n"
                  "Bay key returned with the body - the driver would not come in for it.\\n"
                  "02:50. The drawer is open and it is empty.", yaw=85.0)

    def plant(self) -> None:
        r = self.rooms["Plant"]
        self.prop("SM_Prop_Boiler", r.at(3.2, 4.6), 180.0)
        self.wall_prop("Plant", "SM_Prop_FuseBox", "west", 2.6)
        self.lv.actor("FuseBox_North", "ALeonHorrorFuseBox", from_blender(r.wall("west", 2.6)), props={"Group": "North", "RequiredItem": "Fuse"})
        self.prop("SM_Prop_ShelfMetal", r.at(1.0, r.depth - 0.33), 0.0)
        self.prop("SM_Prop_Boxes", r.at(1.0, 0.8), 0.0)
        self.prop("SM_Prop_OxygenTank", r.at(4.0, 0.5), 0.0)
        self.wall_prop("Plant", "SM_Prop_Radiator", "east", 1.5, collision=True)
        # The one light here is on the emergency circuit: enough to find the board by.
        self.light("Plant", 2.2, 2.4, color=(1.0, 0.55, 0.35), intensity=1.6, radius=5.0)
        self.sound("Sound_Plant_Generator", r.at(3.2, 4.6, 1.0), "AMB_Generator", 0.7, 12.0, "North")
        self.note("Note_Plant", r.at(1.0, r.depth - 0.36, 0.992), "Maintenance card",
                  "North wing board. One cartridge fuse, 60 A. Fit, then throw the lever.\\n"
                  "Feeds: service corridor, theatre, cold room and its door lock.\\n"
                  "Do NOT bridge it with wire again. - J.", yaw=5.0)

    def bay(self) -> None:
        r = self.rooms["Bay"]
        self.prop("SM_Prop_Gurney", r.at(1.3, 3.4), 85.0)
        self.prop("SM_Prop_Boxes", r.at(3.0, 3.4), 0.0)
        self.prop("SM_Prop_OxygenTank", r.at(0.4, 3.5), 0.0)
        self.light("Bay", 1.8, 2.0, color=(1.0, 0.5, 0.4), intensity=2.0, radius=6.0)
        # The way out: a steel door in the east wall.
        east = r.wall("east", 2.0)
        self.lv.static_mesh("Exit_Frame", "SM_DoorFrame_Single", from_blender((east[0] + T, east[1], 0.0)), 90.0, collision=False)
        self.lv.movable_mesh("Exit", "SM_Door_Metal", from_blender((east[0], east[1] - 0.45, 0.0)), 90.0, cls="ALeonHorrorExit")
        self.sound("Sound_Bay_Wind", r.at(3.2, 2.0, 1.4), "AMB_Wind_Window", 0.8, 9.0)

    # ------------------------------------------------------------------ all of it
    def build(self) -> Level:
        lv = self.lv
        lv.exposure = 1.05
        lv.fog = {"color": (0.018, 0.022, 0.024), "density": 0.035, "start": 3.5, "max_opacity": 0.8}
        lv.post_process = {
            "ToneMapper": "ACES",
            "VignetteIntensity": "0.38", "VignetteRadius": "0.5", "VignetteSoftness": "0.65",
            "FilmGrainIntensity": "0.028",
            # Cold shadows, slightly warm highlights, a little colour taken out.
            "Lift": "[0, 0.008, 0.014]", "Gain": "[1.02, 1, 0.97]",
            "Contrast": "1.06", "Saturation": "0.88", "ChromaticAberration": "0.002",
        }
        width = max(r.x1 for r in self.rooms.values())
        depth = max(r.y1 for r in self.rooms.values())
        # Doorways are 0.9 m. A cell is walkable when a box of the agent's radius fits around its centre,
        # so the doorway less twice the radius has to be wider than a cell for one to always land in it.
        lv.nav_bounds(from_blender((width * 0.5, depth * 0.5, 1.4)), (width + 2.0, 3.0, depth + 2.0), cell_size=0.25, agent_radius=0.25)
        lv.actor("Music", "ALeonHorrorAmbientSound", (0, 0, 0), props={"Sound": "/Game/Audio/MUS_Clinic_Ambience", "Volume": "0.5", "Music": "true"})

        self.shells()
        self.probes()
        self.doors()
        self.reception()
        self.waiting()
        self.corridor()
        self.consults()
        self.pharmacy()
        self.staff()
        self.ward()
        self.xray()
        self.toilets()
        self.storage()
        self.service()
        self.theatre()
        self.morgue()
        self.plant()
        self.bay()
        return lv


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the clinic level")
    parser.add_argument("--bake", action="store_true", help="bake lightmaps after writing the map")
    parser.add_argument("--quality", default="Production", choices=["Preview", "Draft", "Production"])
    args = parser.parse_args()

    plan.validate()
    level = Clinic(args.quality).build()
    path = level.write("Games/LeonHorror/Scripts/build_clinic_map.py")
    print(f"[OK] wrote {path} ({len(level.blocks)} actors)")
    if not args.bake:
        return 0
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    command = [sys.executable, str(ROOT / "Tools/bake_lightmaps.py"), "--project", PROJECT, "--map", "/Game/Maps/Clinic", "--force",
               "--quality", args.quality]
    return subprocess.run(command, cwd=ROOT, env=env).returncode


if __name__ == "__main__":
    sys.exit(main())
