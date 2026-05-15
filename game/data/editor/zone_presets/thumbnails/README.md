# Thumbnails des zone presets — `game/data/editor/zone_presets/thumbnails/`

> M100.46 — Zone Presets Library (Phase 12 « Accessibilité éditeur »).

## Statut MVP

Le dialog « Nouvelle zone depuis preset » (incrément UI à venir) affiche
une grille 4×2 de vignettes 256×144 px, une par preset. Les **8 fichiers
PNG ne sont pas livrés** dans cet incrément — ils relèvent d'un ticket
d'art dédié (rendus de zones-test ou illustrations).

Fichiers attendus (le champ `thumbnail` de chaque `zone_presets/<id>.json`
y pointe) :

- `temperate_forest.png`
- `rocky_coast.png`
- `desert.png`
- `snowy_plateau.png`
- `marshland.png`
- `elven_valley.png`
- `dead_lands.png`
- `volcanic_island.png`

Format : PNG 256×144, RGBA. Tant qu'un PNG est absent, le dialog
affichera un placeholder procédural (même pattern que la toolbar
M100.35 et les catalogues Phase 11).
