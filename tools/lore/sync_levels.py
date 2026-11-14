#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_levels.py — recopie les niveaux de référence des régions du lore vers le
# master du monde jouable (v1.0.118, décision du 13/11/2026 : « couleur du nom de zone »).
#
# Chaîne : lore (legacy/world-builder-html/lune-noire-data.json, `regions[].niveau_par_faction`,
# identifiants de faction du LORE) → ce script → game/data/world/royaumes/world_master.json,
# clé `levels` des `regions` = { "<faction de jeu>": [min, max], … } (identifiants de
# game/data/races/factions.json) → tools/world_gen (places.json) → client (couleur du nom de
# la zone dans la bannière d'entrée selon l'écart entre le niveau du personnage et la région).
#
# Règles :
#   - Correspondance des régions : même identifiant, sinon table REGIONS_LORE ; une région
#     du master sans région du lore liée (ou sans `niveau_par_faction`) n'a pas de `levels`.
#   - Correspondance des factions : table FACTIONS (faction de jeu → faction du lore) ; une
#     faction absente de la fiche du lore prend `niveau_ref` de la région.
#   - Édition CHIRURGICALE du texte du master : une ligne `"levels": {…}` par région, placée
#     après `n_max` ; le reste du fichier est conservé octet pour octet.
#
# Lancement : python3 tools/lore/sync_levels.py [--check]
#   --check : n'écrit rien, sort en erreur (code 1) si le master n'est pas à jour.

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")
FACTIONS_JEU = os.path.join(REPO, "game", "data", "races", "factions.json")

# Faction de jeu (factions.json) → faction du lore (niveau_par_faction).
FACTIONS = {
    "lumiere": "chevaliers_de_la_lumiere",
    "justice": "chevaliers_de_la_justice",
    "lune_noire": "faction_black_moon",
    "dzorak": "dzorak_orcs",
    "legion": "faction_legion_infernale",
    "dragons": "faction_dragon_knights",
    "serpent": "maison_du_serpent",
    "naine": "faction_dwarves",
    "elfe": "faction_hauts_elfes",
    "empire_hynn": "faction_legions_imperiales",
}

# Région du master → région du lore, quand les identifiants diffèrent.
REGIONS_LORE = {
    "marches_du_nord": "region_marches",
    "marches_du_nord_ouest": "region_marches",
    "hishtarland": "region_hishtarland",
    "marche_de_lest": "region_marche_est",
    "marais_maudits": "marais_maudits_t11",
    "ile_de_la_treve": "region_ile_de_la_treve",
}


def bande(b):
    """Bande {min, max} du lore → [min, max] entiers, ou None si invalide."""
    if not isinstance(b, dict):
        return None
    lo, hi = b.get("min"), b.get("max")
    if not isinstance(lo, int) or not isinstance(hi, int) or not 1 <= lo <= hi <= 70:
        return None
    return [lo, hi]


def niveaux_attendus(lore, master):
    """Ligne `levels` attendue par région du master. \\return dict id → dict faction → [min, max]."""
    par_id = {r["id"]: r for r in lore.get("regions", [])}
    out = {}
    for r in master["regions"]:
        fiche = par_id.get(REGIONS_LORE.get(r["id"], r["id"]))
        if not fiche or not fiche.get("niveau_par_faction"):
            continue
        defaut = bande(fiche.get("niveau_ref"))
        npf = fiche["niveau_par_faction"]
        lv = {}
        for jeu, lore_id in FACTIONS.items():
            b = bande(npf.get(lore_id)) or defaut
            if b:
                lv[jeu] = b
        if lv:
            out[r["id"]] = lv
    return out


def ligne_levels(lv, indent):
    """Texte de la ligne `"levels": {...}` (ordre de FACTIONS, compact)."""
    corps = ", ".join('"%s": [%d, %d]' % (k, lv[k][0], lv[k][1]) for k in FACTIONS if k in lv)
    return '%s"levels": {%s}' % (indent, corps)


def reecrire(texte, attendus):
    """Applique les lignes `levels` au texte du master. \\return nouveau texte."""
    debut = texte.index('"regions": [')
    fin = texte.index("\n  ]", debut)
    bloc = texte[debut:fin]
    # Chaque objet de région : de `{` à `}` au niveau d'indentation 4.
    def objet(m):
        corps = m.group(0)
        rid = re.search(r'"id": "([^"]+)"', corps).group(1)
        corps = re.sub(r',\n\s*"levels": \{[^\n]*\}', "", corps)
        if rid in attendus:
            nmax = re.search(r'\n(\s*)"n_max": [-0-9]+', corps)
            corps = corps[:nmax.end()] + ",\n" + ligne_levels(attendus[rid], nmax.group(1)) + corps[nmax.end():]
        return corps
    nouveau = re.sub(r"\{\n      \"id\".*?\n    \}", objet, bloc, flags=re.S)
    return texte[:debut] + nouveau + texte[fin:]


def main():
    """Synchronise (ou vérifie avec --check) les niveaux des régions. \\return code de sortie."""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="vérifie sans écrire (code 1 si en retard)")
    args = ap.parse_args()
    with open(LORE, encoding="utf-8") as f:
        lore = json.load(f)
    with open(MASTER, encoding="utf-8") as f:
        texte = f.read()
    master = json.loads(texte)
    with open(FACTIONS_JEU, encoding="utf-8") as f:
        connues = {x["id"] for x in json.load(f)["factions"]}
    inconnues = sorted(set(FACTIONS) - connues)
    if inconnues:
        print("[FAIL] factions de jeu inconnues dans FACTIONS : " + ", ".join(inconnues))
        return 2
    attendus = niveaux_attendus(lore, master)
    nouveau = reecrire(texte, attendus)
    verif = json.loads(nouveau)
    for r in verif["regions"]:
        if r.get("levels") != attendus.get(r["id"]):
            print("[FAIL] région %s : levels mal écrits" % r["id"])
            return 2
    sans = [r["id"] for r in master["regions"] if r["id"] not in attendus]
    print("[sync_levels] %d région(s) avec niveaux, %d sans (%s)" % (len(attendus), len(sans), ", ".join(sans)))
    if nouveau == texte:
        print("[check] world_master.json a jour" if args.check else "[ok] rien a changer")
        return 0
    if args.check:
        print("[FAIL] world_master.json pas a jour : lancer python3 tools/lore/sync_levels.py")
        return 1
    with open(MASTER, "w", encoding="utf-8", newline="") as f:
        f.write(nouveau)
    print("[ok] world_master.json mis a jour")
    return 0


if __name__ == "__main__":
    sys.exit(main())
