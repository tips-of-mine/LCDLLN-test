# CMANGOS.20_MotionGenerators_stack_navmesh_waypoints

## Objectif

Mettre en place la **pile de générateurs de mouvement** côté shard
LCDLLN, inspirée de `src/game/MotionGenerators` cmangos. Cinq piliers :

1. **`MotionMaster = std::stack<MovementGenerator*>`** : on push un
   Chase pendant un combat, on pop quand il finit, le Random idle
   reprend automatiquement. État comportemental sans state machine
   explicite.
2. **Cleanup différé via flags** : si on `Clear()` pendant un `Update()`,
   on marque pour cleanup au prochain tick — évite use-after-free.
3. **MoveMap (navmeshes Detour)** chargés par tile à la demande, partagés
   entre toutes les créatures de la map.
4. **`MoveSplineInit` builder pattern** (déjà couvert par CMANGOS.04
   Movement) — l'IA produit un MoveSplineInit, le générateur le Launch.
5. **`WaypointManager` + DB** : patrouilles décrites en data
   (`creature_movement_template`), pas en C++.

C'est un **P2 shard**, le cœur de l'IA de mouvement.

## Dépendances

- M00.1 (build base)
- CMANGOS.02 (Entities) — `Unit` porte un `MotionMaster`
- CMANGOS.04 (Movement) — `MoveSplineInit` est l'API de sortie des générateurs
- CMANGOS.07 (AI) — l'IA pousse/pop des générateurs sur le `MotionMaster`
- CMANGOS.13 (Maps) — chaque Map charge les navmeshes des tiles actifs

## Livrables

### Côté shard (`engine/server/shard/motion/`)

- `MovementGenerator.{h,cpp}` — interface abstraite. Méthodes virtuelles :
  - `Initialize(Unit&)`, `Reset(Unit&)`, `Finalize(Unit&)`
  - `Update(Unit&, int32 dtMs) → bool` — retourne `false` quand fini
  - `GetType() → MovementGeneratorType` (enum)
- `IdleMovementGenerator.{h,cpp}` — base, ne fait rien (pour PNJ frozen).
- `RandomMovementGenerator.{h,cpp}` — déplacement aléatoire dans un rayon.
- `WaypointMovementGenerator.{h,cpp}` — patrouille selon
  `creature_movement_template`.
- `ChaseMovementGenerator.{h,cpp}` — poursuit une cible (Unit) à une
  distance min/max.
- `FollowMovementGenerator.{h,cpp}` — suit une cible avec un offset.
- `PointMovementGenerator.{h,cpp}` — atteint un point précis puis fini.
- `FleeMovementGenerator.{h,cpp}` — fuit une cible.
- `MotionMaster.{h,cpp}` — la pile. API :
  - `MoveIdle()`, `MoveRandom(radius)`, `MoveTargetedHome()`, `MoveChase(Unit&, range)`, `MoveFollow(Unit&, dist, angle)`, `MovePoint(id, pos)`, `MoveFleeing(Unit&)`, `MoveWaypoint(pathId)`
  - `Update(Unit&, int32 dtMs)` — appelé par `Map::Tick`
  - `Clear(MovementSlot)` — supprime des générateurs mais reporte si dans Update
- `MoveMap.{h,cpp}` — wrapper Detour (navmesh recast). Charge `.mmap`
  par tile. Requêtes : `FindPath(start, end) → vector<Vector3>`.

### Outil offline (`tools/mmap_extractor/`)

- `tools/mmap_extractor/mmap_extractor.cpp` — bake offline : prend les
  meshes + heightmap (mêmes inputs que `vmap_extractor`, CMANGOS.05) →
  produit des `.mmap` Recast.

### Migration DB

```sql
CREATE TABLE creature_movement_template (
  entry         INT UNSIGNED NOT NULL,
  point         INT UNSIGNED NOT NULL,
  position_x    FLOAT NOT NULL,
  position_y    FLOAT NOT NULL,
  position_z    FLOAT NOT NULL,
  delay_ms      INT UNSIGNED NOT NULL DEFAULT 0,
  script_id     INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (entry, point)
);
```

### Configuration (`config.json`)

```json
"motion": {
  "navmesh_dir": "mmap",
  "navmesh_max_loaded_tiles": 64,
  "random_movement_default_radius_m": 10.0,
  "chase_default_range_m": 5.0,
  "follow_default_distance_m": 3.0
}
```

### Tests

- `MotionMasterTests.cpp` — push Chase puis Clear → cleanup différé OK.
- `WaypointMovementTests.cpp` — patrouille passe par tous les points.
- `ChaseMovementTests.cpp` — créature suit la cible jusqu'à `range`.
- `MoveMapTests.cpp` — pathfinding sur navmesh synthétique.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : navmeshes `.mmap` sous `/game/data/mmap/`
- Outils offline : `/tools/mmap_extractor/`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Stack de générateurs

```cpp
class MotionMaster {
public:
  void MoveChase(Unit& target, float range);  // push ChaseGenerator
  void Clear(MovementSlot slot = MOTION_SLOT_ACTIVE);
  void Update(Unit& owner, int32 dtMs) {
    if (m_inUpdate) { m_pendingClear = true; return; }
    m_inUpdate = true;
    auto& top = m_stack.top();
    if (!top.Update(owner, dtMs)) {
      m_stack.pop();
      // pop expose le générateur en-dessous (Random idle, par ex.)
    }
    m_inUpdate = false;
    if (m_pendingClear) ApplyDelayedClear();
  }
private:
  std::stack<std::unique_ptr<MovementGenerator>> m_stack;
  bool m_inUpdate = false;
  bool m_pendingClear = false;
};
```

