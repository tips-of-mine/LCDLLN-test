#!/usr/bin/env python3
# Genere une foret riveraine (~80 arbres) dans config.json > world.scenery.
#
# Disperse 4 essences (CommonTree, Pine, DeadTree, TwistedTree) autour du bassin
# d'eau (centre 55,0 ; demi-taille 40 -> rect x[15,95] z[-40,40]) et sur les terres
# environnantes. Exclut la surface d'eau, le voisinage du spawn (0,0), du villageois
# (4,0) et du coffre (-4,0), et impose une distance minimale entre arbres.
#
# Deterministe (graine fixe) -> meme foret a chaque execution. Idempotent : remplace
# le bloc world.scenery existant (insertion TEXTE ciblee, le reste de config.json est
# preserve octet pour octet).
#
# Lancement (Python systeme ou Python embarque de Blender) :
#   python tools/world_gen/scatter_forest.py [--count 80]
#   blender --background --python tools/world_gen/scatter_forest.py   (si pas de python)
import json
import math
import os
import random
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFIG = os.path.join(REPO, "config.json")

# (mesh, rayon de tronc pour la collision en metres)
SPECIES = (
    [(f"meshes/props/CommonTree_{i}.gltf", 0.6) for i in range(1, 6)] +
    [(f"meshes/props/Pine_{i}.gltf",       0.5) for i in range(1, 6)] +
    [(f"meshes/props/DeadTree_{i}.gltf",   0.5) for i in range(1, 6)] +
    [(f"meshes/props/TwistedTree_{i}.gltf", 0.8) for i in range(1, 6)]
)

WATER_CX, WATER_CZ, WATER_HALF = 55.0, 0.0, 40.0
# (x, z, rayon) a laisser libres : spawn, villageois, coffre.
EXCLUDE = [(0.0, 0.0, 6.0), (4.0, 0.0, 6.0), (-4.0, 0.0, 6.0)]
MIN_DIST = 3.0
AREA = (-25.0, 125.0, -75.0, 75.0)  # xmin, xmax, zmin, zmax
SEED = 20260531


def in_water(x, z):
    return abs(x - WATER_CX) <= WATER_HALF and abs(z - WATER_CZ) <= WATER_HALF


def excluded(x, z):
    for ex, ez, er in EXCLUDE:
        if (x - ex) ** 2 + (z - ez) ** 2 < er * er:
            return True
    return False


def scatter(count):
    rng = random.Random(SEED)
    placed = []
    attempts = 0
    while len(placed) < count and attempts < count * 400:
        attempts += 1
        x = rng.uniform(AREA[0], AREA[1])
        z = rng.uniform(AREA[2], AREA[3])
        if in_water(x, z) or excluded(x, z):
            continue
        if any((x - px) ** 2 + (z - pz) ** 2 < MIN_DIST * MIN_DIST for px, pz, *_ in placed):
            continue
        mesh, trunk = rng.choice(SPECIES)
        placed.append((x, z, mesh, trunk, rng.uniform(0.0, 360.0), rng.uniform(0.8, 1.3)))
    return placed, attempts


def build_block(placed):
    # Bloc texte au style de config.json (8 espaces pour les cles de "world",
    # 12 pour les entrees). Une entree par ligne.
    lines = []
    lines.append('        "_comment_scenery": "Decor solide non interactif (foret riveraine + props nature). '
                 'Genere par tools/world_gen/scatter_forest.py (deterministe). count = nombre d\'elements ; '
                 'chaque index i a mesh (relatif a paths.content), x/z (metres monde, Y pose au sol au runtime), '
                 'yaw_deg, scale, collision_radius (rayon du cylindre de collision en m ; tronc pour les arbres). '
                 'Tout element est SOLIDE (le personnage ne le traverse pas).",')
    lines.append('        "scenery": {')
    lines.append('            "count": %d,' % len(placed))
    for i, (x, z, mesh, trunk, yaw, scale) in enumerate(placed):
        last = (i == len(placed) - 1)
        lines.append(
            '            "%d": { "mesh": "%s", "x": %.2f, "z": %.2f, "yaw_deg": %.1f, "scale": %.2f, "collision_radius": %.2f }%s'
            % (i, mesh, x, z, yaw, scale, trunk, "" if last else ",")
        )
    lines.append('        },')
    return "\n".join(lines) + "\n"


def main():
    count = 80
    if "--count" in sys.argv:
        count = int(sys.argv[sys.argv.index("--count") + 1])

    placed, attempts = scatter(count)
    block = build_block(placed)

    with open(CONFIG, "r", encoding="utf-8") as f:
        text = f.read()

    # Retire un bloc scenery existant (idempotence) : commentaire + objet.
    text = re.sub(r'        "_comment_scenery": ".*?",\n', "", text, flags=re.DOTALL)
    text = re.sub(r'        "scenery": \{.*?\n        \},\n', "", text, flags=re.DOTALL)

    # Insere le bloc juste avant le commentaire test_water (ancre stable dans "world").
    anchor = '        "_comment_test_water":'
    idx = text.find(anchor)
    if idx < 0:
        raise SystemExit("Ancre _comment_test_water introuvable dans config.json")
    text = text[:idx] + block + text[idx:]

    # Validation JSON avant ecriture.
    json.loads(text)

    with open(CONFIG, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("[scatter_forest] %d arbres ecrits dans world.scenery (%d tentatives)"
          % (len(placed), attempts))


main()
