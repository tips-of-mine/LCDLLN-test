# CMANGOS.22_Pools_weighted_spawns_nested

## Objectif

Mettre en place un **système de spawns aléatoires groupés** côté shard
LCDLLN, inspiré de `src/game/Pools` cmangos. Trois piliers :

1. **Spawn pool pondéré** : table `pool_template` (max_active) +
   `pool_creature`/`pool_gameobject` avec `chance`. Permet rareté
   contrôlée (rare spawns) sans logique custom par mob.
2. **Pools imbriqués (`pool_pool`)** : un pool peut contenir d'autres
   pools, créant des hiérarchies « zone → sous-zone → mob ». Élégant
   pour scaling éditorial.
3. **Sélection cryptographiquement non biaisée** : alias method ou
   roulette wheel selon poids — utile dès qu'on a des loot tables ou
   spawns de boss.

C'est un **P2 shard** dès qu'on a 10+ creatures différentes à spawner
dans une zone avec des règles de rareté.

## Dépendances

- M00.1 (build base)
- CMANGOS.02 (Entities) — pool spawne des Creature/GameObject
- CMANGOS.13 (Database — SQLStorage cache)
- CMANGOS.19 (Maps — pool persisté par instance via MapPersistentState)

## Livrables

### Côté shard (`engine/server/shard/pools/`)

- `PoolTemplate.h` — struct row de la table.
- `PoolMember.h` — entry + chance + type.
- `PoolManager.{h,cpp}` :
  - `Load(ConnectionPool&)`
  - `void SpawnPool(MapId, uint32_t poolId)` — sélectionne `max_active`
    membres pondérés et les spawn.
  - `void OnMemberDespawn(uint32_t poolId, uint32_t memberId)` — le
    membre revient au "viable pool" pour le prochain respawn.
- `PoolState.{h,cpp}` — état runtime d'un pool dans une Map :
  membres actuellement actifs, prochain respawn timestamp.
- `WeightedSelector.h` — alias method O(1) sampling :
  ```cpp
  template <typename T>
  class WeightedSelector {
  public:
    void Init(std::vector<std::pair<T, float>> weighted);
    T const& Sample(RandomEngine& rng) const;
  };
  ```

### Migration DB

```sql
CREATE TABLE pool_template (
  pool_id     INT UNSIGNED NOT NULL,
  max_active  INT UNSIGNED NOT NULL DEFAULT 1,
  description VARCHAR(255),
  PRIMARY KEY (pool_id)
);

CREATE TABLE pool_creature (
  pool_id     INT UNSIGNED NOT NULL,
  guid        INT UNSIGNED NOT NULL,           -- creature spawn guid (du table creature)
  chance      FLOAT NOT NULL DEFAULT 100,
  description VARCHAR(255),
  PRIMARY KEY (pool_id, guid)
);

CREATE TABLE pool_gameobject (
  pool_id     INT UNSIGNED NOT NULL,
  guid        INT UNSIGNED NOT NULL,
  chance      FLOAT NOT NULL DEFAULT 100,
  description VARCHAR(255),
  PRIMARY KEY (pool_id, guid)
);

CREATE TABLE pool_pool (
  parent_pool   INT UNSIGNED NOT NULL,
  child_pool    INT UNSIGNED NOT NULL,
  chance        FLOAT NOT NULL DEFAULT 100,
  description   VARCHAR(255),
  PRIMARY KEY (parent_pool, child_pool)
);
```

### Configuration (`config.json`)

```json
"pools": {
  "respawn_check_interval_sec": 30,
  "log_pool_spawns": false
}
```

### Tests

- `WeightedSelectorTests.cpp` — distribution uniforme à 1000 samples avec poids inégaux respecte les ratios (±5%).
- `PoolManagerTests.cpp` — pool max_active=2, 5 candidats pondérés → exactement 2 spawnés ; à mort d'un, respawn d'un autre selon poids.
- `PoolPoolTests.cpp` — pool parent A contient B et C (sous-pools), spawn de A déclenche spawn de B ou C selon chance.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. WeightedSelector (alias method)

L'alias method permet O(1) sampling pour distribution discrète. À la
construction, on précompute deux tables (probability + alias). Au sample,
on tire un index uniforme + un float uniforme, on dispatch selon la
table.

Avantage vs roulette wheel naïve (linéaire en N) : critique quand on
spawn 1000s de pools.

### 2. Pool nesting

Un pool A peut référencer des pools B/C/D dans `pool_pool`. À l'évaluation
de A : on tire selon les poids les éléments de A (creatures + gos +
sous-pools), et pour chaque sous-pool tiré, on récurse.

```
pool_zone_dragoncrest (max_active = 5)
  ├── pool_dragoncrest_north (chance 50%)  // sous-pool
  │     ├── creature_dragoncrest_pup (×3 spots, chance 100%)
  │     └── creature_dragoncrest_alpha (×1 spot, chance 5%)  // rare spawn
  └── pool_dragoncrest_south (chance 50%)
        └── creature_dragoncrest_drone (×4 spots, chance 100%)
```

### 3. Persistance

Pour les pools dans `WorldMap` : volatile (reset au reboot ok).

Pour les pools dans `DungeonMap` (raids permanents) : peut être persisté
via `MapPersistentState` (CMANGOS.19) — sauvegarde quels GUIDs sont
"actifs" pour ne pas reset après reboot.

## Étapes d'implémentation

1. Créer `engine/server/shard/pools/`.
2. Migrations DB (4 tables).
3. Implémenter `WeightedSelector` + tests.
4. Implémenter `PoolManager::Load` (avec SQLStorage cache).
5. Implémenter `PoolManager::SpawnPool` (sélection + spawn via Map).
6. Implémenter `OnMemberDespawn` (respawn timer).
7. Implémenter pool nesting (pool_pool).
8. Tests : 3 fichiers.
9. Doc : section « Pools shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : pool max_active=2 avec 5 candidats → 2 spawnés ; despawn de l'un → 3ᵉ spawne après timer
- [ ] Pool nesting : parent A → 50% B / 50% C, tirage 1000 fois donne ratios ~50/50
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Cycles dans pool_pool** : pool A référence B référence A → boucle infinie. Détecter au load + reject.
- **Chance = 0** : autorisé (membre désactivé temporairement) ; éviter division par zéro dans WeightedSelector.
- **Total chance = 0** : dégénéré (tous désactivés) → ne rien spawner, log warning.
- **Respawn timing** : ne **pas** respawn juste à la mort. Délai (`creature.respawn_time` du template) — sinon farm exploit.
- **Performance** : 10k pools × 30s tick = 333 pool checks/s. OK. Si 1M pools, profiler.
- **Persistance volatile vs persistante** : par défaut **volatile** côté `WorldMap`. La persistance arrive avec CMANGOS.19 (MapPersistentState) pour les raids.

## Références

- `CMANGOS_ANALYSIS.md` § Pools (P2 shard)
- cmangos `src/game/Pools/PoolManager.cpp`
- Alias method : Vose's algorithm
