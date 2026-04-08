# Pont carte monde (HTML) ↔ moteur (streaming)

## Objectif

Relier **l’éditeur carte** (`Editeur de monde/map-viewer-v23.html`, export type `carte_export.json`) au **découpage runtime** (`WorldModel` : chunk **256 m**, zone **4 km**), sans forcer des approximations cachées.

---

## Ce que fait déjà l’outil HTML

| Élément | Rôle |
|--------|------|
| Grille `grid.rows` × `grid.cols` | Univers découpé en **cellules macro** |
| Identifiant `R###C###` | Repère stable d’une cellule (ex. `R050C010`) |
| `meta.cell_size` | Taille d’une cellule, ex. **10 × 10 km** (configurable dans l’UI) |
| `tiles.cells[id]` | Terrain / biome / notes par cellule macro |
| `nodes` / `edges` | Lieux et liaisons ; `meta.coordinate_system` précise que **0..100** est un placement **normalisé sur la carte 2D**, **pas** des kilomètres |

**Impact :** la **charge / décharge « au fil du jeu »** peut s’indexer sur les **cellules macro** (gros lots de contenu), pendant que le moteur continue à streamer des **chunks 256 m** à l’intérieur.

---

## Ce que fait le moteur C++

| Élément | Taille | Usage |
|--------|--------|--------|
| `Chunk` | **256 m** × 256 m | Unité de streaming (anneaux autour du joueur) |
| `Zone` | **4096 m** × 4096 m | 16 × 16 chunks ; repère « monde » |

**Impact :** une cellule macro de **10 km** recouvre environ **39 × 39 chunks** en théorie (10 000 / 256 ≈ 39,06) — ce n’est **pas** un entier : c’est normal. On ne triche pas avec des faux arrondis ; on utilise des **boîtes englobantes (AABB)** en mètres.

---

## Règle simple d’alignement (recommandée)

1. **Fixer une origine monde** (une fois pour toute), par exemple le coin **nord-ouest** de la cellule `R000C000` exportée.
2. **Taille macro en mètres** : `L = meta.cell_size.value` × facteur d’unité (si `unit` = `km`, multiplier par **1000**).
3. Pour une cellule `(row = r, col = c)` au sens export (**axes** : `x` ouest→est, `y` nord→sud selon `meta`) :
   - en coordonnées **moteur XZ** (mètres, comme `WorldModel`) :
     - **X** = `c * L` (ouest → est)
     - **Z** = `r * L` si l’axe « nord-sud » du JSON correspond à **+Z** monde (à verrouiller une fois avec la convention du jeu)
4. **AABB de la macro-cellule** : `[c·L, (c+1)·L) × [r·L, (r+1)·L)` sur le plan XZ.
5. **Chunks à considérer** pour cette macro-cellule : tous les `GlobalChunkCoord` dont l’AABB chunk **intersecte** l’AABB macro (via `World::ChunkBounds`).

**Impact qualité :** comportement **prévisible** et **indépendant** des bugs d’arrondi « 10 km vs 256 m ».  
**Impact simplicité :** une seule formule + intersection de boîtes ; pas de table de correspondance à maintenir à la main.

---

## Chargement / déchargement « au fil du jeu »

- **Niveau design / données** : décider quels paquets (heightmap, layout, TEXR…) sont attachés à un **`R###C###`** ou à un groupe de cellules.
- **Niveau runtime** : quand le joueur entre dans l’AABB d’une macro-cellule (± marge / **hystérésis** pour éviter le thrash), la couche **streaming** demande les assets des chunks concernés ; à la sortie, **priorité basse** puis éviction selon budget (déjà dans l’esprit M10 / scheduler).

**Impact :** même logique que le streaming chunk, avec un **garde-fou macro** pour l’I/O grossiers (gros fichiers par « région »).

---

## Cohérence avec tes choix précédents

- **`zone.json` / terrain** : reste la vérité **terrain local** (`.r16h`, résolution contrôlée).
- **`layout.json`** : reste la vérité **instances** (`zone_builder`).
- **Carte HTML** : vérité **macro** (biomes, lieux, liens narratifs) ; le lien vers le moteur passe par **l’AABB + ids `R/C`**, pas par duplication des objets dans `zone.json`.

---

## Pistes d’évolution (sans bloquer le MVP)

- Ajouter dans l’export JSON une section optionnelle `meta.engine_bridge` : `origin_xz_m`, `macro_cell_m`, `chunk_m = 256`, `zone_m = 4096` (redondant mais **lisible** pour les outils).
- Outil CLI : valider que chaque `R###C###` référencé par un lieu a bien une entrée dans `tiles.cells`.

---

*Document v1 — aligné sur `carte_export.json` (meta.cell_size, grid, tiles.cells) et `engine/world/WorldModel.h`.*
