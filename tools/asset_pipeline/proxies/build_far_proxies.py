#!/usr/bin/env python3
# v1.0.145 — Silhouettes lointaines du bâti (HLOD léger des cellules du monde).
#
# Écrit dans game/data/meshes/proxies/ les meshes UNITAIRES utilisés par le client pour
# remplacer, au-delà de la distance de culling du bâti, les pièces modulaires des maisons,
# remparts, tours et palais (src/client/world/scatter/FarProxyRules.h) :
#   Proxy_Block_<famille>.gltf  bloc       X, Z ∈ [-0,5 ; 0,5], Y ∈ [0 ; 1]
#   Proxy_Gable_<famille>.gltf  toit à deux pans, faîtage le long de X, à Y = 1
#   Proxy_Spire_<famille>.gltf  flèche conique à 8 pans, pointe à Y = 1
# La matrice d'instance met chaque mesh à l'échelle de la boîte englobante de la pièce.
#
# Couleur : COLOR_0 par sommet (linéaire), matériau sans texture nommé « Proxy_* » : le
# moteur prend la couleur de sommet comme albedo (MaterialFlags::VertexColorAlbedo). Les
# teintes sont la moyenne perçue de loin des textures du kit (plâtre, pierre, brique, bois,
# tuiles, ardoise, chaume) ; le bas des murs est légèrement assombri (salissure, ombre de
# contact) pour éviter l'aspect « maquette ».
#
# Lancement : python tools/asset_pipeline/proxies/build_far_proxies.py
# Pur Python (aucune dépendance) ; idempotent ; contrôlé en CI par --check (fichiers à jour).
import base64
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(REPO, "game", "data", "meshes", "proxies")

# Teintes sRGB (0-255) vues de loin.
WALLS = {
    "Plaster": (214, 203, 178),
    "Stone": (150, 144, 134),
    "Brick": (160, 118, 92),
    "RedBrick": (150, 82, 66),
    "Wood": (112, 84, 58),
}
ROOFS = {
    "Tiles": (146, 72, 52),
    "Slate": (82, 80, 86),
    "Wood": (98, 72, 50),
    "Thatch": (158, 134, 88),
}


def lin(c):
    """sRGB 0-255 -> linéaire 0-1 (COLOR_0 est linéaire en glTF)."""
    v = c / 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def colour(rgb, k=1.0):
    return [round(lin(c) * k, 5) for c in rgb] + [1.0]


def face(verts, tri_list, pos, col, nrm):
    """Ajoute un polygone convexe plan (sommets dans l'ordre CCW vu de l'extérieur)."""
    base = len(verts)
    for p, c in zip(pos, col):
        verts.append((p, nrm, c))
    for i in range(1, len(pos) - 1):
        tri_list.append((base, base + i, base + i + 1))


def normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    l = math.sqrt(sum(x * x for x in n)) or 1.0
    return [round(x / l, 6) for x in n]


def block(rgb):
    v, t = [], []
    top, low = colour(rgb), colour(rgb, 0.78)
    x0, x1, z0, z1 = -0.5, 0.5, -0.5, 0.5
    # Côtés (bas assombri), CCW vus de l'extérieur.
    sides = [
        ((x0, 0, z1), (x1, 0, z1), (x1, 1, z1), (x0, 1, z1)),  # +Z
        ((x1, 0, z0), (x0, 0, z0), (x0, 1, z0), (x1, 1, z0)),  # -Z
        ((x1, 0, z1), (x1, 0, z0), (x1, 1, z0), (x1, 1, z1)),  # +X
        ((x0, 0, z0), (x0, 0, z1), (x0, 1, z1), (x0, 1, z0)),  # -X
    ]
    for q in sides:
        face(v, t, q, [low, low, top, top], normal(*q[:3]))
    q = ((x0, 1, z1), (x1, 1, z1), (x1, 1, z0), (x0, 1, z0))
    face(v, t, q, [top] * 4, normal(*q[:3]))
    return v, t


