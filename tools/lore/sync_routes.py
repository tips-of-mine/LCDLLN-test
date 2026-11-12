#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_routes.py — recopie les tracés des routes du lore vers le master du monde
# jouable (v1.0.105, « le générateur suit les routes du lore »).
#
# Chaîne : éditeur legacy (mode « 〰 Tracer », LORE_BD §16.26) → lore
#   legacy/world-builder-html/lune-noire-data.json, collection `routes` (type, from_/to_place,
#   from_/to_ville_id, from_/to_lieu_id, waypoints) → ce script →
#   game/data/world/royaumes/world_master.json, clé `lore_roads` → tools/world_gen (Roads.cpp,
#   BuildMacroRoads : la route passe par chaque case de son tracé, en suivant le terrain).
#
# Format écrit (une ligne par route, dans l'ordre du lore) :
#   { "id": "000120", "type": "imperial_road", "from": "ville_lhynn", "to": "ville_moork",
#     "waypoints": ["N001_W010", ..., "S002_E015"] }
#   - type : road | imperial_road | path (les `sea_route` ne sont PAS recopiées : le générateur
#     ne trace rien sur l'eau et n'a pas de mécanisme de ponton) ;
#   - from / to : identifiant du MASTER de l'extrémité — ville (même id que le lore) ou POI
#     (lieu `lieu_x` = POI `poi_x`, même règle que sync_positions.py, plus la table EXTREMITES
#     ci-dessous) ; "" = jonction (embranchement : la route part d'une autre route de la case)
#     ou extrémité sans entrée au master (avertissement) ;
#   - waypoints : cases du tracé lore, extrémités comprises ; sans tracé au lore :
#     [from_place, to_place] (le générateur cherche alors seul son chemin entre les deux).
#
# Règles :
#   - Le lore est la source : la section `lore_roads` est réécrite entièrement à partir du lore
#     (route ajoutée, modifiée ou supprimée dans l'éditeur → suivie ici) ;
#   - Refus (code 2) : route sans case de départ ou d'arrivée, case invalide ;
#   - Édition CHIRURGICALE du texte du master : seules les lignes de `_comment_lore_roads` et de
#     `lore_roads` changent (insérées avant `extra_lakes` la première fois) ; contrôle avant
#     écriture que rien d'autre ne change.
#
# Lancement : python3 tools/lore/sync_routes.py [--check] [--lore <json>] [--master <json>]
#   --check : n'écrit rien, sort en erreur (code 1) si le master n'est pas à jour.
#   --lore / --master : autres fichiers (tests) ; par défaut ceux du dépôt.

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_positions  # noqa: E402  (correspondance lieu du lore -> POI du master)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")

# Types de route du lore tracés sur terre par le générateur.
TYPES_TERRESTRES = ("road", "imperial_road", "path")

# Extrémités dont l'entrée du master ne suit pas la règle générale :
#   (id de route, "from" | "to") -> id du master.
# La Mine de la Traverse (lieu unique du lore, N000_W008, sous la chaîne) a deux entrées au
# master (v1.0.73) : le sentier de Gredhaim mène à l'entrée nord, celui de Kholvard au sud.
#
# Le Hameau de Marn (ancienne ville de départ, N003_W009) n'existe qu'au master, sans fiche au
# lore : la route Haeldorf → Marn (000237, v1.0.106) y arrive par une extrémité vide au lore.
EXTREMITES = {
    ("000212", "to"): "poi_mine_traverse_nord",
    ("000213", "to"): "poi_mine_traverse_sud",
    ("000237", "to"): "hameau_marn",
}
# Lieux du lore qui sont une VILLE du master (même case).
LIEUX_VILLES = {
    "lieu_der_hem_shelbem": "ville_der_hem_shelsem",
}

