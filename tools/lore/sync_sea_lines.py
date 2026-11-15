#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/lore/sync_sea_lines.py — recopie les lignes maritimes du lore vers le master du monde
# jouable (v1.0.124, décision du 15/11/2026 : « passeur au ponton », les 4 lignes).
#
# Chaîne : lore (legacy/world-builder-html/lune-noire-data.json, collection `sea_lines` :
# escales `stops[]` = ville + case du ponton) → ce script →
# game/data/world/royaumes/world_master.json, clé `sea_lines` → tools/world_gen (position du
# ponton sur la côte de la case, PNJ capitaine, dialogue, world/royaumes/sea_lines.json) →
# serveur (traversée payante, téléportation d'un ponton à l'autre).
#
# Format écrit (une ligne par ligne maritime, dans l'ordre du lore) :
#   { "id": "ligne_maritime_sud", "name": "Ligne maritime du sud", "loop": true,
#     "price_gold_per_leg": 10, "stops": [ { "city": "ville_equiton", "cell": "S005_W004" }, … ] }
#   - loop : la ligne fait une boucle (dernière escale → première) ; sinon aller-retour ;
#   - price_gold_per_leg : prix d'une étape en or ([GD] v1.0.124 : 10, 5 pour une navette à
#     2 escales) ; le prix d'un voyage = étapes du plus court sens × ce prix ;
#   - stops : ville du lore (même identifiant au master) et case du ponton.
#
# Règles :
#   - Le lore est la source : la section est réécrite entièrement ;
#   - Refus (code 2) : escale sans ville du master, case invalide, ligne de moins de 2 escales ;
#   - Édition CHIRURGICALE du texte du master : seules `_comment_sea_lines` et `sea_lines`
#     changent (insérées avant `_comment_lore_roads`, sinon avant `extra_lakes`, la première
#     fois) ; contrôle avant écriture.
#
# Dialogues des passeurs (v1.0.124) : un fichier game/data/dialogues/passeur_<ligne>_<k>.json
# par escale k (dialogue_id du PNJ capitaine posé par tools/world_gen au ponton), avec un
# choix par autre escale : action `sea_travel`, `seaLine` (ligne), `seaStop` (escale
# d'arrivée), prix affiché (le serveur recalcule et fait foi). Réécrits entièrement, vérifiés
# par --check comme le master.
#
# Lancement : python3 tools/lore/sync_sea_lines.py [--check]

import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LORE = os.path.join(REPO, "legacy", "world-builder-html", "lune-noire-data.json")
MASTER = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")

PRIX_ETAPE = 10   # [GD] v1.0.124 : or par étape d'une ligne
PRIX_NAVETTE = 5  # [GD] v1.0.124 : or pour une navette (2 escales)
COMMENTAIRE = ("GENERE par tools/lore/sync_sea_lines.py depuis le lore (sea_lines) — v1.0.124 : lignes "
               "maritimes jouees par un passeur au ponton (capitaine, dialogue, traversee payante et "
               "teleportee d'un ponton a l'autre, LORE_BD 16.50). stops : ville et case du ponton ; "
               "loop : boucle (sinon aller-retour) ; price_gold_per_leg : or par etape. Ne pas editer a la main.")
CASE = re.compile(r"^[NS]\d{3}_[EW]\d{3}$")
DIALOGUES = os.path.join(REPO, "game", "data", "dialogues")


def etapes(ligne, a, b):
    """Nombre d'étapes de l'escale a à b (plus court sens si la ligne boucle)."""
    n = len(ligne["stops"])
    d = abs(b - a)
    return min(d, n - d) if ligne["loop"] else d


def de(nom):
    """« de Brenhaval », « d'Ekvhitorn » (élision devant une voyelle)."""
    return ("d'" if nom[:1].upper() in "AEIOUYÉÈÊÂÎÔÛ" else "de ") + nom


def dialogues(lignes, noms):
    """Dialogues des passeurs : { nom de fichier : texte JSON }."""
    out = {}
    for l in lignes:
        for k, st in enumerate(l["stops"]):
            ici = noms.get(st["city"], st["city"])
            choix = []
            for j, dest in enumerate(l["stops"]):
                if j == k:
                    continue
                prix = etapes(l, k, j) * l["price_gold_per_leg"]
                choix.append({"text": "Embarquer pour %s (%d or)." % (noms.get(dest["city"], dest["city"]), prix),
                              "action": "sea_travel", "seaLine": l["id"], "seaStop": j})
            choix.append({"text": "Pas aujourd'hui.", "action": "end"})
            arbre = {
                "_comment": "GENERE par tools/lore/sync_sea_lines.py (v1.0.124) — passeur de l'escale %d de la ligne %s (%s). "
                            "Le serveur verifie la proximite du ponton et l'or, puis debarque le joueur au ponton d'arrivee. "
                            "Ne pas editer a la main." % (k, l["id"], ici),
                "start": "accueil",
                "nodes": {"count": 1, "0": {
                    "id": "accueil",
                    "lines": {"count": 2,
                              "0": {"text": "Ici le port %s. Où voulez-vous aller avec la %s ?" % (de(ici), l["name"][:1].lower() + l["name"][1:])},
                              "1": {"text": "(Le capitaine montre du menton une barque qui attend au bout du ponton.)", "cue": True}},
                    "choices": dict([("count", len(choix))] + [(str(i), c) for i, c in enumerate(choix)])}},
            }
            out["passeur_%s_%d.json" % (l["id"], k)] = json.dumps(arbre, ensure_ascii=False, indent=4) + "\n"
    return out


