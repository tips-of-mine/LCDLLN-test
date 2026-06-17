#!/usr/bin/env python3
"""Génère game/data/meshes/props/catalog.json à partir des .gltf du kit.

Scanne game/data/meshes/props/*.gltf, dérive une catégorie par préfixe de
nom de fichier, et écrit un catalogue au format consommé par
src/world_editor/assets/AssetCatalog (compatible parseur Config : objet
`assets` avec `count` + clés indexées "0", "1", …).

Déterministe (tri alphabétique). Idempotent : ré-exécuter régénère le même
fichier tant que le contenu du dossier ne change pas.

Usage:
    python tools/asset_pipeline/generate_props_catalog.py
    python tools/asset_pipeline/generate_props_catalog.py --content game/data
"""

import argparse
import json
import os
import sys

# Règles de catégorisation par préfixe de nom (ordre = priorité).
# (préfixe_test, catégorie). Le préfixe est comparé en casse-insensible.
PREFIX_RULES = [
    ("Wall_", "Wall"),
    ("DoorFrame_", "Door"),
    ("Door_", "Door"),
    ("WindowShutters_", "Window"),
    ("Window_", "Window"),
    ("Roof_", "Roof"),
    ("Floor_", "Floor"),
    ("Balcony_", "Balcony"),
    ("HoleCover_", "Floor"),
    ("Stall_", "Furniture"),
    ("Table_", "Furniture"),
    ("Chair", "Furniture"),
    ("Bench", "Furniture"),
    ("Bed_", "Furniture"),
    ("Barrel", "Furniture"),
    ("Chest", "Furniture"),
    ("Cabinet", "Furniture"),
    ("Bookcase", "Furniture"),
    ("BookGroup", "Furniture"),
    ("CandleStick", "Light"),
    ("Chandelier", "Light"),
    ("Torch", "Light"),
    ("Lantern", "Light"),
    ("Banner", "Decor"),
]


def categorize(stem):
    low = stem.lower()
    for prefix, cat in PREFIX_RULES:
        if low.startswith(prefix.lower()):
            return cat
    return "Misc"


def pretty_name(stem):
    """Nom lisible : remplace underscores par espaces."""
    return stem.replace("_", " ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--content", default="game/data",
                    help="Racine du contenu (défaut: game/data)")
    args = ap.parse_args()

    props_dir = os.path.join(args.content, "meshes", "props")
    if not os.path.isdir(props_dir):
        print("ERREUR: dossier introuvable: %s" % props_dir, file=sys.stderr)
        return 1

    gltfs = sorted(f for f in os.listdir(props_dir) if f.lower().endswith(".gltf"))

    assets = {"count": len(gltfs)}
    for i, fname in enumerate(gltfs):
        stem = fname[:-len(".gltf")]
        assets[str(i)] = {
            "id": stem,
            "category": categorize(stem),
            "gltf": "meshes/props/%s" % fname,
            "displayName": pretty_name(stem),
            "thumbnail": "",
        }

    doc = {
        "_comment": ("Catalogue d'assets du kit props, généré par "
                     "tools/asset_pipeline/generate_props_catalog.py. "
                     "Ne pas éditer à la main : régénérer."),
        "assets": assets,
    }

    out_path = os.path.join(props_dir, "catalog.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
        f.write("\n")

    # Récapitulatif par catégorie.
    by_cat = {}
    for i in range(len(gltfs)):
        c = assets[str(i)]["category"]
        by_cat[c] = by_cat.get(c, 0) + 1
    print("Catalogue écrit: %s (%d assets)" % (out_path, len(gltfs)))
    for c in sorted(by_cat):
        print("  %-10s %d" % (c, by_cat[c]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
