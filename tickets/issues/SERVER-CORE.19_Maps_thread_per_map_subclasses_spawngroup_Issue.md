# Issue: SERVER-CORE.19

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/shardd/maps/Map.h
- src/shardd/maps/InstanceManager.h

## Note
Maps subclasses + InstanceManager

---

## Contenu du ticket (SERVER-CORE.19)

# SERVER-CORE.19_Maps_thread_per_map_subclasses_spawngroup

## Objectif

Mettre en place le **squelette de la simulation par instance** côté
shard LCDLLN, inspiré de `src/game/Maps` server-core. Quatre piliers :

1. **« Map = un thread logique »** : tout le gameplay d'une instance
   tourne mono-thread, supprimant 90% des locks. Le parallélisme se fait
   **entre maps** (`MapUpdater` / pool de workers), pas dans une map.
2. **Sous-classes spécialisées** (`DungeonMap`, `BattleGroundMap`,
   `WorldMap`) avec policies différentes (lifecycle, persistance, joueur
   cap) — éviter un `Map` monolithique avec 50 flags.
3. **`MapPersistentState`** : les instances sauvegardées (raids loot
   lockés) ont un état DB séparé du runtime — recharge un raid après
   reboot avec progression intacte.
4. **`SpawnGroup`** : groupes de spawns coordonnés (« patrouille de 3 »,
   « pop l'un OU l'autre ») data-driven, pas scripté.
5. **`ObjectPosSelector`** : trouve une position libre autour d'un point
   sans collision — utile pour summons, loot piles, téléports de groupe.

C'est un **P2 shard**, fondation de la simulation à plusieurs instances.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — Map contient des `WorldObject`
- SERVER-CORE.03 (Grids) — chaque Map a sa GridMap
- SERVER-CORE.05 (vmap) — collisions par Map
- SERVER-CORE.07 (AI), SERVER-CORE.11 (Combat), SERVER-CORE.20 (MotionGenerators) — tickés par Map

## Livrables

### Côté shard (`engine/server/shard/maps/`)

- `Map.{h,cpp}` — base abstraite. Méthodes `Update(int32 dtMs)`, `Add(WorldObject*)`, `Remove(WorldObject*)`, `Tick()` qui orchestre dans l'ordre : grids → AI → motion → combat → snapshot.
- `WorldMap.{h,cpp}` — sous-classe pour le monde ouvert. Une seule instance par mapId, lifetime infini.
- `DungeonMap.{h,cpp}` — sous-classe pour donjons 5-man. Lifetime borné par présence joueurs ; persistance optionnelle si binding raid.
- `BattleGroundMap.{h,cpp}` — sous-classe pour BG/colisée (SERVER-CORE.10). Lifetime = match.
- `MapManager.{h,cpp}` — singleton, gère la création/destruction des Map instances. API `GetOrCreate(mapId, instanceId)`.
- `MapUpdater.{h,cpp}` — pool de threads workers qui ticke les maps en parallèle. Une map = un thread à un instant donné, mais différentes maps en parallèle.
- `MapPersistentState.{h,cpp}` — état DB par instance (loot locks, boss kills, timers).
- `SpawnGroup.{h,cpp}` — descripteur de spawn coordonné chargé depuis DB.
- `ObjectPosSelector.{h,cpp}` — utilitaire de placement.

### Migration DB

```sql
CREATE TABLE map_persistent_state (
  instance_id     INT UNSIGNED NOT NULL,
  map_id          INT UNSIGNED NOT NULL,
  reset_time      BIGINT NOT NULL DEFAULT 0,
  bosses_killed   INT UNSIGNED NOT NULL DEFAULT 0,
  data_blob       MEDIUMBLOB,
  PRIMARY KEY (instance_id)
);

CREATE TABLE spawn_group_template (
  group_id      INT UNSIGNED NOT NULL,
  name          VARCHAR(64) NOT NULL,
  type          TINYINT UNSIGNED NOT NULL,    -- creature=0, gameobject=1
  max_active    INT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (group_id)
);

CREATE TABLE spawn_group_member (
  group_id      INT UNSIGNED NOT NULL,
  member_entry  INT UNSIGNED NOT NULL,         -- creature_template.entry
  chance        TINYINT UNSIGNED NOT NULL DEFAULT 100,
  PRIMARY KEY (group_id, member_entry)
);
```

### Configuration (`config.json`)

```json
"map": {
  "updater_thread_count": 4,
  "tick_target_ms": 100,
  "world_map_idle_grace_sec": 600,
  "dungeon_map_empty_unload_sec": 300
}
```

### Tests

- `MapManagerTests.cpp` — get/create/destroy ; ne pas créer 2 instances pour le même `mapId+instanceId`.
- `SpawnGroupTests.cpp` — `max_active=2` avec 5 candidats : exactement 2 spawnés en simultané.
- `ObjectPosSelectorTests.cpp` — autour d'un point avec 8 entités proches, trouve la 9ᵉ position libre.
- `MapPersistentStateTests.cpp` — round-trip save/load DB.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. « Map = un thread logique »

