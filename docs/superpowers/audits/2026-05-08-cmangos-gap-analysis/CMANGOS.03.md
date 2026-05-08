# CMANGOS.03 — Grids (spatial partitioning / AoI)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.03_Grids_spatial_partitioning_aoi.md](../../../../tickets/CMANGOS/CMANGOS.03_Grids_spatial_partitioning_aoi.md)
> **Priorité** : P1 — squelette
> **Cible** : shard

## 1. Statut implémentation

🟡 **Partiel** — partitionnement spatial 2D + interest set présents
(architecture proche), mais le pattern Visitor templated, la state
machine GridState, les Searchers composables et `MessageDistDeliverer`
sont absents.

## 2. Preuves dans le code

**Existant :**
- [engine/server/SpatialPartition.h](../../../../engine/server/SpatialPartition.h) (101 lignes) — couvre une bonne partie :
  - `kCellSizeMeters` (configurable depuis `engine::world::kSpatialCellSizeMeters`)
  - `CellCoord` 2D + `CellCoordHash`
  - `CellGrid::UpsertEntity(entityId, posX, posZ, outCell)` — insertion/déplacement
  - `CellGrid::RemoveEntity` — retrait
  - `CellGrid::BuildInterestSet` (7×7 cells autour du centre, `kBaseInterestRadiusCells=3`)
  - `CellGrid::GatherEntityIds(cells, outIds)` — listing par cellule
  - `ComputeInterestDiff` (entering/leaving cells)
- [engine/server/SpatialPartition.cpp](../../../../engine/server/SpatialPartition.cpp) — implémentation
- Constantes alignées sur `engine::world::kZoneSize` et `kSpatialCellSizeMeters`
  → single source of truth déjà respectée

**Manquant (vs spec ticket) :**
- ❌ `GridReference<T>` / `GridRefManager<T>` (intrusive list pattern) — actuellement
  `std::unordered_map<EntityId, CellCoord>` (donc moins cache-friendly mais OK).
- ❌ `Grid<T>` templated par type (`Player`, `Creature`, `GameObject` séparés) — actuellement
  un seul `EntityId` opaque, pas de typage compile-time
- ❌ Visitor pattern templated (`ContainerVisitor`, dispatch statique compile-time)
- ❌ Searchers composables (`CreatureSearcher<Check>`, `WorldObjectSearcher<Check>`)
- ❌ Checks prêts à l'emploi (`NearestAttackableUnitInObjectRangeCheck`, etc.)
- ❌ `MessageDistDeliverer` (broadcast spatial 3D)
- ❌ `VisibleNotifier` (recompute visibility par joueur)
- ❌ State machine `GridState` (Loaded / Active / Idle / Removal) avec timers
- ❌ Activation conditionnelle des cellules (tick uniquement les cellules `Active`)
- ❌ Config `grid.idle_timeout_sec`, `grid.unload_timeout_sec`

## 3. Recouvrement milestones existantes

✅ **Couvert** — M13.2 (Spatial partition cells 64m + interest set) est
clairement la milestone qui a livré la partie existante. Le ticket
CMANGOS.03 va plus loin (Visitor + StateMachine).

## 4. Écart par rapport à la spec CMANGOS

L'écart est moins large qu'il n'y paraît : le **noyau spatial** est livré.
Ce qui manque est de l'**outillage** au-dessus :

1. **Typage compile-time** : `EntityId` actuel est opaque. Si on veut un
   visitor templated par type (`Player` vs `Creature`), il faut soit
   1 grid par type, soit un dispatch typé. Cela dépend si on adopte
   CMANGOS.02 hiérarchie ou non.
2. **GridState state machine** : aujourd'hui la grille est statique, toutes
   les cellules sont dans le même état logique. Pas de tick conditionnel
   selon présence joueurs → CPU gaspillé sur les zones désertes (mais
   probablement non-bloquant tant que le shard est dimensionné modérément).
