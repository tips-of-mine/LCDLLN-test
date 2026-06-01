#!/usr/bin/env python3
# Genere une foret riveraine DENSE dans config.json > world.scenery :
#   - arbres (4 essences) : solides (collision tronc),
#   - sous-bois (buissons, fougeres, fleurs, champignons, rochers, plantes, trefles) :
#     les rochers sont solides, le reste est traversable (solid=false).
#
# Disperse autour du bassin d'eau (centre 55,0 ; rect x[15,95] z[-40,40]) et sur les
# terres environnantes. Exclut la surface d'eau et le voisinage du spawn (0,0), du
# villageois (4,0) et du coffre (-4,0). Deterministe (graine fixe). Idempotent :
# remplace le bloc world.scenery existant (insertion TEXTE ciblee).
#
# Lancement : python tools/world_gen/scatter_forest.py [--trees 160] [--decor 150]
import json
import math
import os
import random
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFIG = os.path.join(REPO, "config.json")

# Arbres : (mesh, rayon de tronc pour la collision en metres).
TREES = (
    [(f"meshes/props/CommonTree_{i}.gltf", 0.6) for i in range(1, 6)] +
    [(f"meshes/props/Pine_{i}.gltf",       0.5) for i in range(1, 6)] +
    [(f"meshes/props/DeadTree_{i}.gltf",   0.5) for i in range(1, 6)] +
    [(f"meshes/props/TwistedTree_{i}.gltf", 0.8) for i in range(1, 6)]
)

# Sous-bois : (mesh, solid, collision_radius, scale_min, scale_max). Poids = nb d'entrees.
DECOR = (
    [("meshes/props/Bush_Common.gltf",         False, 0.0, 0.8, 1.3)] * 3 +
    [("meshes/props/Bush_Common_Flowers.gltf", False, 0.0, 0.8, 1.3)] * 2 +
    [("meshes/props/Fern_1.gltf",              False, 0.0, 0.7, 1.4)] * 4 +
    [("meshes/props/Plant_1.gltf",             False, 0.0, 0.7, 1.3)] * 2 +
    [("meshes/props/Plant_1_Big.gltf",         False, 0.0, 0.7, 1.2)] * 1 +
    [("meshes/props/Plant_7.gltf",             False, 0.0, 0.7, 1.3)] * 2 +
    [("meshes/props/Flower_3_Group.gltf",      False, 0.0, 0.8, 1.4)] * 2 +
    [("meshes/props/Flower_4_Group.gltf",      False, 0.0, 0.8, 1.4)] * 2 +
    [("meshes/props/Clover_1.gltf",            False, 0.0, 0.8, 1.4)] * 2 +
    [("meshes/props/Clover_2.gltf",            False, 0.0, 0.8, 1.4)] * 2 +
    [("meshes/props/Mushroom_Common.gltf",     False, 0.0, 0.8, 1.5)] * 2 +
    [("meshes/props/Mushroom_Laetiporus.gltf", False, 0.0, 0.8, 1.3)] * 1 +
    [("meshes/props/Rock_Medium_1.gltf",       True,  1.0, 0.8, 1.6)] * 1 +
    [("meshes/props/Rock_Medium_2.gltf",       True,  1.0, 0.8, 1.6)] * 1 +
    [("meshes/props/Rock_Medium_3.gltf",       True,  1.0, 0.8, 1.6)] * 1
)

WATER_CX, WATER_CZ, WATER_HALF = 55.0, 0.0, 40.0
EXCLUDE = [(0.0, 0.0, 6.0), (4.0, 0.0, 6.0), (-4.0, 0.0, 6.0)]  # spawn, villageois, coffre
AREA = (-25.0, 125.0, -75.0, 75.0)  # xmin, xmax, zmin, zmax
SEED = 20260531


def in_water(x, z):
    return abs(x - WATER_CX) <= WATER_HALF and abs(z - WATER_CZ) <= WATER_HALF


def excluded(x, z):
    for ex, ez, er in EXCLUDE:
        if (x - ex) ** 2 + (z - ez) ** 2 < er * er:
            return True
    return False


