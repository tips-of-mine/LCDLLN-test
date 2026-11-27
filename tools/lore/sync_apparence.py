#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_apparence.py — recopie l'apparence propre des villes du lore vers le
# master du monde jouable (v1.0.142, atelier Kendrhir : enceinte, donjon, bâti, vestiges).
#
# Chaîne : lore legacy/world-builder-html/lune-noire-data.json, champ `apparence` (objet)
#   des fiches `villes` → ce script → game/data/world/royaumes/world_master.json, clé
#   `apparence` des `cities` (objet sur une ligne) → tools/world_gen (WorldMaster.cpp lit
#   l'objet ; Settlements.cpp pose rempart de rondins, donjon à flèche, maisons de planches
#   et restes de siège).
#
# Valeurs admises (contrôlées ici et par le générateur, qui refuse une valeur inconnue) :
#   enceinte : "rempart_bois" ; donjon : "tour_fleche" ; bati : "planches" ;
#   vestiges_siege : true / false.
#
# Règles :
#   - Correspondance : même `id` (ville du lore = ville du master).
#   - Une ville sans apparence (absente, null ou objet vide) n'a pas de clé `apparence`.
#   - Édition CHIRURGICALE du texte du master : une ligne `"apparence": {...},` par ville,
#     insérée après la ligne `"services"` (sinon `"faction"`), ou remplacée ; rien d'autre
#     ne change (vérifié avant écriture).
#
# Lancement : python3 tools/lore/sync_apparence.py [--check]
#   --check : n'écrit rien, sort en erreur (code 1) si le master n'est pas à jour ou si une
#   valeur est inconnue.

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")

ALLOWED = {
    "enceinte": {"rempart_bois"},
    "donjon": {"tour_fleche"},
    "bati": {"planches"},
}
ORDER = ("enceinte", "donjon", "bati", "vestiges_siege")


def normalize(cid, ap):
    """Objet d'apparence normalisé (clés dans l'ordre ORDER, vides retirées) ou None.
    Lève SystemExit sur une clé ou une valeur inconnue."""
    if not ap:
        return None
    if not isinstance(ap, dict):
        raise SystemExit(f"[sync_apparence] ville {cid} : apparence n'est pas un objet")
    out = {}
    for k, v in ap.items():
        if k not in ORDER:
            raise SystemExit(f"[sync_apparence] ville {cid} : clé d'apparence inconnue « {k} »")
        if k == "vestiges_siege":
            if not isinstance(v, bool):
                raise SystemExit(f"[sync_apparence] ville {cid} : vestiges_siege doit être true ou false")
            continue
        if v in (None, ""):
            continue
        if v not in ALLOWED[k]:
            raise SystemExit(f"[sync_apparence] ville {cid} : {k} « {v} » inconnu ({', '.join(sorted(ALLOWED[k]))})")
    for k in ORDER:
        v = ap.get(k)
        if k == "vestiges_siege":
            if v:
                out[k] = True
        elif v:
            out[k] = v
    return out or None


def lore_apparence():
    with open(LORE, encoding="utf-8") as f:
        lore = json.load(f)
    return {v["id"]: normalize(v["id"], v.get("apparence")) for v in lore.get("villes", [])}


def sync(text, wanted):
    lines = text.split("\n")
    # Seul le tableau `cities` est parcouru (une région peut porter le même id qu'une ville).
    i = next(k for k, l in enumerate(lines) if l.startswith('  "cities": ['))
    end = next(k for k in range(i + 1, len(lines)) if lines[k].startswith("  ]"))
    changed = 0
    while i < end:
        m = re.match(r'^(\s*)"id": "([^"]+)",\s*$', lines[i])
        if not m or m.group(2) not in wanted:
            i += 1
            continue
        indent, cid = m.group(1), m.group(2)
        close = indent[:-2] + "}"
        j = i + 1
        while j < len(lines) and not lines[j].startswith(close):
            j += 1
        block = range(i, j)
        ap_line = next((k for k in block if lines[k].startswith(indent + '"apparence":')), None)
        anchor = next((k for k in block if lines[k].startswith(indent + '"services":')), None)
        if anchor is None:
            anchor = next((k for k in block if lines[k].startswith(indent + '"faction":')), None)
        ap = wanted[cid]
        new = indent + '"apparence": ' + json.dumps(ap, ensure_ascii=False) + "," if ap else None
        if ap_line is not None:
            if not ap:
                del lines[ap_line]
                j -= 1
                end -= 1
                changed += 1
            elif lines[ap_line] != new:
                lines[ap_line] = new
                changed += 1
        elif ap:
            if anchor is None:
                raise SystemExit(f"[sync_apparence] ville {cid} : ligne \"services\" ou \"faction\" introuvable")
            lines.insert(anchor + 1, new)
            j += 1
            end += 1
            changed += 1
        i = j
    return "\n".join(lines), changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    wanted = lore_apparence()
    with open(MASTER, encoding="utf-8") as f:
        text = f.read()
    out, changed = sync(text, wanted)
    # Contrôle : seule la clé `apparence` des villes a bougé.
    before, after = json.loads(text), json.loads(out)
    for c in after["cities"]:
        c.pop("apparence", None)
    for c in before["cities"]:
        c.pop("apparence", None)
    if before != after:
        raise SystemExit("[sync_apparence] modification hors de la clé apparence : abandon")
    if a.check:
        if changed:
            print(f"[sync_apparence] master pas à jour ({changed} ville(s))")
            sys.exit(1)
        print("[sync_apparence] master à jour")
        return
    if changed:
        with open(MASTER, "w", encoding="utf-8", newline="") as f:
            f.write(out)
    print(f"[sync_apparence] {changed} ville(s) mise(s) à jour")


if __name__ == "__main__":
    main()
