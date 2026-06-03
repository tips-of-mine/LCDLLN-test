# SERVER-CORE — Index des tickets

44 tickets issus de l'analyse de server-core-tbc (`SERVER-CORE_ANALYSIS.md`).
Chaque ticket reprend un module/concept de server-core, **adapté pour
LCDLLN** (pas de port littéral).

> **Format ticket** : Objectif / Dépendances / Livrables / Spec / Étapes
> / DoD / Notes — aligné sur la convention `tickets/M44/`.

> **Convention de nommage** : `SERVER-CORE.NN_<Module>_<keywords>.md`. La
> numérotation suit l'ordre alphabétique global (49 modules server-core),
> avec gaps pour les modules **non ticketés** (5 modules P4 considérés
> non actionnables).

## Synthèse par priorité

| Priorité | Description | Tickets |
|---|---|---|
| **P1** | Squelette & déblocants | 5 |
| **P2** | Gameplay essentiel | 23 |
| **P3** | Ajouts à valeur | 14 |
| **P4** | Cas spécifiques (subset) | 2 |
| **Skip** | Déjà couvert / non actionnables | 5 |
| **Total** | — | 44 ticketés / 49 modules |

## P1 — Squelette & déblocants (5 tickets)

| # | Ticket | Cible | Idée principale |
|---|---|---|---|
| 01 | [Chat](./SERVER-CORE.01_Chat_routing_safety_commands_split.md) | cross | `CanSpeak()` + `CheckEscapeSequences` + dispatch table-driven + split master(relay)/shard(proximité). **Déblocant MVP.** |
| 02 | [Entities](./SERVER-CORE.02_Entities_hierarchy_objectguid_updatefields.md) | shard | Hiérarchie Object→Unit + ObjectGuid 64 bits + UpdateFields/UpdateMask delta réseau. |
| 03 | [Grids](./SERVER-CORE.03_Grids_spatial_partitioning_aoi.md) | shard | Partitionnement spatial 2D + Visitor templated + GridStates + GridNotifiers. Base AoI scalable. |
| 04 | [Movement](./SERVER-CORE.04_Movement_server_authoritative_splines.md) | cross | Spline server-authoritative + interpolation client + MoveSplineFlag bitmask. |
| 05 | [vmap](./SERVER-CORE.05_vmap_collisions_los_height_dynamictree.md) | shard | LOS + height + tile streaming + BIH/BVH + DynamicTree (portes). |

## P2 — Gameplay essentiel (23 tickets)

