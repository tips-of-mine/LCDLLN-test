# STAB.10 — ShardRegistry : implémenter la transition vers l'état `Degraded`

**Priorité :** Moyenne  
**Périmètre :** `engine/server/ShardRegistry.h` · `engine/server/ShardRegistry.cpp` · `engine/server/ShardRegisterHandler.cpp`  
**Dépendances :** M22.3 livré (heartbeat + EvictStaleHeartbeats)

---

## Objectif

L'état `Degraded` est défini dans l'enum `ShardState` mais n'est jamais atteint : aucune transition ne le déclenche. Ce ticket implémente la logique qui amène un shard à l'état `Degraded` (charge excessive ou heartbeats manqués partiels), et les transitions de retour vers `Online` ou vers `Offline`.

---

## Contexte

La state machine actuelle est :
```
Registering → Online → Offline
```

La state machine spécifiée (M22.1) est :
```
Registering → Online → Degraded → Offline
                ↑___________|
```

L'état `Degraded` signifie que le shard est encore joignable mais doit être dé-priorisé pour de nouveaux clients (charge trop haute ou heartbeats irréguliers). Il est actuellement un état mort — `SelectShard` l'exclut déjà (`state != ShardState::Online`) mais rien ne l'y amène jamais.

---

## Spécification des transitions

| Transition | Déclencheur | Acteur |
|---|---|---|
| `Online → Degraded` | `current_load / max_capacity >= degraded_load_threshold` (défaut : **0.90**, configurable) | `UpdateHeartbeat` |
| `Degraded → Online` | `current_load / max_capacity < degraded_load_threshold` lors d'un heartbeat ultérieur | `UpdateHeartbeat` |
| `Degraded → Offline` | Timeout heartbeat (même règle que `Online → Offline` dans `EvictStaleHeartbeats`) | `EvictStaleHeartbeats` |

---

## Changements requis

### `engine/server/ShardRegistry.h`

- Ajouter le paramètre `degraded_load_threshold` dans le registre :
  ```cpp
  /// Load ratio (current_load / max_capacity) above which a shard transitions to Degraded. Default 0.90.
  void SetDegradedLoadThreshold(double threshold);
  ```
- Ajouter un membre privé `double m_degraded_load_threshold = 0.90;`
- Ajouter une callback optionnelle `m_shard_degraded_callback` (même pattern que `m_shard_down_callback`) :
  ```cpp
  void SetShardDegradedCallback(std::function<void(uint32_t shard_id)> cb);
  ```

### `engine/server/ShardRegistry.cpp`

**Dans `UpdateHeartbeat`**, après la mise à jour de `current_load` et `last_heartbeat` :

```cpp
// Transition Online → Degraded ou Degraded → Online selon le load ratio
if (it->second.state == ShardState::Online || it->second.state == ShardState::Degraded)
{
    uint32_t cap = (it->second.max_capacity > 0u) ? it->second.max_capacity : 1u;
    double ratio = static_cast<double>(it->second.current_load) / static_cast<double>(cap);
    if (ratio >= m_degraded_load_threshold && it->second.state == ShardState::Online)
    {
        it->second.state = ShardState::Degraded;
        // émettre LOG + callback (hors lock)
    }
    else if (ratio < m_degraded_load_threshold && it->second.state == ShardState::Degraded)
    {
        it->second.state = ShardState::Online;
        // émettre LOG
    }
}
```

**Logs obligatoires :**
```cpp
LOG_INFO(Core, "[ShardRegistry] Shard {} now degraded (load={}, cap={}, ratio={:.2f})", shard_id, current_load, max_capacity, ratio);
LOG_INFO(Core, "[ShardRegistry] Shard {} recovered to online (load={}, cap={}, ratio={:.2f})", shard_id, current_load, max_capacity, ratio);
```

**La callback `m_shard_degraded_callback`** doit être invoquée hors du lock (même pattern que `EvictStaleHeartbeats` pour `m_shard_down_callback`).

**Dans `EvictStaleHeartbeats`** : aucun changement — `Degraded` est déjà non-`Offline` donc il sera correctement transitionné vers `Offline` par le timeout.

### `engine/server/main_server_linux.cpp`

- Lire la config `shard.degraded_load_threshold` (défaut `0.90`) et appeler `shardRegistry.SetDegradedLoadThreshold(...)` au démarrage
- Enregistrer une callback shard degraded pour le log serveur :
  ```cpp
  shardRegistry.SetShardDegradedCallback([](uint32_t shard_id) {
      LOG_INFO(Net, "[ServerMain] Shard degraded event: shard_id={}", shard_id);
  });
  ```

### `engine/server/ShardDownDetectionTests.cpp` (ou nouveau fichier de test)

Ajouter un test unitaire couvrant :
1. Shard `Online` → load > threshold → `Degraded` après `UpdateHeartbeat`
2. Shard `Degraded` → load < threshold → `Online` après `UpdateHeartbeat`
3. Shard `Degraded` → timeout heartbeat → `Offline` via `EvictStaleHeartbeats`
4. `SelectShard` ne retourne pas un shard `Degraded`

---

## Critères d'acceptation

- [ ] La transition `Online → Degraded` est déclenchée quand `load/cap >= threshold`
- [ ] La transition `Degraded → Online` est déclenchée quand `load/cap < threshold` lors d'un heartbeat ultérieur
- [ ] La transition `Degraded → Offline` est déclenchée par timeout heartbeat (même logique qu'`Online`)
- [ ] `SelectShard` n'inclut toujours pas les shards `Degraded`
- [ ] Les logs `now degraded` et `recovered to online` apparaissent au bon moment
- [ ] La callback `SetShardDegradedCallback` est invoquée hors du lock
- [ ] Le seuil est configurable via `shard.degraded_load_threshold` dans `config.json`
- [ ] Les 4 cas de test unitaire passent via `ctest`
- [ ] Aucune régression sur M22.1 / M22.2 / M22.3

---

## Interdit

- Ne pas modifier la logique de `SelectShard` au-delà de l'exclusion déjà en place
- Ne pas modifier les shaders ni le rendu
- Ne pas changer le type de mutex existant
- Ne pas modifier `AGENTS.md` ni `DEFINITION_OF_DONE.md`
