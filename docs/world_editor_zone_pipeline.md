# Pipeline World Editor, export runtime et zone_builder

Document de référence (**ticket 001** — `tickets/world/001_spec_pipeline_et_contraintes_monde.md`). Tout chemin fichier est **relatif à `paths.content`** (souvent `game/data` via `config.json`), sauf mention contraire.

---

## 1. Constantes monde (moteur)

Définies dans `engine/world/WorldModel.h` (source de vérité pour le streaming et le découpage chunk côté outils qui incluent ce header) :

| Symbole | Valeur | Rôle |
|---------|--------|------|
| `kZoneSize` | **10 000** | Côté d’une zone en mètres (XZ), espace zone-local / monde logique. |
| `kChunkSize` | **500** | Côté d’un chunk en mètres → **20 × 20** chunks par zone. |
| `kChunksPerZoneAxis` | **20** | `kZoneSize / kChunkSize`. |
| `kSpatialCellSizeMeters` | **100** | Maille intérêt / réplication serveur (`SpatialPartition`) : **100 × 100** cellules par zone. |

**Remarques design :** 512 m ne divise pas 10 000 ; 500 m est le pas chunk retenu. 64 m ne divise pas 10 000 ; 100 m est la maille cellulaire retenue. Toute évolution doit préserver les `static_assert` du header.

---

## 2. World Editor (`lcdlln_world_editor`)

### 2.1 Rôle

Édition d’une carte : heightmap, listes de textures / ids d’objets (MVP), sculpt CPU, rechargement GPU du terrain. Sortie **édition** sous `world_editor/maps/<zone_id>/` et sortie **runtime** sous `zones/<zone_id>/`.

### 2.2 Fichiers d’édition (relatifs au content)

| Chemin | Description |
|--------|-------------|
| `world_editor/maps/<zone_id>/height.r16h` | Heightmap binaire (magic HAMP), créée par « Nouvelle carte ». |
| `world_editor/maps/<zone_id>/map.lcdlln_edit.json` | Document d’édition JSON (voir §4). |

`<zone_id>` est normalisé par `SanitizeZoneId` (alphanum + `_`, minuscules).

### 2.3 Export runtime (`ActionExportRuntime` → `ExportRuntimeBundle`)

Racine cible : **`zones/<zone_id>/`** (avec le même `zone_id` sanitizé).

| Fichier / dossier | Contenu |
|-------------------|---------|
| `terrain_height.r16h` | Copie du heightmap référencé par le document (`heightmapContentRelativePath`). |
| `terrain_splat.slap` | Copie du splatmap SLAP référencé par `splatmapContentRelativePath` si le fichier source existe ; sinon avertissement log et pas de copie (`terrain_splatmap` = `null` dans le manifeste). |
| `terrain_grass.grms` | Copie du masque herbe GRMS (`grassMaskContentRelativePath`) si le fichier source existe ; sinon `terrain_grass_mask` = `null` (ticket **010**). |
| `zone.meta` | En-tête versionné binaire seul (`OutputVersionHeader` : magic zone meta, versions builder/engine) — pas d’extension binaire sans bump `kZoneMetaVersion`. |
| `exported_textures/` | Pour chaque entrée de `textures` du JSON d’édition (chemins relatifs au content, sans `..`) : copie miroir sous `zones/<id>/exported_textures/<chemin>`. Fichier source absent → entrée listée dans le manifeste, export continue. |
| `runtime_manifest.json` | `lcdlln_runtime_manifest_version` **3** : idem v2 + `terrain_grass_mask` (`zones/<id>/terrain_grass.grms` ou `null`). Champs v2 inchangés : `zone_id`, `terrain_heightmap`, `terrain_splatmap`, `source_edit_format_version`, `terrain_world_size_m`, listes textures, `object_prefab_ids`, `note`. |
| `layout_from_editor.json` | `version` **1** + `instances` (schéma `zone_builder` : `guid`, `gltf`, `position`) — rempli depuis l’éditeur (ticket **009**) ou stub vide (**006**). Les champs **013** (`species_id`, `shape_variant`, etc.) sont dans le **JSON d’édition** carte (`map.lcdlln_edit.json`) uniquement ; l’export runtime garde le schéma minimal pour `zone_builder`. |