def lignes_du_master(lore, master):
    """Entrées `sea_lines` déduites du lore. \\return (lignes, erreurs)."""
    villes = {c["id"] for c in master["cities"]}
    out, erreurs = [], []
    for s in lore.get("sea_lines", []):
        stops = []
        for st in s.get("stops", []):
            vid, cell = st.get("ville_id", ""), st.get("ponton_cell", "")
            if vid not in villes:
                erreurs.append("%s : escale %r sans ville au master" % (s["id"], vid))
            if not CASE.match(cell or ""):
                erreurs.append("%s : case de ponton invalide %r" % (s["id"], cell))
            stops.append({"city": vid, "cell": cell})
        if len(stops) < 2:
            erreurs.append("%s : moins de 2 escales" % s["id"])
        out.append({"id": s["id"], "name": s.get("name", s["id"]), "loop": bool(s.get("loop", False)),
                    "price_gold_per_leg": PRIX_NAVETTE if len(stops) == 2 else PRIX_ETAPE, "stops": stops})
    return out, erreurs


def texte_section(lignes):
    """Lignes de texte des clés `_comment_sea_lines` et `sea_lines` (une ligne par ligne maritime)."""
    q = lambda v: json.dumps(v, ensure_ascii=False)
    t = ['  "_comment_sea_lines": %s,' % q(COMMENTAIRE), '  "sea_lines": [']
    for k, l in enumerate(lignes):
        t.append("    " + json.dumps(l, ensure_ascii=False, separators=(", ", ": ")) + ("," if k + 1 < len(lignes) else ""))
    t.append("  ],")
    return t


def appliquer(texte, lignes):
    """Nouveau texte du master : section remplacée, ou insérée avant `extra_lakes`."""
    ls = texte.split("\n")
    i_com = next((i for i, t in enumerate(ls) if t.startswith('  "_comment_sea_lines":')), None)
    i_deb = next((i for i, t in enumerate(ls) if t.startswith('  "sea_lines": [')), None)
    if i_deb is None:
        # Avant les routes du lore (sync_routes.py réinsère les siennes avant extra_lakes :
        # les deux sections ne se croisent pas), sinon avant extra_lakes.
        i = next((i for i, t in enumerate(ls) if t.startswith('  "_comment_lore_roads":')), None)
        if i is None:
            i = next(i for i, t in enumerate(ls) if t.startswith('  "extra_lakes":'))
        return "\n".join(ls[:i] + texte_section(lignes) + ls[i:])
    i_fin = next(i for i in range(i_deb, len(ls)) if ls[i] == "  ],")
    debut = i_com if i_com is not None else i_deb
    return "\n".join(ls[:debut] + texte_section(lignes) + ls[i_fin + 1:])


def main():
    """Réécrit (ou vérifie avec --check) `sea_lines` du master. \\return code de sortie."""
    ap = argparse.ArgumentParser(description="Recopie les lignes maritimes du lore vers world_master.json")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    lore = json.load(open(LORE, encoding="utf-8"))
    texte = open(MASTER, encoding="utf-8").read()
    master = json.loads(texte)
    lignes, erreurs = lignes_du_master(lore, master)
    if erreurs:
        for e in erreurs:
            print("[FAIL] " + e)
        return 2
    noms = {c["id"]: c.get("name", c["id"]) for c in master["cities"]}
    attendus = dialogues(lignes, noms)
    en_retard = [f for f, txt in attendus.items()
                 if not os.path.exists(os.path.join(DIALOGUES, f)) or open(os.path.join(DIALOGUES, f), encoding="utf-8").read() != txt]
    orphelins = [f for f in os.listdir(DIALOGUES) if f.startswith("passeur_") and f not in attendus]
    nouveau = appliquer(texte, lignes)
    relu = json.loads(nouveau)
    hors = ("sea_lines", "_comment_sea_lines")
    if {k: v for k, v in relu.items() if k not in hors} != {k: v for k, v in master.items() if k not in hors}:
        print("[FAIL] controle : sections du master modifiees au-dela de sea_lines")
        return 2
    if relu["sea_lines"] != lignes:
        print("[FAIL] controle : sea_lines relu different du lore")
        return 2
    print("[sync_sea_lines] %d ligne(s), %d escale(s), %d dialogue(s) de passeur" % (
        len(lignes), sum(len(l["stops"]) for l in lignes), len(attendus)))
    if a.check and (en_retard or orphelins):
        print("[FAIL] dialogues des passeurs pas a jour (%s) : lancer python3 tools/lore/sync_sea_lines.py"
              % ", ".join(en_retard + orphelins))
        return 1
    for f in orphelins:
        os.remove(os.path.join(DIALOGUES, f))
    for f in en_retard:
        with open(os.path.join(DIALOGUES, f), "w", encoding="utf-8", newline="\n") as h:
            h.write(attendus[f])
    if nouveau == texte:
        print("[check] world_master.json a jour" if a.check else "[ok] rien a changer")
        return 0
    if a.check:
        print("[FAIL] world_master.json pas a jour : lancer python3 tools/lore/sync_sea_lines.py")
        return 1
    with open(MASTER, "w", encoding="utf-8", newline="") as f:
        f.write(nouveau)
    print("[ok] world_master.json mis a jour")
    return 0


if __name__ == "__main__":
    sys.exit(main())