3. **GridNotifiers** : actuellement le code shard pour broadcast (chat,
   replication) doit gather lui-même les entityIds via `GatherEntityIds`
   puis itérer. Un `MessageDistDeliverer` standardiserait ce pattern.
4. **Searchers composables** : pas critique tant que les usages sont peu
   nombreux (chercher cible la plus proche, chercher AoE), mais devient
   utile dès qu'on accumule 5+ usages.

## 5. Effort estimé

**M** (2-3 PR) — extension de l'existant, pas une refonte :
- PR 1 : `GridState` state machine + tick conditionnel `Active`-only
- PR 2 : Visitor + Searchers + Checks (1 visitor + 1 searcher template)
- PR 3 : `MessageDistDeliverer` + tests broadcast spatial

Pas de migration DB, pas de wire-breaking.

## 6. Valeur joueur/serveur

**Élevée** — AoI scalable, pré-requis performance pour les zones populeuses.
Moins critique court-terme que CMANGOS.01 ou CMANGOS.06 (le système actuel
fonctionne déjà), mais devient critique dès qu'on dépasse 50-100 joueurs
co-localisés. Valeur principale : **éviter une refonte forcée plus tard
sous pression incident**.

## 7. Dépendances bloquantes

- **CMANGOS.02 Entities** — partiellement requis pour le dispatch templated
  par type. Si on garde l'archi data-driven (`EntityId` opaque), on peut
  faire CMANGOS.03 sans CMANGOS.02 mais on perd le bénéfice du Visitor.
- **CMANGOS.05 vmap** — pas bloquant pour le grid 2D ; les requêtes 3D
  précises (LOS, height) délèguent à vmap (ticket §Notes).

## 8. Risque / piège ⚠️

- ⚠️ **Refonte de `SpatialPartition`** — l'existant a une API unifiée
  (`EntityId` opaque). Ajouter Visitor par type sans casser cette API
  demande soit (a) un wrapper templated, soit (b) une refonte. **Risque
  de régression** sur le code shard existant qui consomme `SpatialPartition`.
- ⚠️ **Cell size hardcodé** — actuellement tiré de `engine::world::kSpatialCellSizeMeters`
  (constexpr). Pour rendre configurable via `config.json` (`grid.cell_size_meters`),
  il faudra repasser de constexpr à variable runtime → impact sur les
  hash/tableaux statiques.
- ⚠️ **Steady_clock vs wall_clock** — la state machine (timers idle/unload)
  doit utiliser `std::chrono::steady_clock`, pas wall clock (cf. ticket
  §Notes — sinon changement d'heure système peut décharger toute la map).
- ⚠️ **Persistance à l'unload** — au passage `Idle → Removal`, les
  Creatures avec état modifié (HP bas, position non-default) doivent être
  sauvegardées en DB. Risque de perte d'état si non géré.
- Pas de wire-breaking, pas de migration DB.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.02 (au moins la partie ObjectGuid)
— l'existant LCDLLN est solide, on étend plutôt qu'on refait :

1. **Étape 1** : ajouter `GridState` state machine (Loaded/Active/Idle/Removal)
   au-dessus de l'existant, sans changer l'API publique. Tick conditionnel
   sur `Active` seulement.
2. **Étape 2** : décider si Visitor templated. Option A — garder
   `EntityId` opaque + visitor générique (moins typé, plus simple). Option B —
   adopter typage par classe (requiert CMANGOS.02). **Recommandation** :
   Option A tant que la hiérarchie cmangos n'est pas adoptée.
3. **Étape 3** : ajouter `MessageDistDeliverer` (utilisable par CMANGOS.01
   shard-side `say/yell/emote`).
4. **Étape 4** : exposer `cell_size_meters` via config si besoin d'ajustement
   profilage (sinon laisser constexpr).

**À planifier après CMANGOS.01** (déblocant immédiat) et **après** la
décision CMANGOS.02 (impact direct sur le typage Visitor).

---

*Audit du 2026-05-08. Mises à jour : —*
