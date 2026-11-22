#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Contrôle des arbres « dark fantasy » générés par generate_trees_blender.py.

Vérifie, sans Blender (Python standard), pour chaque arbre et chacun de ses 3 LOD :
  - le .gltf et son .bin existent, la taille du .bin correspond aux buffers ;
  - le budget de triangles du LOD est respecté et décroît de LOD0 à LOD2 ;
  - les matériaux : écorce (baseColor + normal) puis feuillage en alphaMode BLEND
    (sauf l'arbre mort, sans feuillage), tous doubleSided ;
  - les images référencées existent (les LOD rangés dans lod/ pointent vers ../) ;
  - la hauteur (axe Y du glTF) est plausible pour l'espèce, base au sol ;
  - chaque arbre est au catalogue des props et dans les tables de Vegetation.cpp.

Usage : python tools/asset_pipeline/check_trees.py   (code retour 1 en cas d'échec)
"""

import json
import os
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")

#: nom -> (hauteur min, hauteur max) en mètres, feuillage attendu
TREES = {
    "DarkOak_1": (8, 20, True), "DarkOak_2": (8, 20, True),
    "DarkPine_1": (12, 24, True), "DarkPine_2": (12, 24, True),
    "GnarledDeadTree_1": (6, 16, False), "GnarledDeadTree_2": (6, 16, False),
    "MarshWillow_1": (5, 16, True), "MarshWillow_2": (5, 16, True),
}
TRI_BUDGET = {0: 26000, 1: 9000, 2: 2500}

_fails = []


def check(ok, what):
    """Enregistre un échec si \\p ok est faux."""
    if not ok:
        _fails.append(what)
        print("FAIL:", what)


def load(name, lod):
    """Charge le .gltf d'un LOD ; retourne (chemin, document) ou (chemin, None)."""
    path = os.path.join(PROPS, name + ".gltf") if lod == 0 else os.path.join(PROPS, "lod", "%s_LOD%d.gltf" % (name, lod))
    if not os.path.isfile(path):
        return path, None
    with open(path, encoding="utf-8") as f:
        return path, json.load(f)


def main():
    """Lance tous les contrôles ; code retour 0 si tout est conforme."""
    with open(os.path.join(PROPS, "catalog.json"), encoding="utf-8") as f:
        cat = json.load(f)["assets"]
    catalog_ids = {v["id"] for k, v in cat.items() if k != "count"}
    with open(os.path.join(REPO, "tools", "world_gen", "lib", "Vegetation.cpp"), encoding="utf-8") as f:
        vegetation = f.read()
    for name, (hmin, hmax, foliage) in TREES.items():
        check(name in catalog_ids, "%s absent du catalogue des props" % name)
        check('"%s"' % name in vegetation, "%s absent de Vegetation.cpp" % name)
        tris = []
        for lod in (0, 1, 2):
            path, g = load(name, lod)
            check(g is not None, "%s introuvable" % path)
            if g is None:
                continue
            base = os.path.dirname(path)
            for b in g["buffers"]:
                bin_path = os.path.join(base, b["uri"])
                check(os.path.isfile(bin_path) and os.path.getsize(bin_path) == b["byteLength"],
                      "%s : buffer %s absent ou de taille fausse" % (path, b["uri"]))
            n = sum(g["accessors"][p["indices"]]["count"] // 3 for m in g["meshes"] for p in m["primitives"])
            tris.append(n)
            check(0 < n <= TRI_BUDGET[lod], "%s LOD%d : %d triangles (budget %d)" % (name, lod, n, TRI_BUDGET[lod]))
            mats = g.get("materials", [])
            check(len(mats) == (2 if foliage else 1), "%s LOD%d : %d matériaux" % (name, lod, len(mats)))
            for m in mats:
                check(m.get("doubleSided") is True, "%s : %s pas doubleSided" % (name, m["name"]))
                if m["name"].startswith("Leaves_"):
                    check(m.get("alphaMode") == "BLEND", "%s : feuillage sans alphaMode BLEND" % name)
                else:
                    check("normalTexture" in m, "%s : écorce sans normal map" % name)
            for im in g.get("images", []):
                check(os.path.isfile(os.path.normpath(os.path.join(base, im["uri"]))),
                      "%s LOD%d : image %s introuvable" % (name, lod, im["uri"]))
            ymin = min(g["accessors"][p["attributes"]["POSITION"]]["min"][1] for m in g["meshes"] for p in m["primitives"])
            ymax = max(g["accessors"][p["attributes"]["POSITION"]]["max"][1] for m in g["meshes"] for p in m["primitives"])
            check(-1.5 <= ymin <= 0.1, "%s LOD%d : base à y=%.2f (attendu ≈ 0)" % (name, lod, ymin))
            check(hmin <= ymax <= hmax, "%s LOD%d : hauteur %.1f m hors [%d, %d]" % (name, lod, ymax, hmin, hmax))
        if len(tris) == 3:
            check(tris[0] > tris[1] > tris[2], "%s : triangles non décroissants %s" % (name, tris))
    if _fails:
        print("check_trees: %d échec(s)" % len(_fails))
        return 1
    print("check_trees: OK (%d arbres, 3 LOD chacun)" % len(TREES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
