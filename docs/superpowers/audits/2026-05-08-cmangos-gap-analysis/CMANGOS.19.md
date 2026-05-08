# CMANGOS.19 — Maps (thread per map / subclasses / SpawnGroup)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.19_Maps_thread_per_map_subclasses_spawngroup.md](../../../../tickets/CMANGOS/CMANGOS.19_Maps_thread_per_map_subclasses_spawngroup.md)
> **Priorité** : P2 — gameplay essentiel (squelette simulation)
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de classe `Map` abstraite côté shard, pas de
`DungeonMap`/`BattleGroundMap`/`WorldMap`, pas de `MapUpdater`
multi-thread, pas de `SpawnGroup` data-driven.

## 2. Preuves dans le code

**Existant (orthogonal) :**
- [engine/server/SpawnerRuntime.h](../../../../engine/server/SpawnerRuntime.h) — spawners JSON par zone (pas
  groupes coordonnés)
- [engine/server/ZoneTransitions.cpp](../../../../engine/server/ZoneTransitions.cpp) — transitions de zone (concept
  proche `Map`)
- M14.4 — Persistance character state (touche au sujet mais pas
  Map persistante)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/maps/` — dossier inexistant
- ❌ `Map` abstraite (Update/Add/Remove/Tick orchestration)
- ❌ `WorldMap`, `DungeonMap`, `BattleGroundMap` sous-classes
- ❌ `MapManager` singleton (GetOrCreate par mapId+instanceId)
- ❌ `MapUpdater` pool de threads workers (1 map = 1 thread)
- ❌ `MapPersistentState` (loot locks, boss kills par instance)
- ❌ `SpawnGroup` (groupes coordonnés data-driven)
- ❌ `ObjectPosSelector` (placement libre sans collision)
- ❌ Tables `map_persistent_state`, `spawn_group_template`,
  `spawn_group_member`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — les milestones touchent au sujet (ZoneTransitions,
SpawnerRuntime, M14.4 persistance) mais l'**architecture "Map = thread"**
+ sous-classes par type d'instance + SpawnGroup data-driven n'existe pas.

## 4. Écart par rapport à la spec CMANGOS

100% absent pour la structure Map. C'est un **squelette architectural**
fondamental — sans lui, pas de cap propre sur le multi-instance, pas de
parallélisme entre maps, pas de donjons/raids.

Le pattern "Map = thread" supprime 90% des locks intra-map et est un
**game-changer de simplicité**. Le parallélisme inter-map donne la
scalabilité horizontale.

## 5. Effort estimé

**XL** (multi-sprints) — refonte transversale du shard :
- Map abstraite + 3 sous-classes
- MapManager + MapUpdater pool threads
- SpawnGroup loaders + SQLStorage
- Migration DB (3 tables)
- Refactor de tout le code shard existant qui parle d'entités sur le
  shard global (SpawnerRuntime, ZoneTransitions, GatheringSystem,
  CraftingSystem) pour qu'ils passent par leur `Map`

Pas de wire-breaking côté protocole (server-only).

## 6. Valeur joueur/serveur

**Critique (architecturalement)** mais **invisible joueur**. Sans
multi-instance, pas de donjon (nécessite copie privée par groupe), pas
de BG (nécessite arène par match). Sans `MapUpdater`, pas de scalabilité
horizontale (1 thread shard = capacité limitée).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — `Map` contient des `WorldObject`
- **CMANGOS.03 Grids** — chaque `Map` a sa `GridMap`
- **CMANGOS.05 vmap** — collisions par `Map`
- **CMANGOS.07 AI**, **.11 Combat**, **.20 MotionGenerators** — tickés
  par `Map`

→ **CMANGOS.19 dépend de la chaîne P1 + plusieurs P2**. Ticket à venir
**tard** dans la roadmap, mais c'est un **déblocant aval** pour
CMANGOS.10 BattleGround.

## 8. Risque / piège ⚠️

- ⚠️ **Refonte transversale** — touche tout le code shard qui parle
  d'entités/spawns/zones. Risque de régressions massives si fait à
  la va-vite.
- ⚠️ **Migration DB** — 3 tables (`map_persistent_state` +
  `spawn_group_template/member`). Idempotent.
- ⚠️ **Threading** — un bug map → tout le worker bloque. Watchdog par
  thread + timeout tick obligatoire.
- ⚠️ **Migration entité entre Maps** — joueur passe d'un donjon au
  monde ouvert. Transfer ownership thread-safe (queue cross-thread,
  pattern Messager — cf. CMANGOS.35).
- ⚠️ **Persistance partielle** — `MapPersistentState.data_blob` =
  format binaire libre. Versioning + magic obligatoire sinon parsers
  cassent à chaque évolution.
- ⚠️ **Reset timers** — quand reset un raid (lockout 7 jours) ?
  WeeklyMaintenance cron requis (cf. CMANGOS.08 Arena pattern).
- ⚠️ **SpawnGroup max_active** — coordination "pop A OU B max 1
  actif" demande sync globale au sein du groupe. Politique : random à
  chaque tentative ? Pondéré par chance ?
- ⚠️ **Conflit avec architecture data-driven LCDLLN** — comme CMANGOS.02,
  ce ticket pousse l'archi cmangos. Décision archi à valider en amont.

## 9. Recommandation finale

🔧 **Adapter et faire**, **avec précautions**, **après** la chaîne P1 :

1. **Étape 0 (cadrage critique)** : décider l'archi instance.
   - Option A (pragmatique) : étendre LCDLLN actuel (zones, spawners
     JSON) avec une notion d'instance + persistance partielle.
     **Effort L**.
   - Option B (cmangos pur) : refonte complète Map + thread per map.
     **Effort XL**, parallélisme horizontal.
   - Recommandation : **Option A en V1**, B reportée si bottleneck
     scalabilité.
2. **Étape 1** : ajouter notion d'`instanceId` aux entités shard. Map
   = `(zoneId, instanceId)`.
3. **Étape 2** : `MapPersistentState` minimal (loot locks par
   `(accountId, instanceId)`).
4. **Étape 3** : `SpawnGroup` data-driven (DB + loader).
5. **Étape 4** : `MapUpdater` pool threads (bench gain perf vs single
   thread).
6. **Étape 5** : sous-classes (`DungeonMap`, `BattleGroundMap`) au fur
   et à mesure des besoins.

À planifier après CMANGOS.02/03/05 et **avant** CMANGOS.10
BattleGround. Effort important, à phaser progressivement.

---

*Audit du 2026-05-08. Mises à jour : —*
