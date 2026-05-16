# Convention de nommage des cellules monde

Document de référence pour le découpage de la carte LCDLLN en cellules de
zone. Cette convention fige l'identifiant de chaque cellule sur disque et
dans le code, indépendamment de l'**implémentation interne** (chunks moteur,
spatial grid serveur, etc., qui ont leurs propres unités).

---

## 1. Grille monde

| Paramètre | Valeur |
|-----------|--------|
| Nombre de lignes (Nord/Sud) | **33** (14 N → 18 S) |
| Nombre de colonnes (Est/Ouest) | **40** (19 W → 20 E) |
| Total de cellules | **1320** (33 × 40) |
| Taille d'une cellule | **10 km × 10 km** (= `engine::world::kZoneSize` en mètres) |
| Carte totale | **330 km × 400 km** (132 000 km²) |

Une cellule = une **zone** au sens moteur (1 `runtime_manifest.json`, 1
heightmap, 20×20 chunks de 500 m).

---

## 2. Origine et orientation

L'**Oracle** est le centre logique de l'empire. Sa cellule est
**`cell_n000_e000`**. Toutes les autres cellules sont nommées par rapport à
elle :

- Axe **Nord/Sud** : `N` croît vers le nord depuis Oracle, `S` croît vers le
  sud.
- Axe **Est/Ouest** : `E` croît vers l'est depuis Oracle, `W` croît vers
  l'ouest.

L'origine `(N000, E000)` est volontairement **décentrée** dans la grille
33×40 (Oracle a 14 lignes au nord et 18 au sud ; 19 colonnes à l'ouest et
20 à l'est) pour refléter la géographie narrative de l'empire.

---

## 3. Format de l'identifiant

```text
cell_<lat><DDD>_<lon><DDD>
```

- Préfixe littéral `cell_`.
- `lat` ∈ {`n`, `s`}, `lon` ∈ {`e`, `w`}.
- `DDD` : trois chiffres décimaux **zéro-paddés** (000 .. 999). 3 chiffres
  laissent de la marge si la carte s'agrandit (1 000 cellules par côté).
- Tout en **minuscules** : impératif imposé par
  `SanitizeZoneId` (`src/world_editor/ui/WorldMapIo.cpp:1235`), qui force
  alphanum + `_` + lowercase sur tout `zone_id` côté éditeur. Toute saisie
  utilisateur `cell_N000_E000` sera normalisée en `cell_n000_e000` au Save.

### Exemples

| Cellule | Sens |
|---------|------|
| `cell_n000_e000` | Oracle (centre) |
| `cell_n014_w019` | Coin nord-ouest |
| `cell_n014_e020` | Coin nord-est |
| `cell_s018_w019` | Coin sud-ouest |
| `cell_s018_e020` | Coin sud-est |
| `cell_n011_e002` | Exemple d'île à 2 cellules au NE d'Oracle |

### Conversion ↔ ancienne convention `R###C###`

Sous l'ancienne grille `R050C010` → `R370C400` (32 lignes × 391 colonnes,
origine NO), la cellule située à `(row=R, col=C)` correspondait à un point
de la carte. La nouvelle convention utilise Oracle comme origine ; les
valeurs `R`/`C` qui plaçaient Oracle dans l'ancienne grille sont :

```
oracle_row = 50 + 14 = 64    (offset N: 14 lignes au nord)
oracle_col = 10 + 19 = 29    (offset W: 19 colonnes à l'ouest)
```

Donc :

```
N = oracle_row - R          (négatif → S = R - oracle_row)
E = C - oracle_col          (négatif → W = oracle_col - C)
```

Validation sur les coins (issus de la discussion produit) :

| Ancienne | Nouvelle |
|----------|----------|
| `R050C010` | `cell_n014_w019` |
| `R050C400` | `cell_n014_e020` |
| `R370C010` | `cell_s018_w019` |
| `R370C400` | `cell_s018_e020` |
| `R080C220` | `cell_n011_e002` |

---

## 4. Structure d'un dossier de cellule

