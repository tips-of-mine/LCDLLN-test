# FIX.11 — Carte du monde : zoom en mètres visibles

Retour joueur du 30/10/2026 : à l'ouverture de la carte (touche M), le monde entier
s'affichait ; en se déplaçant autour du hameau, le marqueur du personnage semblait
immobile (quelques centaines de mètres sur une carte de plusieurs centaines de km). Le
zoom molette plafonnait à ×8 (≈ 30 km visibles au plus près).

## Règles

| Réglage (`config.json`, section `client.world_map`) | Défaut | Effet |
|---|---|---|
| `default_view_m` | 2000 | côté visible à **chaque** ouverture, recentrée sur le personnage |
| `min_view_m` | 250 | côté visible au zoom maximal (molette) |

- Chaque ouverture (M) réinitialise la vue : recentrage sur le personnage, ≈ 2 km.
- Le pied de la carte affiche l'échelle courante (« ~2.0 km visibles »).
- Les marqueurs de villes et de lieux gardent la taille du zoom ×8 au-delà (sinon ils
  grossissaient jusqu'à couvrir la carte).
- « Vue d'ensemble » montre toujours le monde entier ; « Centrer » recentre sans changer
  le zoom.

À fort zoom, l'image de fond (`world_map.png`) est agrandie : elle devient floue, mais le
déplacement du personnage, les villes et les lieux restent nets (dessinés par-dessus).

## Code

- `src/client/world/places/WorldMapZoom.h` : conversions zoom ↔ mètres visibles,
  testées par `world_map_zoom_tests`.
- `WorldMapImGuiRenderer` : zoom initial, zoom max, taille des marqueurs, échelle.
- `Engine.cpp` : réinitialisation de la vue à l'ouverture ; `EngineWorld.cpp` : lecture
  de la configuration.
