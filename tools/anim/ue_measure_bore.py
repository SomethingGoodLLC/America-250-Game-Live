"""Measure the cannon barrel's real centre line from the mesh's vertices (read-only).

The mesh is one piece and the barrel is not level. In slices along X this logs, for the vertices within 6 cm of
the centre plane (|y| < 6), the highest and lowest Z, and the half-width of everything within 6 cm of that top.
Near the muzzle nothing hangs under the barrel, so top and bottom there give its centre and radius.
  UnrealEditor-Cmd <project> -run=pythonscript -script=tools/anim/ue_measure_bore.py -unattended -nullrhi -nosplash
"""
import unreal

mesh = unreal.load_asset("/Game/ThirdParty/PTM_Cannon/Mesh")
desc = mesh.get_static_mesh_description(0)
pts = []
for i in range(desc.get_vertex_count()):
    vid = unreal.VertexID()
    vid.set_editor_property("id_value", i)
    if not desc.is_vertex_valid(vid):
        continue
    p = desc.get_vertex_position(vid)
    pts.append((p.x, p.y, p.z))
unreal.log("PA250 bore: %d vertices" % len(pts))
for x0 in range(-20, 160, 15):
    band = [p for p in pts if x0 <= p[0] < x0 + 15]
    mid = [p for p in band if abs(p[1]) < 6.0]
    if not mid:
        unreal.log("PA250 bore: x %4d..%4d no vertices near the centre plane (%d in band)" % (x0, x0 + 15, len(band)))
        continue
    top = max(p[2] for p in mid)
    low = min(p[2] for p in mid)
    cap = [abs(p[1]) for p in band if p[2] > top - 6.0 and abs(p[1]) < 30.0]
    unreal.log("PA250 bore: x %4d..%4d  top z %.1f  lowest z %.1f  half-width near the top %.1f  (%d centre vertices)" % (
        x0, x0 + 15, top, low, max(cap) if cap else 0.0, len(mid)))