**Inventaire présent vs futur (contrat streaming)**

| Artefact | Export WE aujourd’hui | Après zone_builder / streaming |
|----------|----------------------|--------------------------------|
| `terrain_height.r16h` | Oui | Réutilisé ou fusionné selon pipeline |
| `terrain_splat.slap` | Oui (si `splatmap` défini et fichier présent) | Réutilisé côté client jeu / builder selon pipeline |
| `zone.meta` (WE) | Header seul | Remplacé ou enrichi côté builder (`ChunkPackageWriter`) avec bump de version si binaire étendu |
| `runtime_manifest.json` | Manifeste trace + textures exportées | Consommation optionnelle outillage |
| `exported_textures/` | Oui (copie depuis content) | Packaging `tex.pak` / autre selon jeu |
| `textures/*`, `audio/*` hors zone | Restent au content ; seules les refs `textureAssets` sont dupliquées ici | Idem + assets builder |
| Splat SLAP (`splatmap` dans le JSON) | Oui (`terrain_splat.slap` + clé `terrain_splatmap` du manifeste) | Client jeu / builder |
| Masque herbe GRMS (`grass_mask` dans le JSON) | Oui si fichier présent (`terrain_grass.grms` + `terrain_grass_mask`) | Client jeu / builder (ticket **010**) |
| `layout_from_editor.json` | Oui (instances WE ou stub) | Consommé par `zone_builder` |
| `chunks/chunk_i_j/*` (générés par `zone_builder`) | Non à l’export WE | **005** / **006** : `layout_from_editor.json` + script ou commande §5 |

**Non produit par l’export WE seul :** sous-dossiers `chunks/chunk_i_j/*`, glTF dédiés layout, paquets `geo.pak` / `tex.pak`, etc. → **`zone_builder`** après export (§3, §5).

### 2.4 Imports depuis l’éditeur

- **PNG → `.texr`** : sous `textures/<nom>` (magic TEXR), entrée ajoutée à `textureAssets` du document.
- **Audio** : copie sous `audio/<chemin relatif>` (pas de décodage).

---

## 3. Outil `zone_builder`

### 3.1 Modes CLI

- **`zone_builder --help`** (ou `-h`) : modes layout / legacy, exemples de chemins, rappel `config.json` et `paths.content`.

- **Layout (recommandé)** :  
  `zone_builder --layout <layout.json relatif au content> [--output <dir>] [--config config.json] [--zone-id <id>]`  

  - **`--layout`** : chemin **relatif à `paths.content`** (ex. `zones/_templates/layout_minimal.json`). Fichier absent ou illisible → **code de sortie 1**.  
  - **`--output`** : si omis, défaut **`<répertoire courant>/build/zone_0`** (hors content, pratique build locale). Si la valeur commence par **`zones/`**, elle est résolue avec **`engine::platform::FileSystem::ResolveContentPath`** (même base que le jeu, ex. `game/data/zones/ma_zone`). Tout autre chemin relatif est interprété **depuis le répertoire courant** au lancement de l’outil.  
  - **`--zone-id`** (optionnel) : après résolution de `--output`, doit correspondre exactement au dossier content `zones/<id_sanitizé>/` (cohérence avec l’export WE ticket **004**).  
  - **glTF** référencé par le layout : résolu sous content ; fichier absent → **code de sortie 1**.

- **Legacy** :  
  `zone_builder --output <dir> --chunk <x> <z>`  
  Écrit un seul paquet de chunk sous `<dir>/chunk_x_z/`. Coordonnées **`x` et `z` ≥ 0** ; sinon **code de sortie 1** (avant écriture).

**Exemple de layout minimal** (versionné, une instance) : **`game/data/zones/_templates/layout_minimal.json`**.

### 3.2 Entrée layout

`LoadLayoutDocument` lit un JSON versionné minimal (`LayoutDocument` : `version`, instances avec `guid`, `gltfPath`, positions). Les assets glTF sont résolus via la config (content).

