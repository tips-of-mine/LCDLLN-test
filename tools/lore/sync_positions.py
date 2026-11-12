#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_positions.py — recopie les positions posées à la main dans l'éditeur
# (lore) vers le master du monde jouable (v1.0.95, « tu places, le jeu suit »).
#
# Chaîne : éditeur legacy (glisser-déposer, mode « Déplacer ») → lore
#   legacy/world-builder-html/lune-noire-data.json, champs `position_x_m` / `position_z_m`
#   des fiches `villes` et `lieux` → ce script → game/data/world/royaumes/world_master.json,
#   clé `position` = { "x_m": …, "z_m": … } des `cities` et `pois` → tools/world_gen
#   (la ville ou le lieu est posé exactement là, plateau aplani autour).
#
# Unité (identique dans le lore et le master) : MÈTRES depuis le CENTRE de la case de 10 km,
# X vers l'est, Z vers le sud (repère global du master) ; |x|, |z| <= 4300 m.
# null (ou clé absente) = comportement historique : site choisi par le générateur.
#
# Règles :
#   - Le lore est la source : une position du lore est écrite au master (avec la case de la
#     fiche, si elle a changé) ; une position null retire `position` du master.
#   - Correspondance : ville = même `id` ; lieu `lieu_x` = POI `poi_x`, sinon POI de même nom
#     dans la même case, sinon table EXPLICITES ci-dessous. Lieux d'une autre carte (`carte`,
#     ex. les Enfers) ignorés.
#   - Refus (code 2) : position hors de la case, ville ancrée sur un relief (`reliefs`) ;
#     sauf un gouffre (v1.0.107, type `gouffre`) : il suit la ville posée au fond.
#   - Avertissement : position posée sur une fiche sans entrée au master (ignorée).
#   - Édition CHIRURGICALE du texte du master (une ligne par position) : la mise en forme
#     écrite à la main est conservée ; rien d'autre ne change (vérifié avant écriture).
#
# Lancement : python3 tools/lore/sync_positions.py [--check]
#   --check : n'écrit rien, sort en erreur (code 1) si le master n'est pas à jour.

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")

# Écart maximal depuis le centre de la case (m, par axe), comme tools/world_gen.
MAX_POSITION_M = 4300.0

# Lieux du lore dont le POI du master ne suit ni la règle de préfixe ni le nom.
EXPLICITES = {
    "lieu_portail_enfer_1": "poi_porte_1",
    "lieu_portail_enfer_2": "poi_porte_2",
    "lieu_portail_enfer_3": "poi_porte_3",
    "lieu_portail_enfer_4": "poi_porte_4",
    "lieu_portail_enfer_5": "poi_porte_5",
    "lieu_foret_rencontre_t1": "poi_foret_rencontre",
    "lieu_camp_etang_t1": "poi_camp_etang",
    "lieu_champ_premiere_bataille_t1": "poi_premiere_bataille",
    "lieu_temple_methraton_lhynn": "poi_temple_methraton",
}


def position_du_lore(fiche):
    """Position (x, z) en mètres d'une fiche villes/lieux, ou None si non posée.

    Une seule des deux valeurs renseignée est une erreur (ValueError)."""
    x, z = fiche.get("position_x_m"), fiche.get("position_z_m")
    if x is None and z is None:
        return None
    if x is None or z is None:
        raise ValueError("%s : position_x_m et position_z_m vont ensemble" % fiche.get("id"))
    return (float(x), float(z))


def nombre(v):
    """Écrit un nombre de mètres : entier si possible, sinon au décimètre."""
    v = round(float(v), 1)
    return str(int(v)) if v.is_integer() else repr(v)


def correspondances(lore, master):
    """Liste (collection_master, id_master, fiche_lore) des fiches liées au master."""
    cities = {c["id"] for c in master["cities"]}
    pois = {p["id"]: p for p in master["pois"]}
    par_nom = {}
    for p in master["pois"]:
        par_nom.setdefault((p["name"], p["cell"]), []).append(p["id"])
    liens, orphelins = [], []
    for v in lore.get("villes", []):
        if v["id"] in cities:
            liens.append(("cities", v["id"], v))
        else:
            orphelins.append(v)
    for l in lore.get("lieux", []):
        if l.get("carte"):
            continue  # autre carte (Enfers…) : hors de la carte des Royaumes
        pid = None
        if l["id"].startswith("lieu_") and "poi_" + l["id"][5:] in pois:
            pid = "poi_" + l["id"][5:]
        elif len(par_nom.get((l.get("name"), l.get("cell")), [])) == 1:
            pid = par_nom[(l["name"], l["cell"])][0]
        elif EXPLICITES.get(l["id"]) in pois:
            pid = EXPLICITES[l["id"]]
        if pid:
            liens.append(("pois", pid, l))
        else:
            orphelins.append(l)
    return liens, orphelins


def bornes_section(lignes, cle):
    """Indices [début, fin) des lignes de la section de premier niveau `cle` du master."""
    debut = next(i for i, t in enumerate(lignes) if t.startswith('  "%s": [' % cle))
    fin = next(i for i in range(debut + 1, len(lignes)) if re.match(r'  "[^"]+":', lignes[i]) or lignes[i] == "}")
    return debut, fin


