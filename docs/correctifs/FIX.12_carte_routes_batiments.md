# FIX.12 — Carte du monde : routes, rues, bâtiments et inexploré en noir

Retour joueur du 30/10/2026 (après FIX.11) : sur la carte zoomée (2 km visibles), on ne
voit ni les routes ni les bâtiments ; les zones non découvertes apparaissent grises au
lieu de noires.

## Causes

- La carte est une seule image du continent (`world_map.png`, 2048², ≈ 215 m par pixel).
  Les routes y sont tracées sur 1 pixel et disparaissent une fois agrandies ; les
  bâtiments n'y figurent pas.
- Le voile de l'inexploré était un brun très sombre (26, 22, 18) : après la conversion
  sRGB de l'affichage, il ressortait gris-brun.

## Correction

1. **Voile noir pur** (0, 0, 0) — reste noir quel que soit l'espace colorimétrique.
2. **Tracés vectoriels** exportés par `tools/world_gen` dans
   `world/<monde>/map_features.bin` :
   - routes inter-villes (grandes routes / routes), chemins locaux, rues des
     agglomérations (arêtes du graphe de rues) ;
   - emprise rectangulaire orientée de chaque bâtiment (dimensions du modèle ; repli :
     carré inscrit dans le rayon d'encombrement pour les bâtiments historiques).
   Les polylignes sont allégées (un point tous les ≥ 10 m).
3. Le client les dessine **sous le voile** : ce qui n'est pas découvert reste caché
   (même règle que pour les villes). Épaisseur = largeur réelle à l'échelle, avec un
   minimum lisible ; rues masquées au-delà de 6 km visibles, bâtiments au-delà de 8 km.

Sans `map_features.bin` (monde généré avant FIX.12), la carte reste utilisable et un
avertissement est journalisé (`[WorldPlaces] … absent`).

## Format `map_features.bin` (v1, petit-boutiste)

| Champ | Type |
|---|---|
| magic `LMFT`, version 1 | u32, u32 |
| nombre de tracés, puis par tracé : type (0 grande route, 1 route, 2 chemin, 3 rue), largeur (m), nombre de points, points (x, z) | u32 ; u8, f32, u32, f32×2n |
| nombre de bâtiments, puis par bâtiment : x, z, cap (rad), demi-largeur, demi-profondeur | u32 ; f32×5 |

Coordonnées globales (m), X = est, Z = sud.

## Code

- `src/shared/world/MapFeatures.h` : format, allègement, coins des bâtiments
  (testé par `map_features_tests`).
- `tools/world_gen/lib/Output.cpp` : `BuildMapFeatures` + écriture (testé dans
  `world_gen_tests`) ; `Settlements.cpp` : emprise des bâtiments conservée.
- `src/client/render/WorldMapImGuiRenderer.cpp` : `DrawMapFeatures`, voile noir.
- `src/client/app/EngineWorld.cpp` : chargement avec `places.json`.

## Déploiement

Client uniquement. La CI régénère le monde avant l'empaquetage : le nouveau paquet
client contient `map_features.bin`.