**Contraintes positions** : chaque instance a une position **`position` = [x, y, z]** ; **x** et **z** doivent être dans **\[0, `kZoneSize`)\`** mètres (plan horizontal de la zone). Hors plage → erreur de chargement (code 1). Le découpage chunk refuse aussi des indices hors **\[0, `kChunksPerZoneAxis` − 1\]**. **Y** n’est pas borné par la zone.

### 3.3 Sortie mode layout (`WriteChunkedZoneOutputs`)

Sous la racine `--output` (ex. `build/zone_0/`) :

| Élément | Description |
|---------|-------------|
| `zone.meta` | Méta-zone (nombre de chunks, hash contenu — logique `ChunkPackageWriter`). |
| `probes.bin` | Sonde(s) globale(s) MVP. |
| `atmosphere.json` | Ambiance minimale. |
| `chunks/chunk_<x>_<z>/chunk.meta` | Méta chunk ; bornes XZ en **mètres** dérivées de `kChunkSize`. |
| `chunks/chunk_<x>_<z>/instances.bin` | Instances regroupées par chunk (`floor(worldX / kChunkSize)`). |

Segments prévus par le runtime (`ChunkPackageLayout`) : `geo.pak`, `tex.pak`, `instances.bin`, `navmesh.bin`, `probes.bin` — le flux layout actuel remplit surtout **instances** (+ méta / probes zone).

### 3.4 Cohérence avec le jeu

Les constantes **`kZoneSize`** et **`kChunkSize`** sont partagées : le découpage chunk outil = découpage chunk moteur. Les positions layout sont en **mètres** dans l’espace de la zone (0 … `kZoneSize` typiquement) ; coordonnées chunk négatives rejetées par l’implémentation actuelle des bornes `chunk.meta`.

---

## 4. Format JSON d’édition (`map.lcdlln_edit.json`)

Champs écrits par `SaveEditDocumentJson` (voir `WorldMapEditDocument`) :

- `zone_id` (string)
- `version` (int, format document, ex. 1)
- `size` (uint, résolution N×N du `.r16h`)
- `seed` (nombre ou `null`)
- `heightmap` (string, chemin relatif content)
- `textures` (tableau de **chaînes uniquement** ; clé absente ou `null` → liste vide)
- `objects` (idem ; ids prefab MVP)

**Chargement (`LoadEditDocumentJson`) :** `textures` et `objects` sont relus à l’identique du fichier sauvé (round-trip). Types non string dans un tableau → erreur. Au plus **4096** entrées par tableau. Si `version` > `WorldMapEditDocument::kFormatVersion` → erreur explicite.

### 4.1 Échelle terrain (World Editor) — `terrain_world_size_m`

- Clé **`terrain_world_size_m`** : nombre (mètres) ou **`null`**. Absence de clé = pas d’override (le terrain WE utilise `terrain.world_size` depuis `config.json`).
- **Nouvelle carte** (`ActionNewMap`) : le document est initialisé avec `hasTerrainWorldSizeM = true` et **`terrain_world_size_m = kZoneSize`** (10 000 m aujourd’hui), aligné sur la zone logique moteur.
- **`TerrainRenderer::Init`** accepte un override optionnel : en World Editor, si le document porte une valeur > 0, elle remplace la lecture de `terrain.world_size` pour `m_terrainWorldSize` (pas le client jeu en l’absence d’override).
- **Pas de `terrain.world_size` dans le JSON** : le jeu continue de lire la config ; seul l’éditeur applique l’override au rebuild GPU.
- **Pas rétro‑ajusté automatiquement** : le moteur utilise encore `m_vertStepWorld = m_terrainWorldSize / 1024.f` (pas le `(width-1)` du heightmap) — documenter pour éviter les surprises si résolution heightmap ≠ 1025 ; voir `TerrainRenderer.cpp`.

L’**export runtime** (`runtime_manifest.json`, version **2**) reprend `terrain_world_size_m` si défini (sinon `null`), les listes du document d’édition et les chemins des textures effectivement copiées sous `exported_textures/`, plus les sources manquantes (`texture_assets_source_missing`).

---

## 5. Chaîne recommandée (vue d’ensemble)

```text
[World Editor]  world_editor/maps/<id>/  (edit JSON + .r16h)
       │  Export runtime
       ▼
       zones/<id>/terrain_height.r16h + zone.meta + runtime_manifest.json
             + layout_from_editor.json + exported_textures/…
       │
       │  zone_builder --layout zones/<id>/layout_from_editor.json --output zones/<id> …
       ▼
       zones/<id>/chunks/… (+ zone.meta / probes / atmosphere enrichis côté builder, voir §3.3)
       │
       ▼
