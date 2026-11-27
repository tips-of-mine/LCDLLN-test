# ui/map/ — habillage parchemin de la carte du monde (touche M)

| Fichier | Taille | Usage côté client (`WorldMapImGuiRenderer.cpp`) |
|---|---|---|
| `map_roll.png` | 1024 × 160, RGBA | Rouleau de bois tourné, en 3 tranches : pommeaux (11,5 % de la largeur chacun, jamais étirés) et corps étiré (fil du bois parallèle à l'axe). Bords haut et bas de la fenêtre. |
| `map_parchment.png` | 512 × 512, RGB(A) opaque | Parchemin répétable sans couture. Fond de la fenêtre ; teinté (`kFogTint`) pour les zones inexplorées et le hors-carte. |
| `map_edge.png` | 256 × 256, RGBA | Bord usé brun, centre transparent, en 9 tranches (coins = 25 % de la texture). |

## Provenance et licence

Créées le 25/11/2026 par rendu Blender 5.1.2 (Cycles) du script
`tools/asset_pipeline/ui/map_parchment_blender.py` : géométrie et matériaux
entièrement procéduraux, aucune image ni source externe. Licence : celle du
dépôt. Aucun élément issu de la BD (génériques : rouleau, parchemin).

Régénération :

    blender --background --python tools/asset_pipeline/ui/map_parchment_blender.py -- <dossier_sortie>

puis copie des trois PNG ici. Absentes, le client retombe sur l'habillage
vectoriel (aucune erreur).