Au lieu de protéger chaque WorldObject avec des mutex, on garantit que :
- Une Map à un instant donné est tickée par **exactement un thread**.
- Tout accès aux WorldObject d'une Map doit se faire **depuis** ce
  thread.
- Communication inter-Map (téléport joueur, mail livré, broadcast world)
  via `Messager` (SERVER-CORE.36, P3) — file de fonctions cross-thread.

### 2. Sous-classes Map

```
Map (abstract)
├── WorldMap         // mapId fixe, instanceId=0, infinite lifetime
├── DungeonMap       // mapId fixe, instanceId unique, bound or not
└── BattleGroundMap  // mapId fixe, instanceId per-match, ephemeral
```

Override des hooks :
- `OnPlayerEnter`, `OnPlayerLeave`
- `OnBossKilled` (DungeonMap : sauvegarde dans `MapPersistentState`)
- `OnEnd` (BG : reset, distribution récompenses)

### 3. SpawnGroup

```cpp
struct SpawnGroup {
  uint32_t groupId;
  uint32_t maxActive;
  std::vector<SpawnGroupMember> members;  // entry + chance
};

// au respawn d'un membre, le mgr décide si on respawne
// (selon maxActive et chances)
```

Évite de coder « 3 patrouilleurs spawnent ensemble + un seul rare » en C++ ; tout en data.

### 4. ObjectPosSelector

```cpp
class ObjectPosSelector {
public:
  ObjectPosSelector(Map& map, Vector3 center, float searchRadius);
  std::optional<Vector3> FindFreePosition(float minDistFromOthers);
};
```

Utilisé par : summon (placer le minion à côté du caster), loot drop (placer la pile pas au milieu d'un PNJ vivant), tp de groupe (chacun à sa position autour du leader).

## Étapes d'implémentation

1. Créer `engine/server/shard/maps/`.
2. Implémenter `Map` abstract + `WorldMap` minimal (juste add/remove, pas de tick gameplay).
3. Implémenter `MapManager` + `MapUpdater` (thread pool).
4. Câbler le tick : Grids → AI → MotionGenerators → Combat → SnapshotBuilder (SERVER-CORE.02 §SnapshotBuilder).
5. Implémenter `DungeonMap` + `BattleGroundMap` stubs (overrides).
6. Implémenter `MapPersistentState` + migration `map_persistent_state`.
7. Implémenter `SpawnGroup` + migrations `spawn_group_*`.
8. Implémenter `ObjectPosSelector`.
9. Tests : 4 fichiers listés.
10. Doc : section « Maps shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests `Map*Tests`, `SpawnGroupTests`, `ObjectPosSelectorTests` passent
- [ ] Smoke test : 4 maps en parallèle (4 threads), chacune avec 100 entités, tick stable à 10 Hz pendant 60 s sans drift
- [ ] DungeonMap : binding raid persiste après reboot (loot lock OK)
- [ ] SpawnGroup : `max_active=2` respecté avec 5 candidats
- [ ] Migrations appliquées et idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Pas de mutex sur WorldObject** : le modèle « Map = thread » repose là-dessus. Si quelqu'un ajoute `std::mutex m_pos;` sur un Unit "par sécurité", ça réintroduit la contention. Code review strict.
- **Tick stabilité** : viser `tick_target_ms` (100ms = 10 Hz) en moyenne, jamais en hard real-time. Si une map mange 200ms ponctuellement, ne pas paniquer ; logger un warning si > 2× target.
- **Téléport inter-map** : ne **jamais** déplacer un WorldObject d'une Map à une autre directement. Stratégie : `Remove(obj)` de la map source → push une fonction sur le `Messager` de la map cible qui fait `Add(reconstructed obj)`. Sinon le worker source touche aux données du worker cible = race.
- **MapPersistentState save** : ne pas sauvegarder à chaque tick. Triggers : boss killed, loot looted, joueur leave. Sinon I/O DB sature.
- **DungeonMap timeouts** : un donjon vide se décharge après `dungeon_map_empty_unload_sec`. Si un joueur revient, recréer la map (avec restauration de l'état persistent si binding) — coût acceptable sur entrée.
- **BattleGroundMap fin de match** : cleanup explicite (remove all players, despawn all). Ne **pas** s'appuyer sur le timeout d'idle, sinon des matches "fantômes" avec un score figé persistent.
- **Tick order matters** : Grids (déplacements en cellules) avant AI (qui consulte les voisins) avant MotionGenerators (qui pousse les splines) avant Combat (qui résout les dégâts) avant SnapshotBuilder (qui sérialise l'état final). Inverser cet ordre = comportements bizarres (un PNJ aggro un joueur déjà bougé, etc.).

## Références

- `SERVER-CORE_ANALYSIS.md` § Maps (P2 shard)
- server-core `src/game/Maps/Map.cpp`, `MapManager.cpp`, `MapUpdater.cpp`,
  `DungeonMap.cpp`, `BattleGroundMap.cpp`, `MapPersistentStateMgr.cpp`,
  `SpawnGroup.cpp`, `ObjectPosSelector.cpp`
