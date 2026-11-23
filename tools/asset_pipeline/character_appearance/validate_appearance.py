#!/usr/bin/env python3
"""Contrôle CI des assets de personnalisation des personnages (sans Blender).

Vérifie le catalogue ``game/data/configuration/customization/appearance_catalog.json``
et tout ce qu'il référence :

* les 5 races jouables (races.json, ``enabled``) × 2 genres sont présentes ;
* au moins 5 emplacements personnalisables (>= 2 options) par race et genre ;
* chaque pièce a ses 3 niveaux de détail, lisibles (.glb), avec un squelette
  IDENTIQUE à celui du corps (mêmes os, même ordre) — condition pour partager
  les matrices d'os à l'exécution ;
* budgets de triangles par catégorie et par LOD, LOD décroissants ;
* boîte englobante plausible (pièce collée au personnage, entre le sol et 2,4 m) ;
* textures et corps référencés présents, teintes « #RRGGBB » valides ;
* licence tracée.

Lancement : ``python tools/asset_pipeline/character_appearance/validate_appearance.py``
Code retour : 0 si tout est conforme, 1 sinon (écarts listés sur la sortie).
Ne dépend que de la bibliothèque standard.
"""

from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DATA = REPO / "game" / "data"
CATALOG = DATA / "configuration" / "customization" / "appearance_catalog.json"
RACES = DATA / "races" / "races.json"

GEOMETRY_SLOTS = ("face", "hair", "beard", "moustache", "ears", "horns", "tail", "wings", "tusks")
COLOR_SLOTS = ("skinTone", "hairColor", "eyeColor")
HEX = re.compile(r"^#[0-9A-Fa-f]{6}$")


def read_glb(path: Path) -> tuple:
    """(document JSON, tampon binaire) d'un .glb."""
    data = path.read_bytes()
    magic, _ver, _len = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("pas un .glb")
    off, doc, binary = 12, None, b""
    while off < len(data):
        ln, ty = struct.unpack_from("<II", data, off)
        chunk = data[off + 8:off + 8 + ln]
        if ty == 0x4E4F534A:
            doc = json.loads(chunk.decode("utf-8"))
        elif ty == 0x004E4942:
            binary = chunk
        off += 8 + ln
    if doc is None:
        raise ValueError("bloc JSON absent")
    return doc, binary


def joint_names(doc: dict) -> list:
    skin = doc["skins"][0]
    return [doc["nodes"][j].get("name", "") for j in skin["joints"]]


def triangles(doc: dict) -> int:
    n = 0
    for m in doc["meshes"]:
        for p in m["primitives"]:
            n += doc["accessors"][p["indices"]]["count"] // 3
    return n


def bbox_m(doc: dict) -> tuple:
    """Boîte englobante en mètres, repère du corps (Z haut) : brut × 100."""
    mn = [1e30] * 3
    mx = [-1e30] * 3
    for m in doc["meshes"]:
        for p in m["primitives"]:
            acc = doc["accessors"][p["attributes"]["POSITION"]]
            mn = [min(a, b * 100.0) for a, b in zip(mn, acc["min"])]
            mx = [max(a, b * 100.0) for a, b in zip(mx, acc["max"])]
    return mn, mx


def main() -> int:
    errors: list = []
    if not CATALOG.exists():
        print(f"[appearance] catalogue absent : {CATALOG}")
        return 1
    cat = json.loads(CATALOG.read_text(encoding="utf-8"))
    budgets = cat.get("budgets", {})
    if not cat.get("licence"):
        errors.append("catalogue : licence non tracée")
    for key in ("hornTexture", "tuskTexture"):
        if not (DATA / cat.get(key, "")).is_file():
            errors.append(f"catalogue : {key} absente ({cat.get(key)})")

    playable = [r["id"] for r in json.loads(RACES.read_text(encoding="utf-8"))["races"] if r.get("enabled")]
    body_joints: dict = {}
    checked_parts = 0
    for race in playable:
        for gender in ("male", "female"):
            entry = cat.get("races", {}).get(race, {}).get(gender)
            where = f"{race}/{gender}"
            if entry is None:
                errors.append(f"{where} : absent du catalogue")
                continue
            body = DATA / entry["body"]
            if not body.is_file():
                errors.append(f"{where} : corps absent ({entry['body']})")
                continue
            if entry["body"] not in body_joints:
                body_joints[entry["body"]] = joint_names(read_glb(body)[0])
            ref = body_joints[entry["body"]]
            slots = entry.get("slots", {})
            customizable = sum(1 for opts in slots.values() if len(opts) >= 2)
            if customizable < 5:
                errors.append(f"{where} : {customizable} emplacements personnalisables (< 5)")
            if not slots.get("face"):
                errors.append(f"{where} : aucun visage")
            for slot, opts in slots.items():
                ids = [o["id"] for o in opts]
                if len(ids) != len(set(ids)):
                    errors.append(f"{where}/{slot} : identifiants en double")
                for o in opts:
                    label = f"{where}/{slot}/{o['id']}"
                    if slot in COLOR_SLOTS:
                        tex = o.get("texture") or o.get("basecolor", "")
                        if not (DATA / tex).is_file():
                            errors.append(f"{label} : texture absente ({tex})")
                        for k in ("hex", "tint"):
                            if k in o and not HEX.match(o[k]):
                                errors.append(f"{label} : {k} invalide ({o[k]})")
                        continue
                    if slot not in GEOMETRY_SLOTS:
                        continue
                    lods = o.get("lods", [])
                    if not lods:
                        continue  # option « aucune » (Rasé, Sans ailes…)
                    if len(lods) != 3:
                        errors.append(f"{label} : {len(lods)} LOD au lieu de 3")
                        continue
                    previous = None
                    for lod, rel in enumerate(lods):
                        path = DATA / rel
                        if not path.is_file():
                            errors.append(f"{label} : LOD{lod} absent ({rel})")
                            continue
                        try:
                            doc, _ = read_glb(path)
                        except (ValueError, KeyError, json.JSONDecodeError) as exc:
                            errors.append(f"{label} : LOD{lod} illisible ({exc})")
                            continue
                        checked_parts += 1
                        if joint_names(doc) != ref:
                            errors.append(f"{label} : LOD{lod} squelette différent du corps")
                        tris = triangles(doc)
                        budget = budgets.get(slot, [None] * 3)[lod]
                        if budget is not None and tris > budget:
                            errors.append(f"{label} : LOD{lod} {tris} triangles > budget {budget}")
                        if previous is not None and tris > previous:
                            errors.append(f"{label} : LOD{lod} plus lourd que LOD{lod - 1}")
                        previous = tris
                        mn, mx = bbox_m(doc)
                        if mn[2] < 0.05 or mx[2] > 2.4 or max(abs(mn[0]), abs(mx[0])) > 0.9:
                            errors.append(f"{label} : LOD{lod} boîte englobante hors du personnage "
                                          f"(z {mn[2]:.2f}..{mx[2]:.2f} m, x ±{max(abs(mn[0]), abs(mx[0])):.2f} m)")

    for e in errors:
        print(f"[appearance] {e}")
    print(f"[appearance] {checked_parts} pièces contrôlées, {len(errors)} écart(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
