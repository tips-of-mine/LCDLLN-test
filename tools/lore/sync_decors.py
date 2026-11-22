#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_decors.py — recopie le registre de l'atelier décors dans le lore (clé `decors`).
#
# Chaîne : tools/asset_pipeline/decor/decor_objects.json (objets inventoriés dans les BD :
#   numéro, nom, tome, placement prévu) + decor_specs.json (assets produits : id, famille,
#   références BD, asset remplacé) + sources de tools/world_gen (le mesh est-il posé par le
#   générateur ?) → ce script → legacy/world-builder-html/lune-noire-data.json et
#   lune-noire-data-v20.json, clé `decors` : une fiche par objet.
#
# Fiche : id « decor_<num> », num, name, tome (0 = demande du propriétaire), family,
# meshes (assets livrés), replaces (assets historiques remplacés, id gardé), references_bd,
# placement (prévu à l'inventaire), generated (au moins un mesh posé par le générateur ;
# sinon catalogue de l'éditeur seulement), source.
#
# Sérialisation du lore inchangée : json.dumps(indent=2, ensure_ascii=False), sans retour à
# la ligne final ; seule la clé `decors` change (vérifié avant écriture).
#
# Lancement : python3 tools/lore/sync_decors.py [--check]
#   --check : n'écrit rien, sort en erreur (code 1) si le lore n'est pas à jour.

import argparse
import glob
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DECOR = os.path.join(REPO, "tools", "asset_pipeline", "decor")
LORES = [os.path.join(REPO, "legacy", "world-builder-html", n) for n in ("lune-noire-data.json", "lune-noire-data-v20.json")]
GEN_SOURCES = glob.glob(os.path.join(REPO, "tools", "world_gen", "lib", "*.cpp")) + \
    [os.path.join(REPO, "src", "shared", "world", "instances", "ForestMask.h"),
     os.path.join(REPO, "game", "data", "buildings", "templates", "maison.json")]


def build():
    with open(os.path.join(DECOR, "decor_objects.json"), encoding="utf-8") as f:
        objets = json.load(f)["objets"]
    with open(os.path.join(DECOR, "decor_specs.json"), encoding="utf-8") as f:
        specs = json.load(f)["assets"]
    code = ""
    for p in GEN_SOURCES:
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f:
                code += f.read()
    fiches = []
    for o in objets:
        assets = [a for a in specs if a["num"] == o["num"]]
        if not assets:
            continue
        meshes = [a["id"] for a in assets]
        # Préfixes posés par concaténation dans le générateur (Sign_Service_<id>, Rock_Water_<n>...).
        generated = any(('"' + m + '"') in code or ('"' + m + ".gltf") in code or ("/" + m + ".gltf") in code
                        or ('"' + m.rsplit("_", 1)[0] + '_"') in code or ("/" + m.rsplit("_", 1)[0] + '_"') in code
                        for m in meshes)
        refs = []
        for a in assets:
            for r in str(a.get("ref", "")).split(" ; "):
                r = r.strip()
                if r and r not in refs:
                    refs.append(r)
        fiches.append({
            "id": "decor_%03d" % o["num"],
            "num": o["num"],
            "name": o["nom"],
            "tome": o["tome"],
            "family": assets[0]["family"],
            "meshes": meshes,
            "replaces": sorted({a["replaces"] for a in assets if a.get("replaces")}),
            "references_bd": refs,
            "placement": o.get("placement_prevu", ""),
            "generated": generated,
            "source": "[BD] T%d" % o["tome"] if o["tome"] else "Hubert, demande du propriétaire (atelier décors)",
        })
    return fiches


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    fiches = build()
    stale = []
    for path in LORES:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        d = json.loads(text)
        if d.get("decors") == fiches:
            continue
        stale.append(os.path.basename(path))
        if a.check:
            continue
        before = {k: v for k, v in d.items() if k != "decors"}
        d["decors"] = fiches
        out = json.dumps(d, indent=2, ensure_ascii=False)
        after = {k: v for k, v in json.loads(out).items() if k != "decors"}
        if before != after:
            raise SystemExit("[sync_decors] modification hors de la clé decors : abandon")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(out)
    if a.check:
        if stale:
            print("[sync_decors] lore pas à jour :", ", ".join(stale))
            sys.exit(1)
        print("[sync_decors] lore à jour (%d fiches)" % len(fiches))
        return
    print("[sync_decors] %d fiche(s) ; mis à jour : %s" % (len(fiches), ", ".join(stale) or "aucun"))


if __name__ == "__main__":
    main()