| # | Ticket | Cible | Idée principale |
|---|---|---|---|
| 06 | [Accounts](./SERVER-CORE.06_Accounts_roles_security_levels.md) | master | Enum AccountRole 5 niveaux + HasLowerSecurity systématique. **Pré-requis Chat.** |
| 07 | [AI](./SERVER-CORE.07_AI_creature_registry_eventai_dbdriven.md) | shard | Registry+Selector pour CreatureAI + EventAI DB-driven. |
| 08 | [Arena](./SERVER-CORE.08_Arena_team_mmr_weekly_colisee.md) | master+shard | ArenaTeam persistant + MMR/rating + WeeklyMaintenance. **Spécifique colisée LCDLLN.** |
| 09 | [AuctionHouse](./SERVER-CORE.09_AuctionHouse_master_side_tick_mail_delivery.md) | master | Tick global + livraison mail + index RAM + multi-maisons. |
| 10 | [BattleGround](./SERVER-CORE.10_BattleGround_framework_instances_score.md) | shard | Framework hiérarchique + score sous-classé + reconnect. |
| 11 | [Combat](./SERVER-CORE.11_Combat_threat_hostile_decomposition.md) | shard | Décomposition CombatManager / ThreatManager / HostileRefManager bidirectionnel. |
| 12 | [Server](./SERVER-CORE.12_Server_packetlog_opcodetable_dbcstores.md) | cross | PacketLog rejouable + OpcodeRegistry typé. |
| 13 | [Database](./SERVER-CORE.13_Database_sqlstorage_async_prepared.md) | cross | SQLStorage cache RAM + SqlDelayThread async + prepared statements. |
| 14 | [DBScripts](./SERVER-CORE.14_DBScripts_dsl_data_driven_hot_reload.md) | shard | DSL ~30 commandes + délais + hot-reload. |
| 15 | [Groups](./SERVER-CORE.15_Groups_groupref_loot_rules_master.md) | master | GroupReference intrusive + loot rules pluggables + persistance partielle. |
| 16 | [Globals](./SERVER-CORE.16_Globals_conditions_objectaccessor_locales.md) | shard | Conditions data-driven + ObjectAccessor + GraveyardManager + Locales. |
| 17 | [Loot](./SERVER-CORE.17_Loot_templates_groups_reference.md) | shard | Templates DB génériques + loot groups + reference loot + conditions par drop. |
| 18 | [Mails](./SERVER-CORE.18_Mails_master_side_cod_expiration_massmail.md) | master | Schéma mail+items + COD + expiration auto + MassMailMgr. |
| 19 | [Maps](./SERVER-CORE.19_Maps_thread_per_map_subclasses_spawngroup.md) | shard | « Map = thread » + sous-classes (Dungeon/BG/World) + SpawnGroup + PersistentState. |
| 20 | [MotionGenerators](./SERVER-CORE.20_MotionGenerators_stack_navmesh_waypoints.md) | shard | Stack de générateurs + Detour navmesh + WaypointManager DB. |
| 21 | [Guilds](./SERVER-CORE.21_Guilds_schema_permissions_bank.md) | master | Schéma DB + permissions bitmask par rank + banque multi-onglets. |
| 22 | [Pools](./SERVER-CORE.22_Pools_weighted_spawns_nested.md) | shard | pool_template + nested pools + sélection pondérée. |
| 23 | [Quests](./SERVER-CORE.23_Quests_template_status_objectives_variant.md) | cross | Split QuestTemplate/Status + objectifs hétérogènes via std::variant + flags bitmask. |
| 24 | [Reputation](./SERVER-CORE.24_Reputation_faction_template_matrix.md) | shard | Faction template matrix + bitmask flags + paliers calculés + spillover parent. |
| 25 | [Social](./SERVER-CORE.25_Social_friends_ignored_presence.md) | master | FriendsManager + broadcast présence + ignored filtré expéditeur. |
| 26 | [Spells](./SERVER-CORE.26_Spells_split_template_aura_proc.md) | shard | Split Spell/Template/Aura/Proc + state machine cast + targeting matrix. |
| 27 | [Trade](./SERVER-CORE.27_Trade_two_phase_commit_anti_scam.md) | master | 2-phase commit + revérification serveur + timer anti-scam 6s. |
| 28 | [World](./SERVER-CORE.28_World_state_expression_dsl.md) | shard | WorldStateExpression mini-DSL conditions data-driven. |

## P3 — Ajouts à valeur (14 tickets)

| # | Ticket | Cible | Idée principale |
|---|---|---|---|
| 29 | [Anticheat](./SERVER-CORE.29_Anticheat_plugin_interface_position_deltas.md) | shard | Interface IServerSideValidator + validation deltas position. |
| 30 | [Cinematics](./SERVER-CORE.30_Cinematics_trigger_opcode_camera_validation.md) | cross | Trigger opcode + asset client-side + parsing serveur anticheat. |
| 31 | [GameEvents](./SERVER-CORE.31_GameEvents_seasonal_scheduler_cascading.md) | shard | Activation par event_id + scheduler central + events cascade. |
| 32 | [GMTickets](./SERVER-CORE.32_GMTickets_support_in_game.md) | master | Ticket = row DB + handler/Mgr séparés. |
| 33 | [LFG](./SERVER-CORE.33_LFG_queue_role_matchmaking.md) | master | Queue séparée du Mgr + matching par rôles + state machine joueur. |
| 34 | [Metric](./SERVER-CORE.34_Metric_influxdb_measurement_raii.md) | cross | InfluxDB line-protocol + Measurement RAII + flush async. |
| 35 | [Multithreading](./SERVER-CORE.35_Multithreading_messager_swap_execute.md) | cross | Pattern Messager (queue cross-thread, swap-and-execute). |
| 36 | [OutdoorPvP](./SERVER-CORE.36_OutdoorPvP_zone_plugins_objectives.md) | shard | Manager+ZonePlugin polymorphe + state machine par objectif. |
| 37 | [Platform](./SERVER-CORE.37_Platform_crash_dump_windows_client.md) | client | WheatyExceptionReport adapté pour crash dumps client Windows. |
| 38 | [PlayerBot](./SERVER-CORE.38_PlayerBot_headless_session_load_test.md) | shard | WorldSession headless via opcodes (load testing). |
| 39 | [Skills](./SERVER-CORE.39_Skills_craft_discovery_extra_item_proc.md) | shard | Hooks data-driven post-action (discovery, extra item). |
| 40 | [Tools](./SERVER-CORE.40_Tools_playerdump_cleaner_formulas_language.md) | cross | PlayerDump migration + Cleaner DB + Formulas + ServerLanguage. |
| 41 | [Util](./SERVER-CORE.41_Util_bytebuffer_pcqueue_trackable.md) | cross | ByteBuffer typé + ProducerConsumerQueue + UniqueTrackablePtr. |
| 42 | [Weather](./SERVER-CORE.42_Weather_markov_zones_authoritative.md) | shard | Markov chain par zone + server-authoritative + transition douce. |