def scatter(rng, count, min_dist, placed_ref):
    # Place `count` points en respectant min_dist vis-a-vis de placed_ref (liste de
    # (x,z) deja places dans le meme groupe). Retourne la liste de (x,z).
    out = []
    md2 = min_dist * min_dist
    attempts = 0
    while len(out) < count and attempts < count * 400:
        attempts += 1
        x = rng.uniform(AREA[0], AREA[1])
        z = rng.uniform(AREA[2], AREA[3])
        if in_water(x, z) or excluded(x, z):
            continue
        if any((x - px) ** 2 + (z - pz) ** 2 < md2 for px, pz in out):
            continue
        out.append((x, z))
    return out


def build_block(entries):
    # entries : liste de dict {mesh,x,z,yaw_deg,scale,collision_radius,solid}.
    lines = []
    lines.append('        "_comment_scenery": "Decor solide/non-solide (foret riveraine dense + sous-bois). '
                 'Genere par tools/world_gen/scatter_forest.py (deterministe). Chaque index i : mesh, '
                 'x/z (metres monde, Y au sol au runtime), yaw_deg, scale, collision_radius (cylindre, m), '
                 'solid (false = traversable : herbe/fougeres/fleurs ; true = bloque : arbres, rochers).",')
    lines.append('        "scenery": {')
    lines.append('            "count": %d,' % len(entries))
    for i, e in enumerate(entries):
        last = (i == len(entries) - 1)
        lines.append(
            '            "%d": { "mesh": "%s", "x": %.2f, "z": %.2f, "yaw_deg": %.1f, "scale": %.2f, "collision_radius": %.2f, "solid": %s }%s'
            % (i, e["mesh"], e["x"], e["z"], e["yaw_deg"], e["scale"], e["collision_radius"],
               "true" if e["solid"] else "false", "" if last else ",")
        )
    lines.append('        },')
    return "\n".join(lines) + "\n"


def main():
    n_trees = 160
    n_decor = 150
    if "--trees" in sys.argv:
        n_trees = int(sys.argv[sys.argv.index("--trees") + 1])
    if "--decor" in sys.argv:
        n_decor = int(sys.argv[sys.argv.index("--decor") + 1])

    rng = random.Random(SEED)
    entries = []

    # Arbres (min_dist 2.2 -> plus dense qu'avant).
    tree_xy = scatter(rng, n_trees, 2.2, [])
    for (x, z) in tree_xy:
        mesh, trunk = rng.choice(TREES)
        entries.append({"mesh": mesh, "x": x, "z": z, "yaw_deg": rng.uniform(0, 360),
                        "scale": rng.uniform(0.8, 1.3), "collision_radius": trunk, "solid": True})

    # Sous-bois (min_dist 1.5 entre decors ; peut etre proche des arbres).
    decor_xy = scatter(rng, n_decor, 1.5, [])
    for (x, z) in decor_xy:
        mesh, solid, radius, smin, smax = rng.choice(DECOR)
        entries.append({"mesh": mesh, "x": x, "z": z, "yaw_deg": rng.uniform(0, 360),
                        "scale": rng.uniform(smin, smax), "collision_radius": radius, "solid": solid})

    block = build_block(entries)

    with open(CONFIG, "r", encoding="utf-8") as f:
        text = f.read()
    text = re.sub(r'        "_comment_scenery": ".*?",\n', "", text, flags=re.DOTALL)
    text = re.sub(r'        "scenery": \{.*?\n        \},\n', "", text, flags=re.DOTALL)
    anchor = '        "_comment_test_water":'
    idx = text.find(anchor)
    if idx < 0:
        raise SystemExit("Ancre _comment_test_water introuvable dans config.json")
    text = text[:idx] + block + text[idx:]
    json.loads(text)  # validation
    with open(CONFIG, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("[scatter_forest] %d arbres + %d decors = %d elements dans world.scenery"
          % (len(tree_xy), len(decor_xy), len(entries)))


main()
