# FIX.19 — Carte du monde : habillage parchemin texturé (Blender)

Retour du test visuel du 25/11/2026 sur FIX.18 : l'effet de déroulé est en place
mais le rendu n'est pas beau. Arbitrage du propriétaire : corriger les
**rouleaux et le cadre**, et remplacer le noir des zones inexplorées par du
**parchemin vierge** ; textures produites avec Blender.

## Correction

- **Textures** (`game/data/ui/map/`, provenance dans son README) rendues par
  `tools/asset_pipeline/ui/map_parchment_blender.py` (Blender 5.1.2, Cycles,
  100 % procédural) : rouleau de bois tourné, parchemin répétable sans couture,
  bord usé.
- **Rouleaux** : bois foncé (2e retour visuel), texture en 3 tranches (pommeaux non étirés), hauteur à
  l'échelle de la police (`SkinScale`, base 16 px) au lieu de 18 px fixes,
  dessinés par-dessus le contenu (`DrawParchmentRolls`, avant `End`).
- **Cadre** : fond de fenêtre en parchemin répété, bord usé en 9 tranches,
  double filet d'encre de la carte à l'échelle.
- **Inexploré** (FIX.12) : voile opaque conservé, mais en parchemin vierge
  (texture teintée `kFogTint`, tuiles calées sur la fenêtre) au lieu du noir ;
  le hors-carte prend le même papier.
- **Repli** : texture absente → habillage vectoriel de FIX.18, voile uni
  `kFogFlat`.

Inchangés : noms au survol, déroulé, sons, zoom, glisser, boutons, pied de
fenêtre.

## Déploiement

Client uniquement (rendu, données UI), pas de redéploiement serveur.
