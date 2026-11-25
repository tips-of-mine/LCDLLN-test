# Blender en arrière-plan : ajuste un maillage TRELLIS.2 (tête, coiffure, barbe,
# moustache sur un buste) au gabarit des corps LCDLLN et l'exporte en nuage
# compact pour trellis_import.py.
#
# Usage : blender -b --factory-startup --python fit_high.py -- params.json
#
# params.json :
#   source       chemin du .glb TRELLIS (couleurs par sommet)
#   name         nom de la pièce ; sortie : <out_dir>/<name>_high.npz
#   out_dir      dossier de sortie
#   target       gabarit du genre (espace Blender, mètres) : crown (sommet du
#                crâne), nose_y / nose_z (pointe du nez), temple_w (largeur du
#                front à 60 % entre nez et sommet), back_y (arrière du crâne à
#                cette hauteur)
#   cluster_div  regroupement des sommets sur une grille de (dimension / div)
#                quand le maillage dépasse cluster_min_faces (défaut 2 000 000)
#
# Sortie .npz : v (sommets, float32), f (triangles, int32), c (couleur par
# sommet 0..1, float16, valeurs sRGB telles qu'écrites par TRELLIS).
import bpy
import json
import math
import sys
import time

import numpy as np
from mathutils import Matrix

t0 = time.time()
P = json.load(open(sys.argv[sys.argv.index("--") + 1], encoding="utf-8"))
T = P["target"]
log = lambda *a: print("[fit]", *a, f"({time.time() - t0:.0f}s)", flush=True)  # noqa: E731

# ---- Import (glTF Y haut -> Z haut appliqué aux sommets) ----
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=P["source"])
objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in bpy.context.scene.objects:
    o.select_set(o in objs)
bpy.context.view_layer.objects.active = objs[0]
if len(objs) > 1:
    bpy.ops.object.join()
hi = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
me = hi.data
nv = len(me.vertices)
V = np.empty(nv * 3, np.float32); me.vertices.foreach_get("co", V); V = V.reshape(-1, 3).astype(np.float64)
lt = np.empty(len(me.polygons), np.int32); me.polygons.foreach_get("loop_total", lt)
ls = np.empty(len(me.polygons), np.int32); me.polygons.foreach_get("loop_start", ls)
lv = np.empty(len(me.loops), np.int32); me.loops.foreach_get("vertex_index", lv)
b3 = ls[lt == 3]
F = np.stack([lv[b3], lv[b3 + 1], lv[b3 + 2]], 1).astype(np.int64)
ca = me.color_attributes[0]
C = np.empty(len(ca.data) * 4, np.float32); ca.data.foreach_get("color", C); C = C.reshape(-1, 4)[:, :3]
if ca.domain == "CORNER":   # couleurs de coin -> sommets
    Cv = np.zeros((nv, 3), np.float32); cnt = np.zeros(nv, np.float32)
    np.add.at(Cv, lv, C); np.add.at(cnt, lv, 1); C = Cv / np.maximum(cnt, 1)[:, None]
log("import", nv, "sommets", len(F), "triangles")

# ---- Regroupement des sommets (maillages TRELLIS de plusieurs dizaines de
# millions de triangles ; la décimation de Blender y prend plus de 15 min) ----
if P.get("cluster_div") and len(F) > P.get("cluster_min_faces", 2000000):
    cell = float((V.max(0) - V.min(0)).max()) / P["cluster_div"]
    key = np.floor((V - V.min(0)) / cell).astype(np.int64)
    kk = key[:, 0] * 1_000_003 * 1_000_003 + key[:, 1] * 1_000_003 + key[:, 2]
    _uk, inv = np.unique(kk, return_inverse=True)
    n = np.bincount(inv).astype(np.float64)
    V = np.stack([np.bincount(inv, V[:, i]) for i in range(3)], 1) / n[:, None]
    C = np.stack([np.bincount(inv, C[:, i]) for i in range(3)], 1) / n[:, None]
    F = inv[F]
    F = F[(F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])]
    log("regroupement", len(V), "sommets", len(F), "triangles")

# ---- Repère : TRELLIS regarde vers -Y ; le corps vers +Y ----
V = V @ np.array(Matrix.Rotation(math.pi, 3, "Z")).T

# ---- Repères anatomiques (buste : la tête occupe ~80 % de la hauteur) ----
zmax, zmin = V[:, 2].max(), V[:, 2].min()
H = zmax - zmin
W = V[:, 0].max() - V[:, 0].min()
cx = float(np.median(V[V[:, 2] > zmax - 0.3 * H][:, 0]))
sel = (np.abs(V[:, 0] - cx) < 0.05 * W) & (V[:, 2] < zmax - 0.25 * H) & (V[:, 2] > zmax - 0.8 * H)
nose = V[sel][V[sel][:, 1].argmax()]
h = zmax - nose[2]
band = V[np.abs(V[:, 2] - (nose[2] + 0.6 * h)) < 0.01 * h]       # front, au-dessus des oreilles
temple_w = band[:, 0].max() - band[:, 0].min()
cx = 0.5 * float(band[:, 0].max() + band[:, 0].min())           # axe de symétrie
s = (T["crown"] - T["nose_z"]) / h
sw = float(np.clip((T["temple_w"] / temple_w) / s, 0.8, 1.1))   # correction de largeur bornée
S = np.diag([s * sw, s, s])
V = (V - np.array([cx, 0, 0])) @ S
nose2 = (nose - np.array([cx, 0, 0])) @ S
band2 = (band - np.array([cx, 0, 0])) @ S
t = np.array([0.0, T["back_y"] - band2[:, 1].min(), T["nose_z"] - nose2[2]])
V += t
log("repères", dict(h=round(float(h), 4), s=round(float(s), 4), sw=round(sw, 3)), "nez", (nose2 + t).round(4).tolist())

np.savez_compressed(f"{P['out_dir']}/{P['name']}_high.npz", v=V.astype(np.float32), f=F.astype(np.int32),
                    c=np.clip(C, 0, 1).astype(np.float16))
log("OK", len(V), "sommets", len(F), "triangles")
