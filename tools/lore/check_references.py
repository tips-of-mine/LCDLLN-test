#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/check_references.py — liste les références vers des identifiants inexistants
# dans le fichier de lore (legacy/world-builder-html/lune-noire-data.json) (v1.0.103).
#
# Une « référence » est la valeur d'un champ qui désigne une autre fiche par son
# identifiant : case d'une ville, région d'une case, PNJ d'une étape de quête, butin d'une
# créature, etc. La liste des champs contrôlés est la table REFERENCES ci-dessous (champs
# repérés dans le fichier v1.0.102 : seuls ceux dont les valeurs sont des identifiants).
# Sont volontairement exclus les champs libres ou d'énumération : `type`, `category`, `tags`,
# `faction_control` (texte libre et clés du générateur de services : « lune_noire »,
# « empire_humain »…), `zones[].faction`, `rewards.*_unlock`, le registre des décisions
# (`regle_canon_registre_decisions`, dont `id` peut citer un champ ou une liste).
#
# Identifiants connus : `id` de toutes les fiches des collections de premier niveau (cells,
# villes, lieux, quêtes, PNJ…), des sous-objets de `lore` (religions, lieux de culte,
# événements), de `hydrography` (lacs, rivières, ponts), des compétences des arbres
# (`skill_trees[].skills[].id`) ; plus les noms des collections elles-mêmes (une règle
# transverse peut s'appliquer à « factions »). Une case de la carte des Enfers s'écrit
# « enfers:<case> » (zones_chasse, v1.0.71) : elle est valide si <case> a la forme d'une case.
#
# Seules les valeurs qui ont la forme d'un identifiant sont contrôlées (lettres, chiffres,
# « _ » et « - », sans espace ni point) : une source citée en clair (« [BD-T0] … ») ou un
# chemin (« lore_t1.magic… ») n'est pas une référence.
#
# Sortie : une ligne par référence orpheline (collection, fiche, champ, valeur) avec une
# suggestion d'identifiant proche quand il en existe une (difflib, à vérifier à la main),
# puis le total. Lancement :
#   python3 tools/lore/check_references.py            # rapport, code 0
#   python3 tools/lore/check_references.py --strict   # code 1 s'il reste une orpheline
#   python3 tools/lore/check_references.py --fichier <chemin.json>
# Python standard uniquement.

import argparse
import difflib
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")

# Forme d'un identifiant et d'une case (N000_E000).
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_\-]*$")
CASE_RE = re.compile(r"^[NS]\d{3}_[EW]\d{3}$")

