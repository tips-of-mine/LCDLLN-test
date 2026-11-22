#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_services.py — recopie la liste des services de chaque ville du lore
# vers le master du monde jouable (atelier décors, 22/11/2026 : enseignes de service).
#
# Chaîne : lore legacy/world-builder-html/lune-noire-data.json, champ `services` des
#   fiches `villes` (rempli par tools/lore/generate_city_services.py) → ce script →
#   game/data/world/royaumes/world_master.json, clé `services` des `cities` (liste
#   d'identifiants sans doublon, dans l'ordre du lore) → tools/world_gen, qui pose un
#   panneau à pictogramme (Sign_Service_<id>) devant le bâtiment de chaque service.
#
# Règles :
#   - Correspondance : même `id` (ville du lore = ville du master).
#   - Une ville sans service (liste vide ou absente) n'a pas de clé `services`.
#   - Édition CHIRURGICALE du texte du master : une ligne `"services": [...]` par ville,
#     insérée après la ligne `"faction"` (ou remplacée) ; rien d'autre ne change
#     (vérifié avant écriture).
#
# Lancement : python3 tools/lore/sync_services.py [--check]
#   --check : n'écrit rien, sort en erreur (code 1) si le master n'est pas à jour.

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")


def lore_services():
    with open(LORE, encoding="utf-8") as f:
        lore = json.load(f)
    out = {}
    for v in lore.get("villes", []):
        seen = []
        for s in v.get("services") or []:
            if isinstance(s, str) and s and s not in seen:
                seen.append(s)
        out[v["id"]] = seen
    return out


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
        # Bornes de l'objet ville : jusqu'à l'accolade fermante de même niveau.
        close = indent[:-2] + "}"
        j = i + 1
        while j < len(lines) and not lines[j].startswith(close):
            j += 1
        block = range(i, j)
        svc_line = next((k for k in block if lines[k].startswith(indent + '"services":')), None)
        fac_line = next((k for k in block if lines[k].startswith(indent + '"faction":')), None)
        svc = wanted[cid]
        new = indent + '"services": ' + json.dumps(svc, ensure_ascii=False) + ","
        if svc_line is not None:
            if not svc:
                del lines[svc_line]
                j -= 1
                end -= 1
                changed += 1
            elif lines[svc_line] != new:
                lines[svc_line] = new
                changed += 1
        elif svc:
            if fac_line is None:
                raise SystemExit(f"[sync_services] ville {cid} : ligne \"faction\" introuvable")
            lines.insert(fac_line + 1, new)
            j += 1
            end += 1
            changed += 1
        i = j
    return "\n".join(lines), changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    wanted = lore_services()
    with open(MASTER, encoding="utf-8") as f:
        text = f.read()
    out, changed = sync(text, wanted)
    # Contrôle : seule la clé `services` des villes a bougé.
    before, after = json.loads(text), json.loads(out)
    for c in after["cities"]:
        c.pop("services", None)
    for c in before["cities"]:
        c.pop("services", None)
    if before != after:
        raise SystemExit("[sync_services] modification hors de la clé services : abandon")
    missing = [c["id"] for c in json.loads(out)["cities"] if c["id"] not in wanted]
    if missing:
        print("[sync_services] villes du master absentes du lore :", ", ".join(missing))
    if a.check:
        if changed:
            print(f"[sync_services] master pas à jour ({changed} ville(s))")
            sys.exit(1)
        print("[sync_services] master à jour")
        return
    if changed:
        with open(MASTER, "w", encoding="utf-8", newline="") as f:
            f.write(out)
    print(f"[sync_services] {changed} ville(s) mise(s) à jour")


if __name__ == "__main__":
    main()