COMMENTAIRE = (
    "Routes du lore (v1.0.105), recopiees de legacy/world-builder-html/lune-noire-data.json "
    "(collection routes) par tools/lore/sync_routes.py : NE PAS EDITER A LA MAIN, tracer dans "
    "l'editeur (mode Tracer) puis relancer le script. type : road | imperial_road | path "
    "(routes maritimes non recopiees) ; from / to : ville ou POI du master, vide = jonction "
    "(embranchement sur une autre route de la case) ou extremite sans entree au master ; "
    "waypoints : cases du trace, extremites comprises. tools/world_gen (Roads.cpp) trace chaque "
    "route en passant par chaque case (ville de la case, sinon point praticable le plus proche "
    "du centre) et cherche son chemin sur le terrain entre deux cases ; les routes de son propre "
    "reseau ne sont gardees que pour relier les villes de depart sans equivalent au lore "
    "(docs/world/LORE_BD.md section 16.34)."
)

CASE = re.compile(r"^[NS]\d{3}_[EW]\d{3}$")


def extremite(route, cote, lieu_vers_poi, villes, pois):
    """Identifiant du master de l'extrémité `cote` ("from" | "to") d'une route du lore.

    Retourne (id_master, avertissement|None) ; id_master vaut "" pour une jonction ou une
    extrémité sans entrée au master."""
    if (route["id"], cote) in EXTREMITES:
        cible = EXTREMITES[(route["id"], cote)]
        if cible in pois or cible in villes:
            return cible, None
        return "", "%s : extremite %s %s absente du master" % (route["id"], cote, cible)
    ville = route.get(cote + "_ville_id") or ""
    lieu = route.get(cote + "_lieu_id") or ""
    if ville:
        if ville in villes:
            return ville, None
        return "", "%s : ville %s absente du master (extremite %s sur la case)" % (route["id"], ville, cote)
    if lieu:
        if lieu in LIEUX_VILLES and LIEUX_VILLES[lieu] in villes:
            return LIEUX_VILLES[lieu], None
        if lieu in lieu_vers_poi:
            return lieu_vers_poi[lieu], None
        return "", "%s : lieu %s sans POI au master (extremite %s sur la case)" % (route["id"], lieu, cote)
    return "", None  # jonction (embranchement)


def routes_du_master(lore, master):
    """Liste des entrées `lore_roads` déduites du lore, + (erreurs, avertissements, ignorées)."""
    liens, _ = sync_positions.correspondances(lore, master)
    lieu_vers_poi = {f["id"]: i for s, i, f in liens if s == "pois"}
    villes = {c["id"] for c in master["cities"]}
    pois = {p["id"] for p in master["pois"]}
    erreurs, avertissements = [], []
    ignorees = {}
    sortie = []
    for r in lore.get("routes", []):
        t = r.get("type")
        if t not in TYPES_TERRESTRES:
            ignorees[t] = ignorees.get(t, 0) + 1
            continue
        depart, arrivee = r.get("from_place") or "", r.get("to_place") or ""
        cases = list(r.get("waypoints") or []) or [depart, arrivee]
        if not CASE.match(depart) or not CASE.match(arrivee) or len(cases) < 2:
            erreurs.append("%s : case de depart ou d'arrivee manquante" % r.get("id"))
            continue
        mauvaises = [c for c in cases if not CASE.match(c or "")]
        if mauvaises:
            erreurs.append("%s : case invalide %s" % (r["id"], mauvaises[0]))
            continue
        ids = {}
        for cote in ("from", "to"):
            ids[cote], av = extremite(r, cote, lieu_vers_poi, villes, pois)
            if av:
                avertissements.append(av)
        sortie.append({"id": r["id"], "type": t, "from": ids["from"], "to": ids["to"], "waypoints": cases})
    return sortie, erreurs, avertissements, ignorees


def texte_section(routes):
    """Lignes (sans retour final) des clés `_comment_lore_roads` et `lore_roads` du master."""
    q = lambda s: json.dumps(s, ensure_ascii=False)  # noqa: E731
    lignes = ['  "_comment_lore_roads": %s,' % q(COMMENTAIRE), '  "lore_roads": [']
    for k, r in enumerate(routes):
        ligne = '    { "id": %s, "type": %s, "from": %s, "to": %s, "waypoints": [%s] }' % (
            q(r["id"]), q(r["type"]), q(r["from"]), q(r["to"]), ", ".join(q(c) for c in r["waypoints"]))
        lignes.append(ligne + ("," if k + 1 < len(routes) else ""))
    lignes.append("  ],")
    return lignes


