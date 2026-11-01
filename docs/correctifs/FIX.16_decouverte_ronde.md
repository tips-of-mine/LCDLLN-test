# FIX.16 — Zone découverte ronde sur la carte du monde

## Symptôme

Sur la carte (M), la zone découverte autour du joueur est un CARRÉ alors que
le champ de vision est circulaire.

## Cause

La carte à découvrir découpait chaque cellule de 10 km en cases de 125 m.
Le rayon de révélation (300 m) ne couvre alors que 4 × 4 cases : la zone est
un carré de 500 m. Le voile était en plus dessiné case par case (carrés).

## Correction

1. **Grille fine** (`src/shared/world/MapDiscovery.h`) : cases de **62,5 m**
   (160 × 160 par cellule, masque de 3 200 octets au lieu de 800). Un passage
   révèle ~72 cases disposées en disque.
2. **Affichage arrondi** (`WorldMapImGuiRenderer.cpp`, `IsPointRevealedSmooth`) :
   chaque case découverte dégage un disque de 0,75 case autour de son centre ;
   les cases cachées au bord d'une zone découverte sont subdivisées et voilées
   sous-case par sous-case. Résultat : bord rond (écart < 20 % mesuré par
   test, quasi parfait à l'œil). En vue très large (case < 6 px), voile par
   cases (contour invisible à cette échelle).
3. **Compatibilité** : les anciens masques (800 octets en base, ou envoyés
   par un ancien client, brut ou RLE de 6 400 cases) sont acceptés et agrandis
   (chaque ancienne case → 2 × 2 nouvelles). Les zones déjà découvertes sont
   conservées ; elles gardent leur forme carrée d'origine, les nouvelles
   découvertes sont rondes.
4. **Réseau** : 4 cellules par paquet au lieu de 16 (masques bruts plus gros,
   paquet ≤ 16 Kio, vérifié par `static_assert`).
5. **Base** : migration `0084_map_discovery_fine_grid.sql` — colonne `bits`
   élargie à `VARBINARY(3200)`. Pas de conversion : les lignes de 800 octets
   sont agrandies à la lecture puis réécrites à la sauvegarde suivante.

## Déploiement

⚠️ **Master + shard + client en même temps** : migration DB, et la taille
des masques échangés (opcodes 209/210) change. Un ancien client parlant à un
nouveau master enverrait encore des masques acceptés (compatibilité), mais
ne saurait pas lire les réponses de 3 200 octets.

## Tests

- `map_discovery_tests` : bornes et cases 160 × 160, disque de ~72 cases,
  anciens masques brut/RLE agrandis, page de 4 masques bruts dans un paquet,
  affichage arrondi (centre, coin, rayon, zone quasi circulaire).
- `map_discovery_tracker_tests`, `map_discovery_client_tests` : inchangés, verts.
