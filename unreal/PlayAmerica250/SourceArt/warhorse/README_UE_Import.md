# Blueskin war horse — UE 5.8 import notes

Procedurally built in Blender 5.2. Units are centimetres, 1:1 with Unreal. Horse stands 160 cm at the withers.

## Files
| File | What it is |
|---|---|
| `SK_WarHorse.fbx` | Skeletal mesh + skeleton (39 bones), rest pose, no animation |
| `Animations/A_WarHorse_*.fbx` | One clip per file, skeleton only, 30 fps |
| `Textures/T_WarHorse_Body_BC.png` / `_ORM.png` | Coat, mane, tail, hooves, eyes (2048) |
| `Textures/T_WarHorse_Tack_BC.png` / `_ORM.png` | Saddle cloth, saddle, bridle, brass (1024) |
| `WarHorse_clips.json` | Frame counts, loop flags, ground speeds |
| `WarHorse_preview.glb` | Same asset in metres with all clips, for quick viewing |
| `WarHorse_source.blend` | Source scene, textures packed |

## Import
1. Drag `SK_WarHorse.fbx` into `Content/Characters/WarHorse`. Skeletal Mesh on, Import Animations off, Import Uniform Scale 1.0.
2. Import the four textures. On both `_ORM` textures turn sRGB off (Compression: Masks).
3. Make `M_WarHorse_Body` and `M_WarHorse_Tack`: BC -> Base Color, ORM.G -> Roughness, ORM.B -> Metallic (R is unused AO, all white).
   Assign to the two material slots.
4. Drag all `Animations/*.fbx` in together, pick `SK_WarHorse_Skeleton`, Import Mesh off.
5. Tick Loop on Idle, Walk, Trot, Canter, Gallop in whatever plays them.

The mesh faces +Y like the Mannequin, so rotate it -90 yaw inside the pawn.

## Clips (all in place, no root motion)
| Clip | Frames | Loops | Match this ground speed (UE units/s) |
|---|---|---|---|
| Idle | 120 | yes | 0 |
| Walk | 36 | yes | 143 |
| Trot | 20 | yes | 353 |
| Canter | 18 | yes | 550 (right lead) |
| Gallop | 14 | yes | 1029 (right lead) |
| Rear | 90 | no | starts and ends in the stand pose |
| HitReact | 24 | no | starts and ends in the stand pose |
| Death | 66 | no | ends lying on the left side |

Set a 1D blendspace on speed with Idle/Walk/Trot/Canter/Gallop at the speeds above and the hooves will not slide.

## Skeleton
`root > pelvis > spine_01..03 > neck_01..03 > head > ear_l/r`, `tail_01..05` under pelvis,
front legs `scapula > humerus > radius > cannon_f > pastern_f > hoof_f`, hind legs `femur > tibia > cannon_h > pastern_h > hoof_h` (`_l` / `_r`).
`rider_seat` is a non-deforming bone on top of the saddle, child of `spine_02`: attach the rider there.

## Known limits
- Stylised procedural model, about 97k triangles, one LOD. Generate LODs in the Skeletal Mesh editor.
- Mane has no bones; it follows the neck rigidly. The tail has five bones if you want Anim Dynamics on it.
- No face rig, no normal maps. Tack is part of the mesh as its own material section; hide that section for a bare horse.
- Animation is procedural keyframing, not motion capture.

## Project import (2026-11-09)

Imported assets: `/Game/PA250/Characters/WarHorse` (16 assets: mesh, skeleton,
four textures, two assigned materials, eight animation sequences).
Repeatable importer: `../../Scripts/import_warhorse.py`; existing assets are preserved.
Run with UnrealEditor-Cmd and `-run=pythonscript -script=<absolute script path> -unattended -nullrhi`.
Loop and ground-speed hints are stored as `WarHorse.Loop` and `WarHorse.SpeedCmS`
asset metadata; configure looping in the animation player when integrating gameplay.
No pawn, animation blueprint, or blendspace is created by the import.

The game module was unavailable during import, so assets were generated in a
content-only UE 5.8 project at the same /Game paths and copied byte-for-byte.
All clips were reloaded and checked for skeleton compatibility and duration.
Interchange reported invalid bind poses for the animation-only FBXs and used
the time-zero pose. Playback and skinning still need visual review in the editor.