Chaque cellule habitée a son dossier sous `game/data/zones/<cell_id>/` avec,
**au minimum** :

```
cell_n000_e000/
├── runtime_manifest.json   # version 3, zone_id = "cell_n000_e000"
└── .gitkeep                # pour garder le dossier en git tant que vide
```

Quand l'éditeur monde sauvegarde la zone, il ajoute selon le contenu :

```
├── terrain_height.r16h     # heightmap (généré par éditeur)
├── terrain_splat.slap      # splat layers (généré par éditeur)
├── terrain_grass.grms      # masque herbe (généré par éditeur)
├── atmosphere.json         # ambiance / éclairage (généré par zone_builder)
├── probes.bin              # sondes globales (généré par zone_builder)
├── zone.meta               # header binaire versionné
├── chunks/                 # 20×20 chunks (généré par zone_builder)
├── layout_from_editor.json # instances depuis l'éditeur
├── spawners.json           # peuplement runtime (édité à la main / outil)
├── events.json             # events runtime (édité à la main / outil)
└── gathering_nodes.json    # nodes de collecte (édité à la main / outil)
```

Voir `docs/world_editor_zone_pipeline.md` pour le pipeline complet
édition → export runtime → `zone_builder`.

---

## 5. Politique de création des cellules

**Pas de pré-création massive** des 1320 cellules. Chaque cellule est créée
**à la main au fur et à mesure** que le mapping avance. Raisons :

1. Une cellule pleinement éditée pèse plusieurs centaines de KB de binaires
   (heightmap + splat + grass + chunks) → 1320 × ~300 KB ≈ 400 MB de
   binaires dans git, inutiles tant que la cellule n'est pas mappée.
2. Le système Zone Presets (M100.46) est l'outil prévu pour bootstrapper
   rapidement une cellule depuis un template (`temperate_forest`,
   `rocky_coast`, …). Pré-créer des stubs vides court-circuite ce flux.
3. La grille évoluera : un découpage en îles ou en sous-régions narratives
   peut rendre certaines cellules absentes/inutiles.

**Workflow attendu** pour une nouvelle cellule :

1. Créer le dossier `game/data/zones/cell_<lat><DDD>_<lon><DDD>/`.
2. Y placer un `runtime_manifest.json` stub minimal (heightmap/splat/grass
   = `null`, `terrain_world_size_m` = 10000, `zone_id` = nom du dossier) +
   un `.gitkeep`.
3. Ouvrir la cellule dans `lcdlln_world_editor`.
4. Appliquer un Zone Preset adapté (Fichier > Appliquer un preset, M100.46
   incrément 3) **OU** sculpter à la main.
5. Sauvegarder → l'éditeur écrit les binaires (terrain_height,
   terrain_splat, terrain_grass) + met à jour le manifest.
6. Optionnel : `zone_builder --layout zones/<cell>/layout_from_editor.json
   --output zones/<cell> --zone-id <cell>` pour packager les chunks.

---

## 6. État actuel

| Cellule | Statut | Notes |
|---------|--------|-------|
| `cell_n000_e000` | Stub (manifest vide) | Oracle, créée pour valider la convention. |
| `demo_plains` | Zone démo legacy | Conservée pour les tests et la checklist ; pas renommée. |
| `zone_0`, `zone_1`, `zone_2` | Zones de test legacy | Conservées pour les exemples de layout. |

Les zones legacy (`demo_plains`, `zone_*`) ne sont **pas** renommées : elles
servent de référence pour les tests et la documentation existante. La
nouvelle convention s'applique aux cellules **du monde de jeu** uniquement.

---

## 7. Évolutions

Si la grille change (taille de cellule, dimensions, origine), mettre à jour
ce document **et** :

- `engine::world::kZoneSize` dans `src/client/world/WorldModel.h` si la
  taille de cellule bouge (impact streaming et chunk size).
- Le tableau de mapping `R###C###` ↔ `cell_*` ci-dessus.
- Toute documentation aval (`world_editor_zone_pipeline.md`,
  `world_zone_demo_checklist.md`) qui pourrait référencer un id de zone
  ancien.
