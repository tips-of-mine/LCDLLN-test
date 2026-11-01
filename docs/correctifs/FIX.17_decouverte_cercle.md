# FIX.17 — Zone découverte : bord rond et lisse

## Symptôme

Après FIX.16 (cases de 62,5 m), la zone découverte n'est plus un carré mais
un octogone en marches d'escalier (capture du 01/11, hameau de Marn).

## Cause

FIX.16 dégageait autour de chaque case découverte un disque à peine plus
grand que la case (0,75 case) : seuls les coins de chaque marche étaient
arrondis, le contour suivait encore les bords des cases de 62,5 m.

## Correction

`IsPointRevealedSmooth` (`src/shared/world/MapDiscovery.{h,cpp}`) : un point
est affiché découvert si la **moyenne gaussienne** (écart type 62,5 m) de
l'état des 5 × 5 cases qui l'entourent atteint 0,5. Le contour est la courbe
de niveau de ce champ lissé : un bord continu et rond, sans marches.

Voile (`WorldMapImGuiRenderer.cpp`) : par case, selon son voisinage 5 × 5 —
tout découvert : rien ; rien de découvert : voile plein ; mélange : case
subdivisée (≥ 4 px par sous-case) et voilée sous-case par sous-case. Les
états des cases visibles sont lus une fois par image dans une grille locale.

Mesure (test) : bord entre 260 et 340 m du joueur, rapport rayon max / min
< 1,16 — la petite ondulation restante vient de la grille de 62,5 m elle-même
(une case est découverte si son CENTRE est à moins de 300 m).

## Déploiement

✅ client uniquement (affichage). Aucune donnée ni opcode modifié.

## Tests

`map_discovery_tests` : rien d'affiché sans case découverte, position du
joueur affichée, bord à ~300 m et zone circulaire pour 3 positions du joueur
dans sa case.
