# Issue: SERVER-CORE.31

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/masterd/events/GameEventManager.h
- src/shared/network/GameEventPayloads.h

## Note
GameEvents scheduler/cascade

---

## Contenu du ticket (SERVER-CORE.31)

# SERVER-CORE.31_GameEvents_seasonal_scheduler_cascading

## Objectif

Mettre en place un **gestionnaire d'events de monde planifiés** côté
shard LCDLLN, inspiré de `src/game/GameEvents` server-core. Trois piliers :

1. **Activation par `event_id`** sur les spawns : un même monde DB
   porte plusieurs variantes saisonnières filtrées au load time, sans
   dupliquer la table.
2. **Scheduler central** avec start/end timestamps et récurrence —
   un seul tick global décide ce qui est actif, pas de polling
   distribué.
3. **Events en cascade** : un event peut en déclencher d'autres
   (parent/child), utile pour les festivals à étapes.

C'est un **P3 shard**.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database)
- SERVER-CORE.19 (Maps — events filtrent les spawns par event_id)

## Livrables

### Côté shard (`engine/server/shard/events/`)

- `GameEvent.h` :
  ```cpp
  struct GameEvent {
    uint32_t eventId;
    std::string name;
    std::optional<int64_t> startTs;        // null = non planifié
    std::optional<int64_t> endTs;
    int64_t   recurrenceSec;                // 0 = non récurrent
    std::vector<uint32_t> childEvents;      // events à déclencher en cascade
    bool      active = false;
  };
  ```
- `GameEventMgr.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge events.
  - `Update(int64_t nowMs)` — tick : check start/end, cascade child events.
  - `void StartEvent(uint32_t eventId)` — manuel (commande GM).
  - `void StopEvent(uint32_t eventId)`.
- Hook spawn : `Map::ShouldSpawnByEvent(spawn.eventId)` — un spawn n'est
  matérialisé que si son event est actif (ou eventId=0 = always-on).

### Migration DB

```sql
CREATE TABLE game_event (
  event_id        INT UNSIGNED NOT NULL,
  name            VARCHAR(64) NOT NULL,
  start_ts        BIGINT,                   -- nullable
  end_ts          BIGINT,
  recurrence_sec  BIGINT NOT NULL DEFAULT 0,
  description     VARCHAR(255),
  PRIMARY KEY (event_id)
);

CREATE TABLE game_event_cascade (
  parent_event_id INT UNSIGNED NOT NULL,
  child_event_id  INT UNSIGNED NOT NULL,
  delay_sec       INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (parent_event_id, child_event_id)
);

ALTER TABLE creature_spawn  ADD COLUMN event_id INT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE gameobject_spawn ADD COLUMN event_id INT UNSIGNED NOT NULL DEFAULT 0;
```

### Configuration (`config.json`)

```json
"game_events": {
  "enabled": true,
  "tick_interval_sec": 60,
  "log_events_state_changes": true
}
```

### Tests

- `GameEventMgrTests.cpp` — start/stop manuel, cascade child events.
- `GameEventScheduleTests.cpp` — start_ts dépassé → event activé automatiquement.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Étapes d'implémentation

1. Créer `engine/server/shard/events/`.
2. Migrations DB (3 tables + 2 colonnes).
3. Implémenter `GameEventMgr::Load`.
4. Implémenter `Update` avec scheduler.
5. Implémenter cascade child events.
6. Câbler `Map::ShouldSpawnByEvent` au load des spawns.
7. Implémenter commandes GM `.event start <id>` / `.event stop <id>`.
8. Tests : 2 fichiers.
9. Doc : section « Game events » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : event "Halloween" avec start_ts et end_ts → activation/désactivation auto + spawns event_id=halloween apparaissent
- [ ] Cascade : event A start → child B activé après delay
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Recurrence overlap** : si recurrence_sec < (endTs - startTs), event n'est jamais désactivé. Détecter et log warning au load.
- **Spawns en transition** : à l'activation d'un event, spawn les nouveaux PNJ ; à la désactivation, despawn proprement (pas snap).
- **Cascade cycles** : A → B → A. Détecter et reject au load.
- **Mass-spawn** : activation d'un gros event (1000 PNJ saisonniers) peut spike CPU. Étaler sur 5-10 secondes au lieu de tout spawner d'un coup.
- **Persistance state** : si shard reboot pendant un event, le state doit être recalculé au boot (`now > startTs && now < endTs` → event actif). Pas de stockage `is_active` séparé en DB.

## Références

- `SERVER-CORE_ANALYSIS.md` § GameEvents (P3 shard)
- server-core `src/game/GameEvents/GameEventMgr.cpp`, `moon.cpp`
