#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/check_secrets.py — concordance des lieux SECRETS entre le lore et le
# master du monde jouable (v1.0.110, point 65 : option a du propriétaire).
#
# Règle : une fiche `villes` ou `lieux` du lore à `secret: true` (case « Secret » de
# l'éditeur) est cachée aux joueurs. Au master (game/data/world/royaumes/world_master.json)
# la ville (`cities`) ou le POI (`pois`) correspondant doit porter `"secret": true` :
# tools/world_gen l'écrit alors dans world/<monde>/secret_places.json (annuaire des MJ,
# jamais livré au client) au lieu de places.json (carte du jeu, bannières, radar).
#
# Contrôles (code 1 au premier écart, liste complète affichée) :
#   - secret au lore, pas au master (le lieu serait révélé sur la carte du jeu) ;
#   - secret au master, pas au lore (le master ne suit plus la décision du lore) ;
#   - une route du lore (`lore_roads`) a pour extrémité un lieu secret (la route
#     le révélerait ; tools/world_gen refuse aussi ce cas).
# Correspondance lore ↔ master : celle de sync_positions.py (même id de ville ; lieu
# `lieu_x` = POI `poi_x`, sinon même nom dans la même case, sinon table explicite).
# Une fiche secrète sans entrée au master est normale (rien à cacher) : signalée seulement.
#
# Lancement : python3 tools/lore/check_secrets.py   (Python standard, aucune écriture)

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sync_positions import LORE, MASTER, correspondances  # noqa: E402


def main():
    """Compare les drapeaux `secret` du lore et du master. \\return code de sortie."""
    with open(LORE, encoding="utf-8") as f:
        lore = json.load(f)
    with open(MASTER, encoding="utf-8") as f:
        master = json.load(f)

    entrees = {"cities": {c["id"]: c for c in master["cities"]},
               "pois": {p["id"]: p for p in master["pois"]}}
    liens, orphelins = correspondances(lore, master)
    erreurs, lies = [], set()
    for coll, mid, fiche in liens:
        lies.add((coll, mid))
        au_lore = fiche.get("secret") is True
        au_master = entrees[coll][mid].get("secret") is True
        if au_lore and not au_master:
            erreurs.append("%s : secret au lore, pas au master (%s.%s) — ajouter \"secret\": true"
                           % (fiche["id"], coll, mid))
        elif au_master and not au_lore:
            erreurs.append("%s.%s : secret au master, pas au lore (%s) — cocher « Secret » dans l'éditeur ou retirer la clé"
                           % (coll, mid, fiche["id"]))
    # Secret au master sans fiche de lore liée : décision non tracée au lore.
    for coll, table in entrees.items():
        for mid, e in table.items():
            if e.get("secret") is True and (coll, mid) not in lies:
                erreurs.append("%s.%s : secret au master sans fiche de lore liée" % (coll, mid))

    secrets = {mid for coll, table in entrees.items() for mid, e in table.items() if e.get("secret") is True}
    for r in master.get("lore_roads", []):
        for bout in (r.get("from"), r.get("to")):
            if bout in secrets:
                erreurs.append("route %s : extrémité secrète %s" % (r.get("id"), bout))

    sans_master = [o["id"] for o in orphelins if o.get("secret") is True]
    print("[check_secrets] %d lieu(x)/ville(s) secret(s) au master ; %d fiche(s) secrète(s) sans entrée au master%s"
          % (len(secrets), len(sans_master), (" : " + ", ".join(sorted(sans_master))) if sans_master else ""))
    for e in erreurs:
        print("[FAIL] " + e)
    if erreurs:
        return 1
    print("[OK] lore et master concordent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