# (racine, champ) : la racine est le chemin (pointé) d'une liste de fiches, le champ le
# chemin (pointé) de la référence dans une fiche. Les listes rencontrées sur le chemin sont
# parcourues ; la valeur finale peut être une chaîne ou une liste de chaînes.
REFERENCES = [
    ("cells", "region_id"), ("cells", "province"), ("cells", "creature_ids"), ("cells", "connects_to"),
    ("villes", "cell"), ("villes", "anchor_tile"), ("villes", "footprint_tiles"), ("villes", "region_id"),
    ("villes", "province"), ("villes", "service_npcs.race_id"), ("villes", "port.ponton_cell"),
    ("villes", "port.vers_cell"), ("villes", "port.destinations"),
    ("lieux", "cell"), ("lieux", "footprint_tiles"), ("lieux", "region_id"), ("lieux", "province"),
    ("lieux", "zone_id"), ("lieux", "type"),
    ("quests", "source_lore"), ("quests", "region_id"), ("quests", "etapes_struct.ville_id"),
    ("quests", "etapes_struct.lieu_id"), ("quests", "etapes_struct.pnj_id"), ("quests", "etapes_struct.cell"),
    ("quests", "for_factions"), ("quests", "for_races"), ("quests", "for_classes"),
    ("quests", "required_classes"), ("quests", "counter_quest_id"), ("quests", "counter_quest_factions"),
    ("quests", "counter_quest_races"), ("quests", "unlock_conditions.id"),
    ("quests", "unlock_conditions.quest_id"), ("quests", "unlock_conditions.exploit_id"),
    ("quests", "unlock_conditions.item_id"), ("quests", "unlock_conditions.faction_id"),
    ("quests", "rewards.exploit"), ("quests", "rewards.exploits"), ("quests", "rewards.exploit_id"),
    ("quests", "rewards.items"), ("quests", "rewards.item"), ("quests", "rewards.loots.loot_id"),
    ("quests", "rewards.companion"), ("quests", "rewards.companions_unlocked"), ("quests", "rewards.mount"),
    ("quests", "rewards.mounts"), ("quests", "rewards.unlock_raid"),
    ("quests", "rewards.faction_bonus.faction_id"),
    ("raids", "location_ville_id"), ("raids", "for_factions"), ("raids", "for_races"),
    ("raids", "required_classes"), ("raids", "rewards.exploit"), ("raids", "rewards.exploit_id"),
    ("raids", "rewards.items"), ("raids", "rewards.loots.loot_id"),
    ("persons", "faction_id"), ("persons", "race_id"), ("persons", "class_id"), ("persons", "ville_ids"),
    ("persons", "zone_ids"), ("persons", "dialogue_ids"), ("persons", "special_items"), ("persons", "tomes"),
    ("creatures", "region_id"), ("creatures", "ville_id"), ("creatures", "faction_id"),
    ("creatures", "zones_chasse"), ("creatures", "drops.loot_id"), ("creatures", "drops"),
    ("creatures", "raid_id"), ("creatures", "familier_famille"),
    ("dialogues", "faction_id"), ("dialogues", "race_id"), ("dialogues", "location_ville_id"),
    ("timeline", "factions"), ("timeline", "ref_id"),
    ("factions", "faction_relations.faction_id"), ("factions", "ville_relations.ville_id"),
    ("factions", "region_id"), ("factions", "region_ids"), ("factions", "territory.lieu_id"),
    ("factions", "territory.comptoir.ville_id"), ("factions", "territory.comptoir.cell"),
    ("factions", "game_design.depart.id"), ("factions", "game_design.depart.cell"),
    ("factions", "game_design.depart.pres_de"), ("factions", "game_design.depart.region.cells"),
    ("routes", "from_ville_id"), ("routes", "to_ville_id"), ("routes", "from_lieu_id"),
    ("routes", "to_lieu_id"), ("routes", "waypoints"), ("routes", "from_place"), ("routes", "to_place"),
    ("routes", "sea_line_id"), ("routes", "type"),
    ("loots", "source_monsters"), ("loots", "source_villes"), ("loots", "source_quests"),
    ("loots", "source_raids"), ("loots", "eligible_races"), ("loots", "eligible_classes"),
    ("regions", "parent_region"),
    ("competences", "usable_by"), ("competences", "prerequisite"),
    ("skill_trees", "class_id"),
    ("races", "class_access"), ("races", "faction_affiliations.faction_id"),
    ("classes", "faction_affiliations.faction_id"),
    ("story_arcs", "linked_quest_ids"), ("story_arcs", "linked_raid_ids"), ("story_arcs", "child_arc_ids"),
    ("story_arcs", "region_id"), ("story_arcs", "zone_id"), ("story_arcs", "ville_id"),
    ("exploits", "location.ville_id"), ("exploits", "eligible.factions"), ("exploits", "eligible.races"),
    ("exploits", "eligible.classes"),
    ("weapons", "eligible_factions"), ("weapons", "eligible_classes"), ("weapons", "eligible_races"),
    ("mounts", "eligible_factions"), ("mounts", "eligible_races"), ("mounts", "eligible_classes"),
    ("mounts", "default_for.factions"), ("mounts", "default_for.races"), ("mounts", "default_for.classes"),
    ("familiar_families", "creature_ids"),
    ("sea_lines", "stops.ville_id"), ("sea_lines", "stops.ponton_cell"), ("sea_lines", "rule_id"),
    ("companions", "invocable_by"),
    ("regles_transverses", "applies_to"),
    ("scenarios", "cell"), ("scenarios", "cells"), ("scenarios", "quests"), ("scenarios", "raids"),
    ("scenarios", "prerequis_raid"), ("scenarios", "recette"), ("scenarios", "potion"),
    ("scenarios", "entree"), ("scenarios", "boss"), ("scenarios", "regle"),
    ("lore.religions", "person_ids"), ("lore.religions", "faction_ids"), ("lore.religions", "race_ids"),
    ("lore.religions", "class_ids"), ("lore.religions", "divinites.person_id"),
    ("lore.religions", "clerge.person_ids"), ("lore.religions", "regles_liees"),
    ("hydrography.rivers", "cells"), ("hydrography.rivers", "source.cell"),
    ("hydrography.rivers", "source.ville_id"), ("hydrography.rivers", "traverse_villes.ville_id"),
    ("hydrography.rivers", "traverse_villes.cell"), ("hydrography.lakes", "cells"),
    ("hydrography.bridges", "cell"),
]


