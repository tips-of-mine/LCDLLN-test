#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/test_sync_routes.py — tests de tools/lore/sync_routes.py (v1.0.105).
#
# Python standard seulement (~2 s), sur des COPIES du lore et du master (dossier temporaire) :
#   1. le master du dépôt est à jour (--check, code 0) ;
#   2. master sans `lore_roads` : --check échoue (code 1) ; une synchronisation insère la
#      section, une seconde ne change rien (idempotence) et le résultat est identique au
#      master du dépôt ; seules `lore_roads` / `_comment_lore_roads` diffèrent de l'entrée ;
#   3. une route retracée dans le lore (waypoint ajouté) et une route maritime ajoutée :
#      la route retracée suit, la route maritime n'est pas recopiée ;
#   4. extrémités : ville du master, lieu -> POI, jonction vide, table EXTREMITES ;
#   5. une route sans case de départ est refusée (code 2) et le master n'est pas écrit.
#
# Lancement : python3 tools/lore/test_sync_routes.py   (code 0 = vert)

import json
import os
import shutil
import subprocess
import sys
import tempfile

ICI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(ICI))
SCRIPT = os.path.join(ICI, "sync_routes.py")
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")

echecs = 0


def verifie(ok, quoi):
    """Affiche le résultat d'une vérification et compte les échecs."""
    global echecs
    print(("[ OK ] " if ok else "[FAIL] ") + quoi)
    if not ok:
        echecs += 1


def lance(*args):
    """Lance sync_routes.py (sans cache de bytecode) ; retourne (code, sortie)."""
    p = subprocess.run([sys.executable, "-B", SCRIPT] + list(args), capture_output=True, text=True, encoding="utf-8")
    return p.returncode, p.stdout + p.stderr


def lit(chemin):
    with open(chemin, encoding="utf-8") as f:
        return f.read()


def main():
    code, _ = lance("--check")
    verifie(code == 0, "master du depot a jour (--check)")

    tmp = tempfile.mkdtemp(prefix="sync_routes_")
    try:
        lore = os.path.join(tmp, "lore.json")
        master = os.path.join(tmp, "master.json")
        shutil.copy(LORE, lore)
        # Master sans la section : retirer les lignes de `_comment_lore_roads` et `lore_roads`.
        lignes = lit(MASTER).split("\n")
        i0 = next(i for i, t in enumerate(lignes) if t.startswith('  "_comment_lore_roads":'))
        i1 = next(i for i in range(i0, len(lignes)) if lignes[i] == "  ],")
        with open(master, "w", encoding="utf-8", newline="") as f:
            f.write("\n".join(lignes[:i0] + lignes[i1 + 1:]))
        sans = json.loads(lit(master))
        verifie("lore_roads" not in sans, "copie du master sans lore_roads")

        code, _ = lance("--check", "--lore", lore, "--master", master)
        verifie(code == 1, "--check echoue sur un master pas a jour (code 1)")
        code, _ = lance("--lore", lore, "--master", master)
        premier = lit(master)
        code2, sortie = lance("--lore", lore, "--master", master)
        verifie(code == 0 and code2 == 0 and lit(master) == premier and "0 ajoutee(s), 0 modifiee(s), 0 retiree(s)" in sortie,
                "synchronisation idempotente (second passage sans changement)")
        verifie(premier == lit(MASTER), "resultat identique au master du depot")
        apres = json.loads(premier)
        hors = ("lore_roads", "_comment_lore_roads")
        verifie({k: v for k, v in apres.items() if k not in hors} == sans, "seules lore_roads et son commentaire changent")
        code, _ = lance("--check", "--lore", lore, "--master", master)
        verifie(code == 0, "--check vert apres synchronisation")

        routes = {r["id"]: r for r in apres["lore_roads"]}
        d = json.loads(lit(lore))
        terrestres = [r for r in d["routes"] if r["type"] in ("road", "imperial_road", "path")]
        verifie(len(routes) == len(terrestres) and all(r["type"] != "sea_route" for r in apres["lore_roads"]),
                "toutes les routes terrestres recopiees, aucune route maritime")
        vm = routes.get("000120", {})
        verifie(vm.get("type") == "imperial_road" and vm.get("from") == "ville_lhynn" and vm.get("to") == "ville_moork"
                and len(vm.get("waypoints", [])) == 34, "Via Magna : voie imperiale Lhynn -> Moork, 34 cases")
        verifie(routes.get("000155", {}).get("from") == "" and routes["000155"]["waypoints"][0] == "S007_E006",
                "embranchement 000155 : depart en jonction (vide) sur S007_E006")
        verifie(routes.get("000134", {}).get("to") == "poi_grotte_contrebandiers", "lieu du lore -> POI du master")
        verifie(routes.get("000212", {}).get("to") == "poi_mine_traverse_nord"
                and routes.get("000213", {}).get("to") == "poi_mine_traverse_sud", "table EXTREMITES (Mine de la Traverse)")
        sans_trace = next(r for r in d["routes"] if r["type"] == "road" and not r.get("waypoints"))
        verifie(routes[sans_trace["id"]]["waypoints"] == [sans_trace["from_place"], sans_trace["to_place"]],
                "route sans trace au lore : [depart, arrivee]")

        # 3. Route retracée + route maritime ajoutée dans le lore.
        r79 = next(r for r in d["routes"] if r["id"] == "000079")
        r79["waypoints"] = r79["waypoints"][:2] + ["S008_E006"] + r79["waypoints"][2:]
        d["routes"].append({"id": "999999", "type": "sea_route", "from_place": "N017_W010", "from_ville_id": "town_brinehaven",
                            "from_lieu_id": "", "to_place": "N019_W010", "to_ville_id": "town_truceholm", "to_lieu_id": "",
                            "description": "", "notes": [], "waypoints": ["N017_W010", "N018_W010", "N019_W010"]})
        with open(lore, "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
        code, sortie = lance("--lore", lore, "--master", master)
        relu = {r["id"]: r for r in json.loads(lit(master))["lore_roads"]}
        verifie(code == 0 and "S008_E006" in relu["000079"]["waypoints"] and "999999" not in relu
                and "1 modifiee(s)" in sortie, "route retracee suivie, route maritime ignoree")

        # 5. Route sans case de départ : refus, master inchangé.
        avant = lit(master)
        d["routes"].append({"id": "999998", "type": "road", "from_place": "", "from_ville_id": "", "from_lieu_id": "",
                            "to_place": "N019_W010", "to_ville_id": "town_truceholm", "to_lieu_id": "",
                            "description": "", "notes": []})
        with open(lore, "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
        code, _ = lance("--lore", lore, "--master", master)
        verifie(code == 2 and lit(master) == avant, "route sans case de depart refusee (code 2), master inchange")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if echecs:
        print("test_sync_routes : %d echec(s)" % echecs, file=sys.stderr)
        sys.exit(1)
    print("test_sync_routes : tout est vert")


if __name__ == "__main__":
    main()