[Client / streaming]  charge segments selon ChunkPackageLayout + scheduler
```

### 5.1 Pont World Editor → `zone_builder` (ticket **006**)

Objectif : sous `paths.content`, le dossier **`zones/<zone_id>/`** contient le bundle exporté WE **puis** les sorties chunk du builder, **sans chemins absolus codés en dur** dans le dépôt.

**Étapes manuelles (répétables)**

1. Lancer **`lcdlln_world_editor`** avec un `config.json` dont **`paths.content`** pointe vers le jeu (souvent `game/data`).
2. Charger ou créer une carte ; **Exporter runtime** → crée notamment `zones/<id>/layout_from_editor.json` (instances vides), heightmap, `zone.meta` (header WE), manifeste.
3. Exécuter **`zone_builder`** depuis la **racine du dépôt** (là où se trouve `config.json`), en passant le layout **relatif au content** et la sortie **`zones/<id>`** (résolution content, voir §3.1) :

**Windows (PowerShell)** — adapter `LCDLLN_BUILD_DIR` si votre build CMake n’est pas sous `build/vs2022-x64` :

```powershell
$env:ZONE_ID = "ma_zone"
$env:LCDLLN_BUILD_DIR = "build/vs2022-x64"
.\tools\world\export_zone_with_chunks.ps1
```

**Linux / macOS (bash)** — idem pour `LCDLLN_BUILD_DIR` (ex. `build/linux-x64`) :

```bash
export ZONE_ID=ma_zone
export LCDLLN_BUILD_DIR=build/linux-x64
chmod +x tools/world/export_zone_with_chunks.sh
./tools/world/export_zone_with_chunks.sh
```

**Commande équivalente** (sans script), toujours depuis la racine du dépôt, avec binaire produit par CMake sous `${LCDLLN_BUILD_DIR}/pkg/zone_builder/` :

```text
zone_builder --layout zones/<zone_id>/layout_from_editor.json --output zones/<zone_id> --zone-id <zone_id> --config config.json
```

Les scripts fixent **`ZONE_BUILDER`** si besoin : exécutable attendu à  
`${REPO_ROOT}/${LCDLLN_BUILD_DIR}/pkg/zone_builder/zone_builder` (`.exe` sous Windows). En cas d’échec, définir explicitement **`ZONE_BUILDER`** (chemin vers l’exécutable).

**Résultat attendu** : `zone_builder` se termine avec **code 0** ; sous `zones/<id>/` apparaissent au minimum **`chunks/`** (éventuellement vide d’instances si le layout est vide), plus les fichiers décrits en **§3.3** (`zone.meta` étendu, `probes.bin`, `atmosphere.json`, etc. — le builder **remplace** le `zone.meta` header-seul du WE par la version packagée liste de chunks).

---

## 6. Documentation terrain rendu (complément)

Rendu terrain décalé, frame graph, reload GPU : **`docs/terrain_et_world_editor.md`**.

---

## 7. Évolution

Modifier ce fichier lorsque le format `zone.meta`, le manifest runtime, ou le contrat `zone_builder` ↔ jeu change. Mettre à jour les `static_assert` et ce document ensemble.

## 8. Backlog d’implémentation (tickets)

La suite chronologique **002 → 007** (spécifications de travail, DoD, chaînage) vit sous **`tickets/world/`** — voir **`tickets/world/README.md`**.

**Zone démo & onboarding** : checklist pas à pas **[`docs/world_zone_demo_checklist.md`](world_zone_demo_checklist.md)** (`zone_id` **`demo_plains`**), incluant le **§5 (ticket 012)** — validation client terrain (SLAP/GRMS) + rappels streaming / `zone_builder` ; liens build dans le **README** racine.