def chemin(obj, segments):
    """Valeurs atteintes en suivant `segments` (liste de clés) depuis `obj`.

    Les listes rencontrées (y compris la valeur finale) sont parcourues élément par
    élément. \\return liste des valeurs feuilles (chaînes ou autres). Pas d'effet de bord."""
    if isinstance(obj, list):
        out = []
        for x in obj:
            out.extend(chemin(x, segments))
        return out
    if not segments:
        return [obj]
    if not isinstance(obj, dict) or segments[0] not in obj:
        return []
    return chemin(obj[segments[0]], segments[1:])


def ids_connus(data):
    """Ensemble des identifiants définis dans `data` (voir l'en-tête du fichier).

    \\param data lore chargé (dict). \\return set de chaînes. Pas d'effet de bord."""
    ids = set(k for k in data.keys())
    for coll, val in data.items():
        if isinstance(val, list):
            for x in val:
                if isinstance(x, dict) and isinstance(x.get("id"), str):
                    ids.add(x["id"])

    # Ajoute à ids tout « id » chaîne trouvé à n'importe quelle profondeur de o (lore, hydrographie, arbres).
    def tous_les_id(o):
        if isinstance(o, dict):
            if isinstance(o.get("id"), str):
                ids.add(o["id"])
            for v in o.values():
                tous_les_id(v)
        elif isinstance(o, list):
            for v in o:
                tous_les_id(v)

    tous_les_id(data.get("lore", {}))
    tous_les_id(data.get("hydrography", {}))
    for t in data.get("skill_trees", []):
        for s in t.get("skills", []) if isinstance(t, dict) else []:
            if isinstance(s, dict) and isinstance(s.get("id"), str):
                ids.add(s["id"])
    return ids


def est_valide(valeur, ids):
    """Vrai si `valeur` (chaîne de forme identifiant) désigne une fiche existante."""
    if valeur in ids:
        return True
    if valeur.startswith("enfers:"):
        return bool(CASE_RE.match(valeur[len("enfers:"):]))
    return False


def orphelines(data):
    """Liste des références orphelines de `data`.

    \\return liste de dicts {racine, fiche, champ, valeur, suggestion} dans l'ordre de
    REFERENCES puis des fiches. Pas d'effet de bord."""
    ids = ids_connus(data)
    tri = sorted(ids)
    out = []
    for racine, champ in REFERENCES:
        fiches = chemin(data, racine.split("."))
        for fiche in fiches:
            if not isinstance(fiche, dict):
                continue
            for v in chemin(fiche, champ.split(".")):
                if not isinstance(v, str) or not v or not ID_RE.match(v.replace("enfers:", "", 1)):
                    continue
                if est_valide(v, ids):
                    continue
                proches = difflib.get_close_matches(v, tri, n=1, cutoff=0.8)
                out.append({"racine": racine, "fiche": fiche.get("id") or fiche.get("class_id") or "?",
                            "champ": champ, "valeur": v, "suggestion": proches[0] if proches else ""})
    return out


def main():
    """Point d'entrée : affiche le rapport ; code 1 avec --strict s'il reste une orpheline."""
    ap = argparse.ArgumentParser(description="Références orphelines du lore")
    ap.add_argument("--fichier", default=LORE, help="fichier de lore (défaut : lune-noire-data.json)")
    ap.add_argument("--strict", action="store_true", help="code de sortie 1 s'il reste une orpheline")
    args = ap.parse_args()
    with open(args.fichier, encoding="utf-8") as f:
        data = json.load(f)
    res = orphelines(data)
    for o in res:
        sug = f"  (proche : {o['suggestion']})" if o["suggestion"] else ""
        print(f"{o['racine']}[{o['fiche']}].{o['champ']} -> {o['valeur']}{sug}")
    valeurs = len({o["valeur"] for o in res})
    print(f"{len(res)} référence(s) orpheline(s), {valeurs} identifiant(s) inexistant(s), "
          f"{len(REFERENCES)} champs contrôlés.")
    return 1 if (args.strict and res) else 0


if __name__ == "__main__":
    sys.exit(main())
