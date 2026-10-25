#!/usr/bin/env python3
# Verifie les services des villes et leurs PNJ de service dans le fichier de lore,
# par rapport a la grille game_design.cities_services.grille.
#
# Controles :
#   - JSON valide ; lune-noire-data.json identique a lune-noire-data-v20.json ;
#   - chaque ville a une taille connue de la grille ;
#   - nombre d'exemplaires de chaque service dans [min, max] (ajustements compris),
#     ecurie = sorties terrestres (minimum 1), port / arene seulement dans les
#     villes listees, cimetiere dans chaque ville habitee ;
#   - "services" = exactement les services qui ont au moins un PNJ ;
#   - PNJ : id et nom uniques dans tout le monde, metier coherent, race et genre
#     valides, aucun mot repris des BD ; pas plus de PNJ de service que la
#     population maximale de la taille.
#
# Lancement : python tools/lore/validate_city_services.py   (code retour 1 si erreur)
import json
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_city_services as gen  # noqa: E402

GENRES = {"male", "female"}


def valider(data, erreurs):
    """Ajoute a `erreurs` chaque ecart a la grille trouve dans `data`."""
    grille = data.get("game_design", {}).get("cities_services", {}).get("grille")
    if not grille:
        erreurs.append("game_design.cities_services.grille absente")
        return
    races = {r["id"] for r in data.get("races", [])}
    interdits = gen.noms_interdits(data)
    sorties = gen.sorties_terrestres(data, grille)
    ids_vus, noms_vus = set(), set()

    for v in data["villes"]:
        vid, taille = v["id"], v.get("taille")
        if taille not in grille["tailles"]:
            erreurs.append(f"{vid}: taille inconnue {taille!r}")
            continue
        pnjs = v.get("service_npcs", [])
        compte = Counter(p["service"] for p in pnjs)

        if taille == "sans_service":
            if pnjs or v.get("services"):
                erreurs.append(f"{vid}: ville sans service mais services/PNJ presents")
            continue

        regles = dict(grille["tailles"][taille]["services"])
        regles.update(grille.get("ajustements", {}).get(vid, {}))
        for service in gen.SERVICES:
            n = compte.get(service, 0)
            if service == "ecurie":
                attendu = max(grille["ecurie"]["minimum"], sorties.get(vid, 0))
                if n != attendu:
                    erreurs.append(f"{vid}: ecurie {n}, attendu {attendu} (sorties terrestres)")
            elif service == "port":
                if n != (1 if vid in grille["ports"] else 0):
                    erreurs.append(f"{vid}: port {n} non conforme a grille.ports")
            elif service == "arene":
                if n != (1 if vid in grille["arenes"] else 0):
                    erreurs.append(f"{vid}: arene {n} non conforme a grille.arenes")
            else:
                mini, maxi = regles.get(service, [0, 0])
                if not mini <= n <= maxi:
                    erreurs.append(f"{vid}: {service} = {n}, attendu entre {mini} et {maxi} ({taille})")
        inconnus = set(compte) - set(gen.SERVICES)
        if inconnus:
            erreurs.append(f"{vid}: services inconnus {sorted(inconnus)}")
        if v.get("services") != [s for s in gen.SERVICES if compte.get(s)]:
            erreurs.append(f"{vid}: champ services incoherent avec ses PNJ")
        if len(pnjs) > grille["tailles"][taille]["pnj"][1]:
            erreurs.append(f"{vid}: {len(pnjs)} PNJ de service, plus que la population maximale")

        for p in pnjs:
            pid = p.get("id", "")
            if pid in ids_vus:
                erreurs.append(f"{vid}: id de PNJ en double {pid}")
            ids_vus.add(pid)
            nom = gen.norm(p.get("name", ""))
            if not nom or nom in noms_vus:
                erreurs.append(f"{vid}: nom de PNJ vide ou en double {p.get('name')!r}")
            noms_vus.add(nom)
            if gen.mots(p.get("name", "").replace("-", " ")) & interdits:
                erreurs.append(f"{vid}: {p['name']!r} reprend un nom des BD ou du monde")
            if p.get("metier") != gen.METIERS.get(p.get("service")):
                erreurs.append(f"{pid}: metier {p.get('metier')!r} incoherent avec {p.get('service')!r}")
            if p.get("race_id") not in races:
                erreurs.append(f"{pid}: race inconnue {p.get('race_id')!r}")
            if p.get("gender") not in GENRES:
                erreurs.append(f"{pid}: genre invalide {p.get('gender')!r}")


def main():
    erreurs = []
    textes = {}
    for nom in gen.LORE_FILES:
        chemin = os.path.join(gen.LORE_DIR, nom)
        with open(chemin, encoding="utf-8") as f:
            textes[nom] = f.read()
    if len(set(textes.values())) != 1:
        erreurs.append(f"{' et '.join(gen.LORE_FILES)} different : les garder identiques")
    try:
        data = json.loads(textes[gen.LORE_FILES[0]])
    except json.JSONDecodeError as e:
        print(f"ERREUR : JSON invalide : {e}")
        return 1
    valider(data, erreurs)

    if erreurs:
        print(f"ERREUR : {len(erreurs)} ecart(s) :")
        for e in erreurs:
            print("  -", e)
        return 1
    nb = sum(len(v.get("service_npcs", [])) for v in data["villes"])
    print(f"OK : {len(data['villes'])} villes, {nb} PNJ de service conformes a la grille")
    return 0


if __name__ == "__main__":
    sys.exit(main())
