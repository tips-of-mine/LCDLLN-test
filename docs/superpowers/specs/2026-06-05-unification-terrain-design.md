# Spec — Unification du terrain (chantier C)

**Date** : 2026-06-05
**Statut** : conception validée, en attente de relecture utilisateur avant plan d'implémentation
**Origine** : audit du moteur de rendu (`docs/audit/2026-06-05-moteur-rendu-client.md`), symptôme rapporté en jeu (triangles scintillants au sol quand le personnage se déplace).
**Portée** : client uniquement. Aucun changement protocole / handler / DB → **pas de redéploiement serveur**.

---

## 1. Problème

Deux systèmes terrain tournent **simultanément** dans le client de jeu :

- `terrain/TerrainRenderer` — heightmap legacy (`.r16h`), dessiné inconditionnellement (`Engine.cpp:5074`).
- `terrain_chunk/TerrainChunkRenderer` — chunks data-driven (`terrain.bin`), dessiné par-dessus (`Engine.cpp:5285-5305`), **sans éteindre le legacy**.

Deux conséquences, confirmées par investigation :

### 1.1 Z-fighting (symptôme visible)
Les deux terrains écrivent dans le même depth buffer (`m_fgDepthId`), sur des surfaces quasi-coplanaires mais à des hauteurs calculées différemment (heightmap `× height_scale` vs `terrain.bin` brut). Le départage de profondeur bascule selon la position caméra → des plaques triangulaires apparaissent/disparaissent à chaque déplacement. **C'est le bug rapporté par l'utilisateur.**

États depth : legacy `LESS` (`TerrainRenderer.cpp:551-553`), chunk `LESS_OR_EQUAL` (`TerrainChunkPipeline.cpp:294-296`), aucun depthBias.

### 1.2 Collision ≠ rendu (défaut de fond)
100 % du gameplay (collision, spawn, snap-au-sol, avatar éditeur) lit la **heightmap legacy** via `TerrainCollider` → `TerrainRenderer::SampleHeightAtWorldXZ` (`TerrainCollider.cpp:29-32`). Le système chunks **n'a aucun consommateur de hauteur**. En zone chunkée, le joueur marche donc sur la heightmap pendant que ses yeux voient les chunks (cause profonde du fix partiel PR #824).

### 1.3 Cause racine annexe — taille de chunk incohérente (bug)
Une seule constante `kChunkSize = 500 m` (`WorldModel.h:15`) sert deux grilles distinctes :
- **Grille instances / streaming / zone** : 500 m, choisie car elle divise `kZoneSize = 10000` (512 ne le fait pas). **Légitime.**
- **Grille du mesh terrain** : devrait être 256 m (= `(kTerrainResolution-1) × kTerrainCellSizeMeters` = 256×1, `TerrainChunk.h:15-19`). Mais `TerrainChunkRenderer.cpp:1129-1130` réutilise par erreur la constante 500 pour placer le mesh terrain.