def appliquer(texte, changements):
    """Applique au texte du master les changements [(section, id, position|None, case|None)].

    Insère, remplace ou retire la ligne `"position": {…},` (placée après `"cell"`) et
    remplace la valeur de `"cell"` si une case est donnée. Retourne le nouveau texte."""
    lignes = texte.split("\n")
    for section, ident, pos, case in changements:
        debut, fin = bornes_section(lignes, section)
        i_id = next(i for i in range(debut, fin) if lignes[i] == '      "id": "%s",' % ident)
        i_fin = next(i for i in range(i_id, fin) if lignes[i].startswith("    }"))
        i_cell = next(i for i in range(i_id, i_fin) if lignes[i].startswith('      "cell": "'))
        if case:
            lignes[i_cell] = '      "cell": "%s",' % case
        i_pos = next((i for i in range(i_id, i_fin) if lignes[i].startswith('      "position":')), None)
        nouvelle = None if pos is None else '      "position": { "x_m": %s, "z_m": %s },' % (nombre(pos[0]), nombre(pos[1]))
        if i_pos is not None and nouvelle is None:
            del lignes[i_pos]
        elif i_pos is not None:
            lignes[i_pos] = nouvelle
        elif nouvelle is not None:
            lignes.insert(i_cell + 1, nouvelle)
    return "\n".join(lignes)


def main():
    parser = argparse.ArgumentParser(description="Recopie les positions manuelles du lore vers world_master.json")
    parser.add_argument("--check", action="store_true", help="verifie sans ecrire")
    args = parser.parse_args()

    with open(LORE, encoding="utf-8") as f:
        lore = json.load(f)
    with open(MASTER, encoding="utf-8") as f:
        texte = f.read()
    master = json.loads(texte)
    # Villes non déplaçables : ancrées sur une colline ou une crevasse (relief dessiné au
    # centre de la case). Un gouffre (v1.0.107) suit sa ville : position admise.
    ancrees = {r.get("city") for r in master.get("reliefs", []) if r.get("city") and r.get("type") != "gouffre"}
    entrees = {("cities", c["id"]): c for c in master["cities"]}
    entrees.update({("pois", p["id"]): p for p in master["pois"]})

    erreurs, avertissements, changements = [], [], []
    liens, orphelins = correspondances(lore, master)
    vus = {}
    for section, ident, fiche in liens:
        if (section, ident) in vus:
            erreurs.append("%s et %s : meme entree du master %s" % (vus[(section, ident)], fiche["id"], ident))
        vus[(section, ident)] = fiche["id"]
    for fiche in orphelins:
        try:
            if position_du_lore(fiche) is not None:
                avertissements.append("%s : position posee mais aucune entree au master (ignoree)" % fiche["id"])
        except ValueError as e:
            erreurs.append(str(e))
    for section, ident, fiche in liens:
        try:
            pos = position_du_lore(fiche)
        except ValueError as e:
            erreurs.append(str(e))
            continue
        entree = entrees[(section, ident)]
        if pos is not None:
            if abs(pos[0]) > MAX_POSITION_M or abs(pos[1]) > MAX_POSITION_M:
                erreurs.append("%s : position hors de la case (|x|, |z| <= 4300 m)" % fiche["id"])
                continue
            if section == "cities" and ident in ancrees:
                erreurs.append("%s : ville ancree sur un relief (reliefs du master), non deplacable" % fiche["id"])
                continue
            if not re.match(r"^[NS]\d{3}_[EW]\d{3}$", fiche.get("cell") or ""):
                erreurs.append("%s : position sans case valide" % fiche["id"])
                continue
        actuelle = entree.get("position")
        actuelle = None if actuelle is None else (float(actuelle["x_m"]), float(actuelle["z_m"]))
        voulue = None if pos is None else (round(pos[0], 1), round(pos[1], 1))
        case = fiche["cell"] if (pos is not None and fiche["cell"] != entree["cell"]) else None
        if voulue != actuelle or case:
            changements.append((section, ident, voulue, case))

    for a in avertissements:
        print("[avertissement] " + a)
    if erreurs:
        for e in erreurs:
            print("[erreur] " + e, file=sys.stderr)
        sys.exit(2)

    nouveau = appliquer(texte, changements)
    # Contrôle : seules les clés `position` / `cell` des entrées modifiées changent.
    relu = json.loads(nouveau)
    touchees = {(s, i) for s, i, _, _ in changements}
    for section in ("cities", "pois"):
        for a, b in zip(master[section], relu[section]):
            if (section, a["id"]) in touchees:
                a = {k: v for k, v in a.items() if k not in ("position", "cell")}
                b = {k: v for k, v in b.items() if k not in ("position", "cell")}
            if a != b:
                sys.exit("[erreur] controle : entree %s modifiee au-dela de sa position" % a.get("id"))
    if {k: v for k, v in master.items() if k not in ("cities", "pois")} != {k: v for k, v in relu.items() if k not in ("cities", "pois")}:
        sys.exit("[erreur] controle : sections du master modifiees au-dela des positions")

    for section, ident, pos, case in changements:
        quoi = "retiree" if pos is None else "x=%s m, z=%s m" % (nombre(pos[0]), nombre(pos[1]))
        print("%s %s : position %s%s" % (section, ident, quoi, (" ; case " + case) if case else ""))
    posees = sum(1 for _, _, f in liens if position_du_lore(f) is not None)
    print("positions manuelles : %d fiche(s) liee(s) au master posee(s), %d changement(s)" % (posees, len(changements)))
    if args.check:
        if changements:
            print("[check] world_master.json n'est pas a jour : lancer tools/lore/sync_positions.py", file=sys.stderr)
            sys.exit(1)
        print("[check] world_master.json a jour")
        return
    if changements:
        with open(MASTER, "w", encoding="utf-8", newline="") as f:
            f.write(nouveau)
        print("ecrit : " + os.path.relpath(MASTER, REPO))


if __name__ == "__main__":
    main()