def appliquer(texte, routes):
    """Nouveau texte du master : section `lore_roads` remplacée (ou insérée avant `extra_lakes`)."""
    lignes = texte.split("\n")
    nouvelles = texte_section(routes)
    i_com = next((i for i, t in enumerate(lignes) if t.startswith('  "_comment_lore_roads":')), None)
    i_deb = next((i for i, t in enumerate(lignes) if t.startswith('  "lore_roads": [')), None)
    if i_deb is None:
        i_ins = next(i for i, t in enumerate(lignes) if t.startswith('  "extra_lakes": ['))
        return "\n".join(lignes[:i_ins] + nouvelles + lignes[i_ins:])
    i_fin = next(i for i in range(i_deb, len(lignes)) if lignes[i] in ("  ],", "  ]"))
    debut = i_com if i_com is not None and i_com == i_deb - 1 else i_deb
    if lignes[i_fin] == "  ]":  # dernière section du fichier : pas de virgule finale
        nouvelles[-1] = "  ]"
    return "\n".join(lignes[:debut] + nouvelles + lignes[i_fin + 1:])


def main():
    parser = argparse.ArgumentParser(description="Recopie les routes du lore vers world_master.json (lore_roads)")
    parser.add_argument("--check", action="store_true", help="verifie sans ecrire")
    parser.add_argument("--lore", default=LORE, help="fichier de lore (defaut : celui du depot)")
    parser.add_argument("--master", default=MASTER, help="master du monde (defaut : celui du depot)")
    args = parser.parse_args()

    with open(args.lore, encoding="utf-8") as f:
        lore = json.load(f)
    with open(args.master, encoding="utf-8") as f:
        texte = f.read()
    master = json.loads(texte)

    routes, erreurs, avertissements, ignorees = routes_du_master(lore, master)
    for a in avertissements:
        print("[avertissement] " + a)
    if erreurs:
        for e in erreurs:
            print("[erreur] " + e, file=sys.stderr)
        sys.exit(2)

    nouveau = appliquer(texte, routes)
    relu = json.loads(nouveau)
    # Contrôle : seules les clés `lore_roads` / `_comment_lore_roads` changent.
    hors = ("lore_roads", "_comment_lore_roads")
    if {k: v for k, v in master.items() if k not in hors} != {k: v for k, v in relu.items() if k not in hors}:
        sys.exit("[erreur] controle : sections du master modifiees au-dela de lore_roads")
    if relu["lore_roads"] != routes:
        sys.exit("[erreur] controle : lore_roads relu different des routes du lore")

    avant = {r["id"]: r for r in master.get("lore_roads", [])}
    apres = {r["id"]: r for r in routes}
    ajoutees = sorted(set(apres) - set(avant))
    retirees = sorted(set(avant) - set(apres))
    modifiees = sorted(i for i in set(apres) & set(avant) if apres[i] != avant[i])
    jonctions = sum(1 for r in routes if not r["from"] or not r["to"])
    print("routes du lore : %d recopiee(s) (%s), %d extremite(s) sans entree (jonctions comprises) ; ignorees : %s"
          % (len(routes), ", ".join("%s %d" % (t, sum(1 for r in routes if r["type"] == t)) for t in TYPES_TERRESTRES),
             jonctions, ", ".join("%s %d" % kv for kv in sorted(ignorees.items())) or "aucune"))
    print("changements : %d ajoutee(s), %d modifiee(s), %d retiree(s)" % (len(ajoutees), len(modifiees), len(retirees)))
    for i in ajoutees[:10] + modifiees[:10] + retirees[:10]:
        print("  " + i)
    change = nouveau != texte
    if args.check:
        if change:
            print("[check] world_master.json n'est pas a jour : lancer tools/lore/sync_routes.py", file=sys.stderr)
            sys.exit(1)
        print("[check] world_master.json a jour")
        return
    if change:
        with open(args.master, "w", encoding="utf-8", newline="") as f:
            f.write(nouveau)
        print("ecrit : " + os.path.relpath(args.master, REPO))


if __name__ == "__main__":
    main()
