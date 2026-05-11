# Waves 17-N — Gaps server réels : design roadmap

> **Sortie de session de brainstorming** (2026-05-11).
> **Sujet** : Roadmap d'intégration des **vrais gaps server** restants après inventaire du code au 2026-05-11 (post Wave 16).
> **Suite de** : [`2026-05-08-cmangos-gap-analysis-design.md`](2026-05-08-cmangos-gap-analysis-design.md) et des Waves 1-16 livrées (PRs #568 → #590).

## 1. Contexte et correction de l'analyse initiale

Une première analyse (Explore agent, basée sur les commit messages) suggérait que 35 tickets étaient "partiels" et 7 "non commencés". **L'audit direct du code au 2026-05-11 montre une réalité bien plus avancée** :

### Modules ALREADY DONE (foundation + tests présents)

| # | Ticket | Preuve |
|---|--------|--------|
| 06 | Accounts (RBAC) | `src/masterd/account/AccountRole.h/cpp` + `AccountRoleService` + tests. Migration `0043_phase_1c_account_roles.sql` (4 niveaux : player/moderator/game_master/administrator). |
| 13 | Database | `src/shared/db/SQLStorage.h`, `SqlDelayThread.h/cpp`, `SqlPreparedStatement.h/cpp` + tests. Migration `0041_phase_1a`. |
| 16 | Globals (Conditions) | `src/shardd/internals/globals/ConditionMgr.h/cpp` + `GraveyardManager` + `LocaleStrings` + `ObjectAccessor` + tests. Migration `0042_phase_1b`. |
| 22 | vmap (LOS) | `src/shardd/internals/vmap/VMapManager::IsInLineOfSight()`, `VMapFormat`, `VMapStreamer` + tests. |
| 26 | Combat (ThreatList) | `src/shardd/combat/ThreatList.h/cpp` + Runtime + tests (Wave 8 #576). |
| 30 | Quests | `src/masterd/quests/QuestState.h`, `MysqlQuestStateStore.h`, `src/shardd/gameplay/quest/QuestRuntime.h`. |
| 31 | Reputation | `src/masterd/reputation/MysqlReputationStore`, `ReputationHandler`, client UI. |
| 32 | DBScripts | `src/shardd/dbscripts/DBScript.h/cpp` + Runtime + tests (Wave 8 #576). |
| 33 | World (WorldState) | `src/masterd/world/WorldStateRegistry.h` + tests. |
| 34 | AI (EventAI) | `src/shardd/ai/EventAI.h/cpp` + Runtime + tests + `MotionGeneratorStack`. |
| 35 | Anticheat + Cinematics | `src/shardd/anticheat/AntiCheatGameplay.h` + Runtime + tests. `src/shardd/cinematics/CinematicSequence.h` + tests + Wave 11 CinematicStore (#580). |
| 36 | GameEvents + LFG | `src/masterd/events/GameEventManager.h` + `MysqlGameEventStore` + tests. `src/masterd/lfg/LfgQueue.h` + tests. |

### Vrais gaps confirmés (greppés au 2026-05-11)

| # | Module | Vérification |
|---|--------|--------------|
| **W20** | `Grid::Visitor<T>` / `GridNotifier` | `grep "Visitor\|Notifier" src/` → 0 hit. SpatialPartition + GridState existent mais pas le pattern visitor. |
| **W21** | `Unit` / `Player` / `WorldObject` / `Creature` | `grep "class Unit\|class Player.*:\|class Creature\|class WorldObject" src/shardd/entities/*.h` → 0 hit. Seul `Object.h` base existe. |
| **W23b** | Pool **nested** / persistence | `head -30 src/shardd/pools/PoolManager.h` montre weighted simple, sans `pool_pool` ni `pool_member_state`. |
| **W24** | Groups (party/raid) | `grep "GroupManager\|GroupReference" src/` → 0 hit. Module entier absent. |
| **W26b** | `HostileRefManager` bidir | `grep "HostileRefManager\|HostileReference" src/` → 0 hit. ThreatList simple existe mais pas le pattern bidirectionnel sym cleanup. |
| **W27** | Navmesh (recast/detour) | `grep -i "recast\|detour" vcpkg.json` → 0 hit. Pas de dépendance, pas de code. |
| **W28b** | Maps subclasses `WorldMap`/`DungeonMap`/`BattlegroundMap` | `grep "class WorldMap\|class DungeonMap\|class BattlegroundMap"` → 0 hit. Seul `InstanceManager` existe. |
| **W29** | `SpellMgr` / `Aura` / `ProcMgr` | `grep "class SpellMgr\|class Aura\|class ProcMgr" src/shardd/spell/*.h` → 0 hit. Seul `SpellFamilyMask` + Runtime existent (Wave 9 #577). |

### Conclusion correction

**8 gaps réels, pas 22 PRs**. La roadmap effective est plus courte et plus focalisée. Les autres tickets sont :
- Soit déjà implémentés avec runtime + tests,
- Soit nécessitent du **wiring incrémental** au cas par cas (à faire au moment où le besoin émerge, pas en wave dédiée).

## 2. Périmètre roadmap (corrigé)

8 PRs séquentielles, server-only, ordonnées par dépendance :

| Wave | Ticket | Effort | Priorité |
|------|--------|--------|----------|
| **17** | W21 Entities suite (Unit/Player/WorldObject/Creature) | L | P1 critique — base pour W24/26/29 |
| **18** | W20 GridVisitor + GridNotifier | M | P1 — base broadcast spatial |
| **19** | W26b HostileRefManager bidir (sur ThreatList existant) | M | P2 — étend Combat |
| **20** | W23b Pool nested + persistence | M | P2 — étend PoolManager |
| **21** | W28b Maps subclasses (WorldMap/Dungeon/BG) | M | P2 — wraps tout |
| **22** | W24 Groups complete (master-side) | L | P2 — bloc pour party play |
| **23** | W29 Spells Mgr/Aura/Proc | L | P2 — gameplay magique |
| **24** | W27 Navmesh (recast/detour intégration) | L | P3 — PNJ patrol intelligente |

### Hors scope

- 8 tickets P3 (29 Anticheat, 30 Cinematics, 31 GameEvents, 33 LFG, 36 OutdoorPvP, 32 GMTickets, 39 Skills, 42 Weather) — déjà faits, seuls les hooks d'intégration restent à câbler au cas par cas.
- 34 Metric, 37 Platform, 38 PlayerBot, 40 Tools, 44 AHBot, 45 SRP6 — explicitement différés.

## 3. Décisions structurantes

| # | Question | Décision |
|---|---------|----------|
| 1 | Ordre Waves 17-24 | Séquentiel strict. W17 (Entities) bloque W23 (Spells, qui agissent sur Unit). W18 (Grid Visitor) bloque W22 (Groups broadcast). |
| 2 | Wire-breaking | Une seule Wave : **W17** (UPDATE_OBJECT opcodes nouveau si non déjà présents). Bump `kProtocolVersion` si nécessaire. |
| 3 | Migrations DB | Reprendre à partir de `0059_*`. Toutes idempotentes. |
| 4 | Tests | 1 binaire test par module. Smoke test sous-vague à la fin de W22 et W24. |
| 5 | Pas de UI client | Aucune nouvelle UI gameplay. Le client existant (HUD combat, group UI à créer post-W22 si manquant) consomme via opcodes existants. |
| 6 | CODEBASE_MAP.md | Mis à jour par PR — sections nouvelles ajoutées au fil de l'eau (cf. §6 ci-dessous). |
| 7 | Splitting | W17 Entities suite peut être split en 17a (Unit) / 17b (Player+WorldObject) / 17c (Creature+Network) si diff > 800 lignes. W23 Spells idem (23a Mgr+Spell, 23b Aura+Proc). |

## 4. Détail des 8 Waves

### Wave 17 — W21 Entities suite (Unit/Player/WorldObject/Creature)

- **Quoi** : étendre la base `Object.h` (existante, Wave 7 PR #575). Hiérarchie complète Object → Unit → Player + Object → WorldObject + Unit → Creature.
- **Code** :
  - `src/shardd/entities/Unit.h/cpp` (HP, stats, factionId, posture combat)
  - `src/shardd/entities/Player.h/cpp` (extends Unit, hérite UpdateFields player-spécifiques, inventaire, quêtes)
  - `src/shardd/entities/WorldObject.h/cpp` (statique, GameObject, transport futur)
  - `src/shardd/entities/Creature.h/cpp` (extends Unit, AI hookée)
- **UpdateFields** : enums complets (existants pour Object, à étendre pour Unit/Player). `UpdateMask` génération delta sur `Update()` (helper existant à utiliser).
- **Wire** : `UPDATE_OBJECT` opcode existe-t-il ? À vérifier. Si non, ajout opcode 49 push. Sinon, juste serialisation étendue (pas wire-breaking strict).
- **Tests** : `unit_tests`, `player_tests`, `creature_tests`, `update_mask_delta_tests`.
- **Risque** : PR la plus invasive. Splitter si > 800 lignes.
- **Déploiement** : **REDÉPLOIEMENT SERVEUR** (probable wire-breaking → preciser à la PR).

### Wave 18 — W20 GridVisitor + GridNotifier

- **Quoi** : pattern `GridVisitor<T>` templated + `GridNotifier` sur la base `SpatialPartition` + `GridState` existante.
- **Code** :
  - `src/shardd/world/GridVisitor.h` (templated, visitor double-dispatch)
  - `src/shardd/world/GridNotifier.h` (broadcast packet à entités voisines)
  - Extension `SpatialPartition` : méthodes `Visit<T>(pos, radius, visitor)`, `Notify(pos, radius, notifier)`.
- **API** : `Map::VisitNearbyPlayers(pos, radius, fn)`, `Map::BroadcastToNearby(pos, radius, packet)`.
- **Tests** : `grid_visitor_tests` (1000 entités, AoI 50m, distribution correcte).
- **Wire** : pas de changement.
- **Bloc débloqué** : tout broadcast spatial (chat /say /yell, combat log local, spell visual, etc.). Aujourd'hui tout est global.
- **Déploiement** : **REDÉPLOIEMENT SHARD**.

### Wave 19 — W26b HostileRefManager bidirectionnel

- **Quoi** : étendre `ThreatList` existant avec le pattern `HostileRefManager` : Unit A a une liste de qui le hait (`m_haters`), Unit B a une liste de qui il hait (`m_hated`). Les deux refs sont liées pour cleanup sym sur death/despawn.
- **Code** :
  - `src/shardd/combat/HostileRefManager.h/cpp`
  - `src/shardd/combat/HostileReference.h` (le node intrusif)
  - Hook `Unit::Die()` (Wave 17) appelle `HostileRefManager::CleanupOnDeath()`.
- **Tests** : `hostile_ref_bidir_tests` (cleanup A.die → B.haters list vide), `hostile_ref_attach_detach_tests`.
- **Wire** : pas de changement.
- **Déploiement** : **REDÉPLOIEMENT SHARD**.

### Wave 20 — W23b Pool nested + persistence

- **Quoi** : étendre `PoolManager` existant. Ajouter pools imbriqués (un pool peut contenir un pool) + persistance d'état runtime (DB).
- **Migration** : `0059_pool_extensions.sql` — `pool_pool` table (parent_pool_id, child_pool_id, weight), `pool_member_state` (runtime state persisté pour respawn timers).
- **Code** :
  - Extension `src/shardd/pools/PoolManager.h/cpp` (`AddNestedPool`, `EvaluateNested`, `SaveState/LoadState`).
  - `src/masterd/pools/MysqlPoolStateStore.h/cpp` (master-side persistence).
- **Tests** : `pool_nested_tests` (cycle detection), `pool_persistence_tests` (round-trip save/load).
- **Wire** : pas de changement.
- **Déploiement** : **REDÉPLOIEMENT SHARD + migration**.

### Wave 21 — W28b Maps subclasses (WorldMap / DungeonMap / BattlegroundMap)

- **Quoi** : sous-classes `Map` avec sémantique propre. WorldMap = ouvert, persistant. DungeonMap = instance lockée par player/group, save state. BattlegroundMap = courte durée, scoreboard intégré.
- **Code** :
  - `src/shardd/maps/Map.h` (base abstraite, déjà existant `InstanceManager`)
  - `src/shardd/maps/WorldMap.h/cpp`
  - `src/shardd/maps/DungeonMap.h/cpp`
  - `src/shardd/maps/BattlegroundMap.h/cpp`
  - Refactor `InstanceManager` pour servir de factory : `CreateMap(map_id, instance_id, type)`.
- **Tests** : `world_map_tests`, `dungeon_map_tests`, `battleground_map_tests`.
- **Wire** : pas de changement.
- **Déploiement** : **REDÉPLOIEMENT SHARD**.

### Wave 22 — W24 Groups complete (master-side)

- **Quoi** : système de groupes (party 5p, raid 10/25p). Master-side car cross-shard si raid futur. Pattern `GroupReference` intrusif.
- **Migration** : `0060_groups.sql` — `groups`, `group_members`, `group_loot_rolls`.
- **Code** :
  - `src/masterd/Groups/GroupManager.h/cpp`
  - `src/masterd/Groups/Group.h/cpp`
  - `src/masterd/Groups/GroupReference.h`
  - `src/masterd/handlers/groups/GroupHandler.h/cpp` (opcodes invite/accept/leave/loot)
  - `src/shardd/loot/LootRule.h` (interface) + 4 impls (FFA, RR, ML, NBG)
- **Wire** : opcodes nouveaux (51-54 ou suivants). Additif, pas wire-breaking.
- **Tests** : `group_reference_tests`, `group_manager_tests`, `loot_rule_tests`.
- **Déploiement** : **REDÉPLOIEMENT MASTER + SHARD + CLIENT** (UI roll #560 utilise déjà ces opcodes côté client).

### Wave 23 — W29 Spells (SpellMgr / Aura / ProcMgr)

- **Quoi** : SpellMgr (catalogue runtime), Spell (instance de cast, state machine), Aura (effet persistant sur Unit), ProcMgr (déclencheur conditionnel).
- **Migration** : `0061_spells_aura.sql` — `character_auras` (persistance logout), `spell_proc_template`.
- **Code** :
  - `src/shardd/spell/SpellMgr.h/cpp`
  - `src/shardd/spell/Spell.h/cpp` (state machine : Idle → Casting → Casted → Resolved)
  - `src/shardd/spell/Aura.h/cpp` (tick periodic, expire, stack rules)
  - `src/shardd/spell/ProcMgr.h/cpp` (hooks events : OnMeleeHit, OnSpellCrit, etc.)
  - `src/shardd/spell/SpellTargetType.h` (matrix ~40 types)
- **Intégration** : Aura sur Unit (Wave 17), ProcMgr hooked dans Combat (existant), conditions via ConditionMgr (déjà fait).
- **Tests** : `spell_cast_state_machine_tests`, `aura_tick_tests`, `proc_trigger_matrix_tests`.
- **Wire** : push Aura via `UPDATE_OBJECT` Wave 17.
- **Déploiement** : **REDÉPLOIEMENT SHARD + migration**.

### Wave 24 — W27 Navmesh (recast + detour)

- **Quoi** : ajouter Recast Navigation (vcpkg) + intégration au système motion existant.
- **Dépendances** : ajouter `recastnavigation` à `vcpkg.json`.
- **Migration** : `0062_creature_movement.sql` — `creature_movement(creature_guid, point_idx, x/y/z, wait_ms, script_id)`.
- **Code** :
  - `src/shardd/Movement/NavmeshManager.h/cpp` (charge `.navmesh` tiles par map)
  - `src/shardd/Movement/PathFollowMotion.h/cpp` (Detour-driven, branche dans `MotionGeneratorStack`)
  - `src/shardd/Movement/WaypointMgr.h/cpp` (lit `creature_movement` DB)
- **Assets** : `.navmesh` tiles via `tools/navmesh_builder/` (à créer — proposer création comme PR séparée si effort important).
- **Tests** : `navmesh_pathfind_tests` (3-4 scenarios fixture), `waypoint_mgr_tests`.
- **Wire** : pas de changement (la spline existe déjà Wave 4).
- **Déploiement** : **REDÉPLOIEMENT SHARD + migration**. Tool à fournir séparément.

## 5. Smoke tests par sous-vague

### Sous-vague A (Waves 17-19) — `wave_entities_combat_smoke`
1. Boot avec migrations 0001-0058 (existantes).
2. Player créé (Wave 17), apparaît dans grid (Wave 18 visitor).
3. Creature spawne (Wave 17), aggro le Player → ThreatList + HostileRefManager bidirectionnel (Wave 19).
4. Player tue Creature → cleanup sym, Creature.haters vide, Player.hated vide.

### Sous-vague B (Waves 20-22) — `wave_pools_maps_groups_smoke`
1. Pool nested spawne 3 creatures dans WorldMap (Waves 20+21).
2. Player crée un groupe party 2p (Wave 22).
3. Loot conditionnel FFA dispatché à l'un des 2 (LootRule FFA).
4. État pool persiste après restart shard.

### Sous-vague C (Waves 23-24) — `wave_spells_navmesh_smoke`
1. Creature spawne, navigue via navmesh Waypoint A→B→C (Wave 24).
2. Player cast un spell (Wave 23), Aura "DoT" appliquée sur Creature.
3. DoT tick chaque seconde → ProcMgr déclenche secondary effect.
4. Aura expire après N ticks, cleanup.

## 6. Mises à jour CODEBASE_MAP.md

Sections à ajouter à `CODEBASE_MAP.md` (1 commit dédié dans la PR du présent design doc) :

| Section nouvelle | Contenu |
|------------------|---------|
| **18. Structure `src/shardd/`** | Tree des sous-dossiers ai/anticheat/.../combat/dbscripts/entities/internals/maps/pools/spell/world/, role de chacun. |
| **19. Admin / RBAC (Waves 1-3)** | AccountRole 4 niveaux, AccountRoleService, AdminCommandHandler, audit log, `slash_commands.json`. |
| **20. Persistence stores (Wave 5, 10, 11)** | Mysql*Store + InMemory*Store pattern (Guild/BG/Loot/Auction/Arena/Skills/OutdoorPvp/Mail/Cinematic/PoolState). |
| **21. PacketLog (Waves 12-16)** | Ring buffer, dump on close, /packetlog admin command. |
| **22. Entities foundation (Wave 7)** | Object.h/cpp, ObjectGuid.h, UpdateField.h, UpdateMask.h. |
| **23. Boot wiring `src/shardd/main_linux.cpp`** | Ordre d'init des runtimes : EventAI → PoolManager → ThreatList → DBScripts → AntiCheat → SpellFamily → InstanceManager. |

Chaque Wave 17-24 ajoute si nécessaire sa propre section ou étend une existante.

## 7. Risques et mitigation

| # | Risque | Mitigation |
|---|--------|------------|
| 1 | W17 Entities est wire-breaking → casse clients déployés | Lock-step déploiement, annoncer maintenance. Si pas réellement wire-breaking, le préciser à la PR. |
| 2 | W21 Maps subclasses thread-safety | Tests TSan/ASan en CI pendant 1 semaine post-merge. |
| 3 | W24 recastnavigation vcpkg breakage | Pinner version dans `vcpkg.json`. |
| 4 | W22 Groups change le format Mail Group / Auction House Group | Auditer call-sites groupes avant merge ; mocks compat si besoin. |
| 5 | Migrations 0059-0062 conflits avec migrations en flight | Numérotation séquentielle stricte, vérifier `tools/migration_checksum/` avant chaque PR. |

## 8. Critères d'acceptance globaux

- Les 3 smoke tests sous-vague verts en CI Linux.
- Tests Wave 1-16 toujours verts (non-régression).
- 4 migrations DB idempotentes et rejouables (`0059`-`0062`).
- `CODEBASE_MAP.md` à jour (6 nouvelles sections + ajustements par Wave).
- Build Windows + Linux verts.
- Tous les `**Déploiement** :` mentionnés dans les PRs.

## 9. Estimation effort

- **8 Waves** au lieu de 22 initiales.
- **Effort cumulé** : ~10-14 jours de dev focus (vs 16-23 jours initiaux).
- **Cadence overnight observée Wave 1-16** : 12 PRs/nuit. Donc 8 PRs = **1 nuit**.

## 10. Sortie de cette session

Le présent design doc est suivi d'un **plan d'implémentation Wave 17 dédié** (Entities suite), à écrire via la skill `writing-plans`. Les Waves 18-24 obtiendront leur plan dédié au fur et à mesure (1 plan par Wave, créés à la demande quand la précédente est mergée — évite la péremption).

---

*Design rédigé le 2026-05-11 — sortie de session brainstorming /superpowers:brainstorming, corrigé après audit direct du code.*
