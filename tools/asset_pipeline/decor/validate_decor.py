#!/usr/bin/env python3
# Atelier décors — contrôle CI des assets du registre decor_specs.json (sans Blender).
#
# Pour chaque asset : présence de <id>.gltf, <id>_LOD1.gltf, <id>_LOD2.gltf (+ .bin) et
# de <id>_BB.gltf si billboard ; budget de triangles par LOD (classe `budget`) ;
# hauteur et largeur (axe X, glTF Y-up) du LOD0 dans la cote prévue ±10 % ; origine au
# sol au centre de la base (min Y ∈ [-0,4 ; 0,05] m, centre XZ à moins de 15 % de la
# largeur) ; textures référencées présentes ; alphaMode MASK sur les matériaux à alpha.
#
# Lancement : python tools/asset_pipeline/decor/validate_decor.py
# Code retour : 0 si tout est conforme, 1 sinon (liste des écarts sur stdout).
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")
TOL = 0.10


def load(name):
    path = os.path.join(PROPS, name + ".gltf")
    if not os.path.exists(path):
        return None, f"{name}.gltf absent"
    with open(path, encoding="utf-8") as f:
        g = json.load(f)
    for b in g.get("buffers", []):
        if not os.path.exists(os.path.join(PROPS, b.get("uri", ""))):
            return None, f"{name} : buffer {b.get('uri')} absent"
    for im in g.get("images", []):
        if not os.path.exists(os.path.join(PROPS, im.get("uri", ""))):
            return None, f"{name} : texture {im.get('uri')} absente"
    return g, None


def stats(g):
    tris = 0
    mn, mx = [1e30] * 3, [-1e30] * 3
    for m in g["meshes"]:
        for p in m["primitives"]:
            acc = g["accessors"][p["attributes"]["POSITION"]]
            mn = [min(a, b) for a, b in zip(mn, acc["min"])]
            mx = [max(a, b) for a, b in zip(mx, acc["max"])]
            tris += g["accessors"][p["indices"]]["count"] // 3 if "indices" in p else acc["count"] // 3
    return tris, mn, mx


def main():
    with open(os.path.join(HERE, "decor_specs.json"), encoding="utf-8") as f:
        reg = json.load(f)
    budgets = reg["budgets"]
    errors, ids = [], set()
    for s in reg["assets"]:
        sid = s["id"]
        if sid in ids:
            errors.append(f"{sid} : identifiant en double dans le registre")
        ids.add(sid)
        names = [sid, sid + "_LOD1", sid + "_LOD2"]
        for lod, n in enumerate(names):
            g, err = load(n)
            if err:
                errors.append(err)
                continue
            tris, mn, mx = stats(g)
            if tris > budgets[s["budget"]][lod]:
                errors.append(f"{n} : {tris} triangles > budget {budgets[s['budget']][lod]}")
            for m in g.get("materials", []):
                if m.get("name", "").startswith(("MI_Decor_Leaves", "MI_Decor_Plant", "MI_Decor_BB")) \
                        and m.get("alphaMode") != "MASK":
                    errors.append(f"{n} : matériau {m.get('name')} sans alphaMode MASK")
            if lod == 0:
                h, w = mx[1], mx[0] - mn[0]
                lo, hi = s["height_m"]
                if not (lo * (1 - TOL) <= h <= hi * (1 + TOL)):
                    errors.append(f"{sid} : hauteur {h:.2f} m hors cote {lo}-{hi} m (±10 %)")
                lo, hi = s["width_m"]
                if not (lo * (1 - TOL) <= w <= hi * (1 + TOL)):
                    errors.append(f"{sid} : largeur {w:.2f} m hors cote {lo}-{hi} m (±10 %)")
                if not (-0.4 <= mn[1] <= 0.05):
                    errors.append(f"{sid} : base à y={mn[1]:.2f} m (origine attendue au sol)")
                cx, cz = (mn[0] + mx[0]) / 2, (mn[2] + mx[2]) / 2
                if max(abs(cx), abs(cz)) > 0.15 * max(w, 0.2) + 0.05:
                    errors.append(f"{sid} : origine décentrée ({cx:.2f}, {cz:.2f})")
        if s.get("billboard"):
            g, err = load(sid + "_BB")
            if err:
                errors.append(err)
    for e in errors:
        print("[validate_decor] ECHEC", e)
    print(f"[validate_decor] {len(reg['assets'])} assets contrôlés, {len(errors)} écart(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