def gable(rgb):
    v, t = [], []
    c, eave = colour(rgb), colour(rgb, 0.85)
    x0, x1, z0, z1 = -0.5, 0.5, -0.5, 0.5
    # Deux pans (faîtage le long de X à Y = 1, gouttières à Y = 0).
    p1 = ((x0, 0, z1), (x1, 0, z1), (x1, 1, 0), (x0, 1, 0))
    p2 = ((x1, 0, z0), (x0, 0, z0), (x0, 1, 0), (x1, 1, 0))
    face(v, t, p1, [eave, eave, c, c], normal(*p1[:3]))
    face(v, t, p2, [eave, eave, c, c], normal(*p2[:3]))
    # Pignons (triangles), teinte du toit assombrie (ils sont normalement en mur).
    g = colour(rgb, 0.7)
    t1 = ((x1, 0, z1), (x1, 0, z0), (x1, 1, 0))
    t2 = ((x0, 0, z0), (x0, 0, z1), (x0, 1, 0))
    face(v, t, t1, [g] * 3, normal(*t1))
    face(v, t, t2, [g] * 3, normal(*t2))
    return v, t


def spire(rgb, sides=8):
    v, t = [], []
    c, eave = colour(rgb), colour(rgb, 0.85)
    for k in range(sides):
        a0 = 2 * math.pi * k / sides
        a1 = 2 * math.pi * (k + 1) / sides
        p0 = (0.5 * math.cos(a0), 0.0, 0.5 * math.sin(a0))
        p1 = (0.5 * math.cos(a1), 0.0, 0.5 * math.sin(a1))
        tri = (p1, p0, (0.0, 1.0, 0.0))  # CCW vu de l'extérieur
        face(v, t, tri, [eave, eave, c], normal(*tri))
    return v, t


def gltf(name, mesh):
    verts, tris = mesh
    pos = b"".join(struct.pack("<3f", *p) for p, _n, _c in verts)
    nor = b"".join(struct.pack("<3f", *n) for _p, n, _c in verts)
    col = b"".join(struct.pack("<4f", *c) for _p, _n, c in verts)
    idx = b"".join(struct.pack("<3H", *tr) for tr in tris)
    blob = pos + nor + col + idx
    n = len(verts)
    mn = [round(min(p[i] for p, _n, _c in verts), 6) for i in range(3)]
    mx = [round(max(p[i] for p, _n, _c in verts), 6) for i in range(3)]
    o_nor, o_col, o_idx = len(pos), len(pos) + len(nor), len(pos) + len(nor) + len(col)
    return {
        "asset": {"generator": "LCDLLN build_far_proxies.py", "version": "2.0"},
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": "Proxy_" + name, "doubleSided": False,
                       "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.9}}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2}, "indices": 3, "material": 0}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": mn, "max": mx},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5123, "count": len(tris) * 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos), "target": 34962},
            {"buffer": 0, "byteOffset": o_nor, "byteLength": len(nor), "target": 34962},
            {"buffer": 0, "byteOffset": o_col, "byteLength": len(col), "target": 34962},
            {"buffer": 0, "byteOffset": o_idx, "byteLength": len(idx), "target": 34963},
        ],
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode("ascii")}],
    }


def build():
    files = {}
    for fam, rgb in WALLS.items():
        files["Proxy_Block_" + fam] = gltf("Block_" + fam, block(rgb))
    for fam, rgb in ROOFS.items():
        files["Proxy_Gable_" + fam] = gltf("Gable_" + fam, gable(rgb))
        files["Proxy_Spire_" + fam] = gltf("Spire_" + fam, spire(rgb))
    return {k + ".gltf": json.dumps(v, indent=1, ensure_ascii=False) + "\n" for k, v in files.items()}


def main():
    check = "--check" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    stale = []
    for fn, text in sorted(build().items()):
        path = os.path.join(OUT, fn)
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            stale.append(fn)
            if not check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
    if check and stale:
        print("[far_proxies] ECHEC fichiers absents ou périmés :", ", ".join(stale))
        return 1
    print(f"[far_proxies] {len(build())} silhouettes {'à jour' if check else 'écrites'}"
          + ("" if check else f" ({len(stale)} modifiée(s))"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
