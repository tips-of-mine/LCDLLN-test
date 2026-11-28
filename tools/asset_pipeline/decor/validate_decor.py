#!/usr/bin/env python3
# Atelier décors — contrôle CI des assets du registre decor_specs.json (sans Blender).
#
# Pour chaque asset : présence de <id>.gltf, <id>_LOD1.gltf, <id>_LOD2.gltf (+ .bin) et
# de <id>_BB.gltf si billboard ; budget de triangles par LOD (classe `budget`) ;
# hauteur et largeur (axe X, glTF Y-up) du LOD0 dans la cote prévue ±10 % ; origine au
# sol au centre de la base (min Y ∈ [-0,4 ; 0,05] m, centre XZ à moins de 15 % de la
# largeur) ; textures référencées présentes ; alphaMode MASK sur les matériaux à alpha.
# v1.0.145 : sur TOUS les props (registre ou non), aucun matériau « sans couleur » — ni
# texture de base, ni baseColorFactor, ni couleur de sommet COLOR_0, ni convention du kit
# (préfixe MI_, textures résolues par le moteur) : le moteur le rendrait en blanc uni.
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


def uncoloured_materials():
    """Liste les matériaux de props rendus en blanc uni faute de toute source de couleur.

    Parcourt game/data/meshes/props/**/*.gltf. Un matériau est accepté s'il a une texture de
    base, un baseColorFactor, un nom du kit (préfixe « MI_ », textures résolues par
    PropMaterialRules côté client) ou si TOUTES les primitives qui l'utilisent portent
    COLOR_0. Retourne une liste de messages d'écart (vide si conforme)."""
    errors = []
    for root, _dirs, files in os.walk(PROPS):
        for fn in sorted(files):
            if not fn.endswith(".gltf"):
                continue
            path = os.path.join(root, fn)
            with open(path, encoding="utf-8") as f:
                g = json.load(f)
            prims = [p for m in g.get("meshes", []) for p in m.get("primitives", [])]
            for mi, mat in enumerate(g.get("materials", [])):
                pbr = mat.get("pbrMetallicRoughness", {})
                if "baseColorTexture" in pbr or "baseColorFactor" in pbr or mat.get("name", "").startswith("MI_"):
                    continue
                users = [p for p in prims if p.get("material") == mi]
                if users and all("COLOR_0" in p.get("attributes", {}) for p in users):
                    continue
                rel = os.path.relpath(path, PROPS).replace(os.sep, "/")
                errors.append(f"{rel} : matériau {mat.get('name')} sans couleur (ni texture, ni facteur, ni COLOR_0)")
    return errors


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
            if lod == 0 and s.get("bbox_ref"):
                # Remplaçant d'un asset existant : même boîte (pivot compris), borne par
                # borne, tolérance max(10 % de l'étendue de l'axe, 3 cm).
                ref = s["bbox_ref"]
                for ax in range(3):
                    ext = ref[2 * ax + 1] - ref[2 * ax]
                    tol = max(TOL * ext, 0.03)
                    for got, want, lab in ((mn[ax], ref[2 * ax], "min"), (mx[ax], ref[2 * ax + 1], "max")):
                        if abs(got - want) > tol:
                            errors.append(f"{sid} : {lab} {'XYZ'[ax]} = {got:.2f} au lieu de {want:.2f} (asset remplacé)")
            elif lod == 0:
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
                # Végétation : houppier naturellement dissymétrique, le tronc reste à l'origine.
                k_off = 0.3 if s.get("family") == "vegetation" else 0.15
                if max(abs(cx), abs(cz)) > k_off * max(w, 0.2) + 0.05:
                    errors.append(f"{sid} : origine décentrée ({cx:.2f}, {cz:.2f})")
        if s.get("billboard"):
            g, err = load(sid + "_BB")
            if err:
                errors.append(err)
    errors += uncoloured_materials()
    for e in errors:
        print("[validate_decor] ECHEC", e)
    print(f"[validate_decor] {len(reg['assets'])} assets contrôlés, {len(errors)} écart(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