Résultat : chunks voisins placés à 0 m et 500 m, meshes ne couvrant que 256 m → **trou de 244 m**. L'éditeur, lui, place déjà les chunks à 256 m (`Engine.cpp:12169-12170`) → divergence silencieuse éditeur↔jeu. Confirmé par l'historique git (256→500 introduit pour le streaming ; mesh 256 arrivé après avec un commentaire d'alignement erroné) et par le fait que le binning d'instances (zone_builder, partition serveur) est le seul usage légitime de 500.

---

## 2. Décisions validées

- **Destination** : chunk-canonique (les chunks deviennent la source de vérité unique du terrain — collision ET rendu), alignée sur ce que l'éditeur fait déjà (`TerrainDocument.h:74-79` : « les chunks sont la source de vérité, le r16h n'est qu'un cache GPU »).
- **Trajectoire** : **phasée**, jeu jouable à chaque étape.
- **Taille de chunk** : ne pas « choisir 256 ou 500 » — **séparer les deux constantes** et router la grille terrain sur 256, en laissant le binning d'instances sur 500.

---

## 3. Conception — 4 phases

### Phase 0 — Correctif minimal (rendu exclusif)
**But** : faire disparaître le z-fighting immédiatement, sans rien unifier.

- Exposer le nombre de chunks réellement dessinés depuis `TerrainChunkRenderer` (compteur des `RecordChunkDraw`, `TerrainChunkRenderer.cpp:1131`) via un `GetLastDrawnChunkCount()`.
- À `Engine.cpp:5070`, éteindre le legacy quand des chunks sont dessinés :
  ```cpp
  const bool chunksCoverScene =
      m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid()
      && m_terrainChunkRenderer->GetLastDrawnChunkCount() > 0;
  const bool terrainBeforeGeometry = m_terrain.IsValid()
      && m_pipeline->GetGeometryPass().HasLoadPass()
      && !chunksCoverScene;
  ```

**Comportement** : carte 100 % chunks → legacy éteint (z-fight + overdraw supprimés) ; carte 100 % heightmap → legacy conservé (fallback intact) ; carte mixte → seul cas ambigu, déjà cassé aujourd'hui de toute façon.

**Vérifier** : que mettre `terrainBeforeGeometry=false` n'empêche pas l'ouverture du render-pass LOAD (RecordIndirect/Record gère l'ouverture quand le flag est faux ; les chunks rouvrent le LOAD pass via `RecordTerrainChunkBatch`).

**Risque** : faible. Livrable indépendant.

### Phase 1 — Réconcilier la taille de chunk
**But** : aligner la grille terrain du jeu sur celle de l'éditeur (256 m), supprimer le trou de 244 m.

- Introduire une constante terrain dédiée : `kTerrainChunkSizeMeters = (kTerrainResolution - 1) * kTerrainCellSizeMeters` (= 256).
- Router sur cette constante terrain :
  - placement du mesh : `TerrainChunkRenderer.cpp:1129-1130` ;
  - conversion monde→coord terrain et bounds : `WorldModel.cpp:17-18, 33-36` ;
  - centre de chunk pour la priorité de streaming : `StreamingScheduler.cpp:31-32`.
- Renommer `kChunkSize` (500) en une constante explicitement « instances/zone » (ex. `kInstanceChunkSizeMeters`) ; conserver son usage dans zone_builder / partition serveur / binning d'instances.
- Ajuster le `static_assert(kZoneSize % kChunkSize == 0)` : la grille terrain (256) n'a pas à diviser `kZoneSize` ; seul le binning d'instances (500) garde cette contrainte.
- Référence d'implémentation : `Engine.cpp:12169-12170` (éditeur, déjà correct).

**Résultat** : jeu et éditeur affichent le terrain au même endroit, sans trou. Pré-requis d'une requête monde→chunk fiable (Phase 2).

**Risque** : moyen — touche au streaming/culling. Tester qu'aucun chunk ne manque/double au franchissement de frontière.

### Phase 2 — `IHeightField` + collision sur les chunks
**But** : la collision lit la même source que le rendu → on marche sur le terrain qu'on voit.

- Interface minimale :
  ```cpp
  struct IHeightField {
    virtual float HeightAt(float worldX, float worldZ) const = 0;   // tous consommateurs
    virtual bool  IsLoadedAt(float worldX, float worldZ) const = 0; // streaming chunks
    virtual ~IHeightField() = default;
  };
  ```
- `ChunkHeightField` : `(worldX,worldZ)` → `GlobalChunkCoord` + offset local (avec la constante terrain de la Phase 1) → `StreamCache::LoadTerrainChunk(coord)` → `TerrainChunk::SampleHeight(localX, localZ)` (bilinéaire, déjà existant `TerrainChunk.cpp:19`). `IsLoadedAt` = chunk résident.
- `HeightmapHeightField` : wrappe `TerrainRenderer::SampleHeightAtWorldXZ`. `IsLoadedAt` = (x,z) dans les bornes. Sert de fallback.
- Rebrancher `TerrainCollider` : remplacer le `const TerrainRenderer*` par un `const IHeightField*` (`TerrainCollider.cpp:29, 72`). Pointer sur `ChunkHeightField` avec repli `HeightmapHeightField` quand `!IsLoadedAt`.
- Transitivement rebranchés (via le collider) : spawn (`Engine.cpp:5006, 7933-7935`), snap entités (`9587, 10613, 10682, 10807`), reserve (`4897`), avatar éditeur (`9165`).

**Contrats / décisions** :
- Éviction LRU : on ne sample la hauteur que près du joueur → chunk toujours résident. Si `!IsLoadedAt` (cas limite), repli heightmap ; ne **jamais** déclencher de lecture disque synchrone dans le chemin de collision.
- Thread-safety : la collision tourne en main-thread (cohérent avec `StreamCache`/`ChunkRuntime` main-thread-only). Aucune query hors main-thread introduite.
- Hors périmètre : `NormalAt`/`WorldBounds` (aucun consommateur actuel — à ajouter quand un besoin réel apparaît) ; le footstep/splat (`SplatSampling`, `FootstepAudioSurfaceHook`) relève du **type de surface**, pas de la hauteur → domaine séparé, non traité ici.

**Risque** : moyen. C'est le cœur du chantier.

### Phase 3 — Retirer la heightmap legacy du chemin de jeu
**But** : éliminer définitivement la dualité.

- Une fois les chunks canoniques et les cartes pourvues de `terrain.bin`, sortir `TerrainRenderer` du rendu de jeu. Conserver au plus la reconstruction côté éditeur si elle reste utile, sinon supprimer.
- Supprimer le code mort associé (cf. audit).

**Dépendance** : migration des cartes existantes vers `terrain.bin` (Feyhin Lokcthat n'a pas encore de `terrain.bin` — `game/data/zones/demo_plains` n'a que `chunk.meta`+`instances.bin`). Cette migration de contenu est un pré-requis de la Phase 3, pas des phases 0-2.

**Risque** : élevé tant que la migration de contenu n'est pas faite → à ne déclencher qu'après validation des phases 0-2 en jeu.

---

## 4. Découpage de livraison & ordre de merge

| Phase | Livrable | Dépend de | Risque | Mergeable seul |
|---|---|---|---|---|
| 0 | Rendu exclusif (gating) | — | faible | ✅ oui (soulage le symptôme) |
| 1 | Constante terrain 256 séparée | — (indépendant de 0) | moyen | ✅ oui |
| 2 | `IHeightField` + collision chunks | Phase 1 | moyen | après Phase 1 |
| 3 | Retrait heightmap legacy | Phase 2 + migration cartes | élevé | en dernier |

Recommandation : livrer **Phase 0** d'abord (PR isolée, gain immédiat), puis **Phase 1**, puis **Phase 2**. **Phase 3** seulement après validation en jeu des précédentes et migration de contenu.

---

## 5. Validation

- **Phase 0** : en jeu, le scintillement triangulaire au sol disparaît en zone chunkée ; le terrain reste visible (pas de trou) sur carte heightmap-only.
- **Phase 1** : jeu et éditeur affichent le terrain aux mêmes coordonnées monde ; pas de trou entre chunks voisins ; streaming sans chunk manquant/dupliqué aux frontières.
- **Phase 2** : le personnage marche exactement sur la surface visible en zone chunkée (plus de décalage hauteur) ; spawn/snap corrects.
- **Phase 3** : aucune régression visuelle/collision après retrait du legacy sur cartes migrées.
- Build validé via CI Windows (pas de toolchain local).
- **Convention `CLAUDE.md` (winding/culling)** : aucune phase ne touche `frontFace`/`cullMode` — non négociable.

---

## 6. Fichiers clés

- `src/client/app/Engine.cpp` — passe Geometry (`5059-5305`), gate Phase 0 (`5070`), consommateurs hauteur, placement éditeur de référence (`12169-12170`).
- `src/client/render/terrain_chunk/TerrainChunkRenderer.{h,cpp}` — placement (`1129-1130`), à exposer `GetLastDrawnChunkCount()`.
- `src/client/world/WorldModel.{h,cpp}` — constantes + conversion monde→coord + bounds.
- `src/client/world/terrain/TerrainChunk.{h,cpp}` — `SampleHeight` (`19`), résolution/cellule (`15-19`).
- `src/client/render/terrain/TerrainRenderer.{h,cpp}` — sampling legacy (`1311`), états pipeline (`551-553`).
- `src/client/render/TerrainChunkPipeline.cpp` — état depth chunk (`294-296`).
- `src/client/gameplay/TerrainCollider.cpp` — à rebrancher sur `IHeightField` (`29, 72`).
- `src/client/world/StreamingScheduler.cpp` — centre chunk (`31-32`).

**Déploiement** : ✅ client uniquement à toutes les phases — **pas de redéploiement serveur**.
