# CMANGOS.20 — MotionGenerators (stack / navmesh / waypoints)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.20_MotionGenerators_stack_navmesh_waypoints.md](../../../../tickets/CMANGOS/CMANGOS.20_MotionGenerators_stack_navmesh_waypoints.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** (pour le `MotionMaster` stack + générateurs).
🟡 Navmesh infrastructure partielle (M10.5/M11.3 navmesh per chunk +
ChunkPackageLayout mentionne navmesh).

## 2. Preuves dans le code

**Existant (navmesh infrastructure, pas IA mouvement) :**
- [engine/world/ChunkPackageLayout.h](../../../../engine/world/ChunkPackageLayout.h) — référence à navmesh dans le
  packaging chunk
- M10.5 Chunk packaging geo+tex+instances+nav split
- M11.3 Navmesh bake per chunk + portals
- M14.2 Aggro AI states (Idle/Patrol/Aggro/Return) — structure proche
  d'une state machine, **différente** du stack of MovementGenerators
  cmangos

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/motion/` — dossier inexistant
- ❌ `MovementGenerator` interface abstraite + 7 sous-classes
  (Idle/Random/Waypoint/Chase/Follow/Point/Flee)
- ❌ `MotionMaster` stack (push/pop/Clear avec cleanup différé)
- ❌ `MoveMap` wrapper Detour côté serveur (FindPath)
- ❌ `WaypointManager` + table `creature_movement_template`
- ❌ Outil offline `tools/mmap_extractor/` (bake `.mmap` Recast côté
  serveur, distinct du navmesh client/world existant)
- ❌ Migration DB

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — M10.5 et M11.3 livrent l'infrastructure
navmesh (bake per chunk, packaging). Mais c'est probablement orienté
**rendu/world** (côté client, tile streaming), pas serveur shard.

M14.2 propose une approche state machine "Idle/Patrol/Aggro/Return"
qui est une alternative au stack of MovementGenerators (plus simple,
moins flexible).

## 4. Écart par rapport à la spec CMANGOS

L'écart est **architectural** :

1. **MotionMaster stack** — pattern élégant : push Chase pendant combat,
   pop à la fin → Random idle reprend tout seul. Pas de state machine
   explicite à maintenir.
2. **Cleanup différé** — bug subtil prévenu (Clear() pendant Update()
   = mark for cleanup, pas delete immédiat).
3. **Navmesh côté serveur** — pour pathfinding IA shard. Distinct du
   navmesh client (collision/rendu).
4. **WaypointManager DB-driven** — patrouilles décrites en SQL, pas C++.

Sans ces patterns, l'IA de mouvement reste primitive (move-to-target
direct, pas de pathfinding crédible autour des obstacles).

## 5. Effort estimé

**L** (1 sprint) :
- `MovementGenerator` interface + 7 sous-classes basiques
- `MotionMaster` stack avec cleanup différé + tests
- `MoveMap` wrapper Detour (lib externe Recast/Detour ?)
- `WaypointManager` + migration DB
- Outil offline `mmap_extractor` (bake `.mmap`)
- Tests : Chase puis Pop revient en Random ; cleanup pendant Update OK

⚠️ **Lib externe Detour/Recast** = nouvelle dépendance, **interdit**
sans accord explicite (AGENTS.md). Alternatives : (a) demander accord,
(b) implémenter pathfinding A* maison sur grille (moins efficace mais
sans dep), (c) réutiliser le navmesh client si exposable côté serveur.

## 6. Valeur joueur/serveur

**Élevée** — déblocant pour IA mouvement crédible. Sans pathfinding,
les mobs sortent des murs ou restent bloqués dans les coins.

Critique pour CMANGOS.07 AI (l'AI pousse/pop sur MotionMaster) et
CMANGOS.11 Combat (Chase pendant aggro).

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — `Unit` porte un `MotionMaster`
- **CMANGOS.04 Movement** — `MoveSplineInit` est l'API de sortie des
  générateurs (chaque MovementGenerator pousse une `MoveSplineInit` qui
  devient une spline)
- **CMANGOS.07 AI** — l'AI consomme/pousse sur MotionMaster
- **CMANGOS.13 Maps** — Map charge les navmeshes des tiles actifs

→ **CMANGOS.20 dépend de la chaîne P1 + .07/.13**.

## 8. Risque / piège ⚠️

- ⚠️ **Lib externe Detour** — interdit sans accord (AGENTS.md). À
  demander explicitement OU implémenter pathfinding maison.
- ⚠️ **Migration DB** — table `creature_movement_template` (waypoints).
  Idempotent.
- ⚠️ **Cleanup différé bug** — un MovementGenerator marqué "to delete"
  pendant Update doit l'être au prochain tick, pas tout de suite. Sinon
  use-after-free.
- ⚠️ **Coordination avec navmesh client** — si bake `.mmap` (serveur) ≠
  bake navmesh client → divergence (mob va à un endroit où le client
  voit un mur). Format unifié ou versioning sync.
- ⚠️ **Tile streaming** — chargement/déchargement des `.mmap` par
  GridState (CMANGOS.03 manquant). Sans state machine, chargement
  global au boot (acceptable v1).
- ⚠️ **Stack overflow MotionMaster** — push push push sans pop = leak.
  Limite max stack (10 ? 20 ?).
- ⚠️ **Performance pathfinding** — A* sur navmesh, ok jusqu'à 100s mobs.
  Au-delà, pool worker async ou cache de chemins.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** la chaîne P1 + CMANGOS.07 AI :

1. **Étape 0 (décision)** : statuer la lib pathfinding.
   - Demander accord pour Detour/Recast (recommandé : éprouvé,
     industriel)
   - OU implémenter A* maison sur grille (acceptable v1, suffit pour
     mobs sur terrain plat).
2. **Étape 1** : `MovementGenerator` + `MotionMaster` stack +
   2-3 sous-classes (Idle, Random, Point) + tests.
3. **Étape 2** : `WaypointManager` + migration DB (waypoint patrouilles).
4. **Étape 3** : intégration Detour/A* + `MoveMap` + outil bake offline.
5. **Étape 4** : sous-classes Chase/Follow/Flee.
6. **Étape 5** : intégration `Map::Tick` qui appelle
   `MotionMaster::Update`.

À planifier après CMANGOS.04 Movement (splines) et **avec** CMANGOS.07
AI (interaction étroite : AI pousse, MotionMaster exécute).

---

*Audit du 2026-05-08. Mises à jour : —*