## P4 — Cas spécifiques (2 tickets)

| # | Ticket | Cible | Idée principale |
|---|---|---|---|
| 44 | [AuctionHouseBot](./SERVER-CORE.44_AuctionHouseBot_internal_client_low_pop.md) | master | Bot interne via opcodes pour low-pop. **Conditionnel.** |
| 45 | [Auth](./SERVER-CORE.45_Auth_srp6_zero_knowledge_optional.md) | master | SRP6 zero-knowledge en remplacement du hash actuel. **Optionnel.** |

## Modules non ticketés (5)

Ces modules n'ont pas généré de ticket car déjà couverts ou très
spécifiques :

| Module | Source server-core | Pourquoi pas de ticket |
|---|---|---|
| Addons | game/Addons | Pattern « version handshake » déjà couvert par `kProtocolVersion`. À reconsidérer si addons UI client deviennent scriptables. |
| Auth (déjà couvert) | shared/Auth | Voir SERVER-CORE.45 (optional). Le hash actuel suffit. |
| Config | shared/Config | Stack JSON+CLI hiérarchique déjà strictement plus expressive que INI server-core. |
| Log | shared/Log | Déjà couvert par PR #468 (filtres, fichiers spécialisés, GM per-account, packet dump, couleurs). |
| Network | shared/Network | NetServer epoll TCP custom déjà mieux adapté aux opcodes binaires Linux que Boost.Asio server-core. |
| VoiceChat | game/VoiceChat | Stub TBC vide — à intégrer via service externe (LiveKit, Mumble) plutôt que master/shard. |

## Ordre d'implémentation suggéré

L'ordre suit les **dépendances** déclarées dans chaque ticket. Voici le
chemin minimal pour aller du squelette aux features visibles joueur :

```
01 Chat (déblocant MVP, bypass dépendances strictes)
06 Accounts (pré-requis Chat dispatch)
13 Database (SQLStorage, utilisé partout)
12 Server (PacketLog, OpcodeRegistry)
02 Entities (squelette WorldObject)
03 Grids (AoI base)
04 Movement (splines)
05 vmap (LOS)
19 Maps (« Map = thread »)
20 MotionGenerators (mouvement IA)
07 AI (premier PNJ scripté)
11 Combat (premier PNJ hostile)
22 Pools (spawns)
17 Loot (drops)
16 Globals (Conditions)
14 DBScripts (contenu narratif)
26 Spells (gameplay magique)
23 Quests (contenu narratif joueur)
24 Reputation (factions)
28 World (DSL conditions)
... (puis le reste selon priorités du moment)
```

## Convention de DoD

Tous les tickets héritent de `tickets/DEFINITION_OF_DONE.md` :

- Build OK (configure + build) via presets
- Aucun dossier racine non autorisé
- Aucun chemin absolu hardcodé
- Tout chemin de contenu via `paths.content`
- Tests + smoke test
- Migrations DB idempotentes (où applicable)
- Rapport final : fichiers modifiés + commandes + résultats

## Mises à jour

Quand un ticket est commencé / terminé / annulé, mettre à jour cet
INDEX avec un statut visible (✅ ⌛ ❌) en début de ligne.

---

*Index généré le 2026-05-07 — basé sur `SERVER-CORE_ANALYSIS.md`.*