### 2. Cleanup différé

Si `Clear()` est appelé pendant un `Update()` (par ex. un `OnDeath`
trigger un Clear), on **diffère** au prochain tick. Sinon on détruit
le générateur en cours d'exécution = use-after-free immédiat.

### 3. MoveMap (Recast/Detour)

```cpp
class MoveMap {
public:
  void LoadTile(MapId mapId, int32 tileX, int32 tileY);
  void UnloadTile(MapId mapId, int32 tileX, int32 tileY);
  std::vector<Vector3> FindPath(MapId mapId, Vector3 start, Vector3 end);
};
```

Charge des `.mmap` (format Recast) à la demande, partage le navmesh
entre toutes les Creature de la map. 1 navmesh par tile, refcounted
comme les `vmap` (CMANGOS.05).

Utilisé par : `WaypointMovementGenerator` (pathfinding entre waypoints
si obstacle), `ChaseMovementGenerator` (path autour des murs).

### 4. WaypointMovementGenerator + DB

```cpp
void Init() {
  m_waypoints = WaypointManager::GetWaypoints(m_owner.GetEntry());
  m_currentIdx = 0;
}
bool Update(Unit& owner, int32 dtMs) {
  if (CurrentReached()) {
    if (m_waypoints[m_currentIdx].delay_ms > 0) Wait();
    if (m_waypoints[m_currentIdx].script_id) RunScript(...);
    m_currentIdx = (m_currentIdx + 1) % m_waypoints.size();
    StartMoveTo(m_waypoints[m_currentIdx].pos);
  }
  return true;  // jamais fini (boucle)
}
```

### 5. Outil `lcdlln_mmap_extractor`

Inputs : meshes + heightmap (mêmes que `vmap_extractor`).

Process :
1. Construire un Recast nav-mesh par tile via lib Recast (vendored ou submodule).
2. Sérialiser au format `.mmap` (header + Detour data).
3. Output : `game/data/mmap/<zone>/tile_<cx>_<cy>.mmap`.

## Étapes d'implémentation

1. Créer `engine/server/shard/motion/`.
2. Implémenter `MovementGenerator` interface + `IdleMovementGenerator`.
3. Implémenter `MotionMaster` (stack + cleanup différé).
4. Implémenter `RandomMovementGenerator`, `PointMovementGenerator`.
5. Implémenter `MoveMap` (vendor Recast/Detour ou submodule).
6. Implémenter `WaypointMovementGenerator` + migration `creature_movement_template`.
7. Implémenter `ChaseMovementGenerator`, `FollowMovementGenerator`, `FleeMovementGenerator`.
8. Créer `tools/mmap_extractor/` (peut être stub initial qui produit un `.mmap` dégénéré "tout est marchable").
9. Tests : 4 fichiers listés.
10. Doc : section « Motion shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) + outil mmap_extractor
- [ ] Tests passent (4 fichiers)
- [ ] Smoke test : 1 Creature avec waypoints (3 points) patrouille en boucle ; cleanup OK quand `MoveChase` push pendant l'`Update`
- [ ] Pathfinding navmesh : Creature contourne un mur synthétique
- [ ] Migration `creature_movement_template` appliquée et idempotente
- [ ] Aucun dossier racine non autorisé (sauf `/tools/mmap_extractor/`)
- [ ] Rapport final

## Notes / pièges à éviter

- **Use-after-free** : le cleanup différé est **non-négociable**. Sans lui, un `OnDeath` qui clear le motion master = crash systématique.
- **Slots** : cmangos a `MOTION_SLOT_IDLE`, `MOTION_SLOT_ACTIVE`, `MOTION_SLOT_CONTROLLED`. Permet de garder un Random idle "en arrière-plan" pendant qu'un Chase est actif. **Démarrer simple** (1 seul slot) ; étendre quand nécessaire.
- **Detour licensing** : Recast/Detour est zlib-licensed (compatible commercial). Vendor en submodule ou copie locale.
- **Navmesh size** : un `.mmap` peut faire 1-10 MB par tile. Avec 64 tiles loaded, ça monte à ~500 MB en RAM. Compresser au boot ou stream agressivement (release_delay_sec dans config).
- **Waypoint script_id** : si une patrouille déclenche un script (`script_id != 0`), le script est exécuté côté shard. Coordonner avec CMANGOS.12 (DBScripts) — `script_id` doit pointer vers une row de `creature_movement_scripts` (ou même table que les autres scripts).
- **Chase et range** : si la cible est plus rapide que le chaser, le chaser ne rattrape jamais — il faut un timeout ou une condition d'évade. Géré côté `CombatManager` (CMANGOS.11).
- **FleeMovementGenerator** : ne pas fuir aveuglément (peut foncer dans un mur). Toujours passer par `MoveMap::FindPath` pour valider la fuite.

## Références

- `CMANGOS_ANALYSIS.md` § MotionGenerators (P2 shard)
- cmangos `src/game/MotionGenerators/` (tout le dossier)
- Recast/Detour : https://github.com/recastnavigation/recastnavigation
