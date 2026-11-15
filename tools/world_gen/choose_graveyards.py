#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/world_gen/choose_graveyards.py — v1.0.122 : choisit les hameaux à priver de cimetière pour
# ramener le total des Royaumes à `graveyards.target` (666, décision du propriétaire, LORE_BD §16.38).
#
# Entrée : le recensement écrit par `lcdlln_world_gen --graveyard-report <fichier>` (une ligne par
# cellule : « cell_n014_e017 2 town_0112@140213:-139874 cell_n014_e017_hameau_0@… », « id@? » pour
# une ligne importée sans position).
# Règle (LORE_BD §16.39) : un à un, le hameau procédural (`_hameau_`) dont le cimetière est le plus
# proche d'un autre cimetière, dans une cellule qui en garde au moins un ; d'abord parmi les cellules
# à --near cases au plus des cellules données par --cells (un lot ouvert), sinon partout.
# Sortie : la liste des identifiants, à ajouter à `graveyards.without_graveyard` du master.
#
# Lancement : python3 tools/world_gen/choose_graveyards.py recensement.txt [--target 666]
#             [--cells N013_E017,S014_E013,...] [--near 4]
import argparse
import math
import re
import sys


def coord(nom):
    """« cell_n014_e017 » ou « N014_E017 » → (nord, est). \\return tuple d'entiers."""
    m = re.match(r"(?:cell_)?([ns])(\d+)_([ew])(\d+)$", nom.lower())
    if not m:
        raise ValueError("cellule invalide : " + nom)
    return ((1 if m[1] == "n" else -1) * int(m[2]), (1 if m[3] == "e" else -1) * int(m[4]))


def lire(chemin):
    """Recensement → liste [id, cellule, (n, e), (x, z) ou None]."""
    tombes = []
    with open(chemin, encoding="utf-8") as f:
        for ligne in f:
            t = ligne.split()
            if not t:
                continue
            for jeton in t[2:]:
                gid, _, xz = jeton.rpartition("@")
                pos = None if xz == "?" else tuple(map(float, xz.split(":")))
                tombes.append([gid, t[0], coord(t[0]), pos])
    return tombes


def choisir(tombes, nombre, lot, pres):
    """Choix glouton de \\p nombre hameaux. \\return liste d'identifiants (ordre du choix)."""
    proche = lambda c: bool(lot) and min(max(abs(c[0] - a), abs(c[1] - b)) for a, b in lot) <= pres
    vivants = list(tombes)
    choix = []
    for _ in range(nombre):
        meilleur = None
        for seulement_proches in ((True, False) if lot else (False,)):
            for g in vivants:
                if "_hameau_" not in g[0] or g[3] is None or (seulement_proches and not proche(g[2])):
                    continue
                if sum(1 for h in vivants if h[1] == g[1]) < 2:
                    continue
                d = min(math.dist(g[3], h[3]) for h in vivants if h is not g and h[3] is not None)
                if meilleur is None or d < meilleur[0]:
                    meilleur = (d, g)
            if meilleur:
                break
        if not meilleur:
            raise SystemExit("[FAIL] plus de hameau à retirer dans une cellule qui garde un cimetière")
        choix.append(meilleur[1][0])
        vivants.remove(meilleur[1])
    return choix


def main():
    """Point d'entrée. \\return code de sortie."""
    ap = argparse.ArgumentParser(description="Hameaux à priver de cimetière (graveyards.without_graveyard).")
    ap.add_argument("recensement")
    ap.add_argument("--target", type=int, default=666)
    ap.add_argument("--cells", default="", help="cellules du lot (N013_E017,...), choix d'abord autour d'elles")
    ap.add_argument("--near", type=int, default=4)
    a = ap.parse_args()
    tombes = lire(a.recensement)
    surplus = len(tombes) - a.target
    print("[choose_graveyards] %d cimetiere(s), cible %d" % (len(tombes), a.target), file=sys.stderr)
    if surplus <= 0:
        print("[choose_graveyards] rien a retirer%s" % ("" if surplus == 0 else " (il en manque %d)" % -surplus), file=sys.stderr)
        return 0 if surplus == 0 else 1
    lot = [coord(c) for c in a.cells.split(",") if c]
    for gid in choisir(tombes, surplus, lot, a.near):
        print(gid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
