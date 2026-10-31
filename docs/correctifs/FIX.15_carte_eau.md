# FIX.15 — Lacs et rivières sur la carte du monde

## Symptôme

Sur la carte (touche M), le lac visible en jeu près du hameau de Marn
n'apparaît pas : seules la couleur du sol, les routes et les bâtiments sont
dessinés.

## Cause

Le fond de carte (`world_map.png`) fait ≈ 215 m par pixel : un étang de
quelques dizaines de mètres ou une rivière de 6 à 12 m de large n'y occupe
même pas un pixel. FIX.12 avait ajouté le tracé vectoriel des routes et des
bâtiments (`map_features.bin`), mais pas celui de l'eau.

## Correction

- `src/shared/world/MapFeatures.h` : format **v2** — bloc « plans d'eau »
  après les bâtiments : rivières (polyligne + largeur) et lacs (contour
  fermé). Les fichiers v1 restent lisibles (aucun plan d'eau).
- `tools/world_gen/lib/Output.cpp` (`BuildMapFeatures`) : exporte les
  rivières globales (largeur médiane, points allégés à 10 m), les lacs du
  lore et les étangs procéduraux des cellules (chaque lac une seule fois ;
  lac sans contour → cercle de 24 côtés).
- `src/client/render/WorldMapImGuiRenderer.cpp` : l'eau est dessinée en bleu
  AVANT les routes (ponts par-dessus) : lacs remplis en éventail + contour,
  rivières en polylignes d'au moins 1,5 px. Toujours sous le voile des zones
  non découvertes.
- Journal : `[WorldPlaces] carte : … traces de routes/rues, … batiments, … plans d'eau`.

## Déploiement

Le fichier vient du générateur de monde : la CI régénère le monde avant
d'empaqueter, le nouveau paquet client contient donc la carte v2. Un client
avec un ancien `map_features.bin` (v1) fonctionne, sans eau.

## Tests

- `map_features_tests` : aller-retour v2, boîte d'une rivière élargie de sa
  demi-largeur, centre d'un lac, lecture d'un fichier v1.
- `world_gen_tests` : rivière (largeur médiane), lac du lore exporté une seule
  fois, étang sans contour converti en cercle.
