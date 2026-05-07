# CMANGOS.13_Database_sqlstorage_async_prepared

## Objectif

Étendre la couche d'accès DB LCDLLN avec 3 patterns cmangos
`src/shared/Database` :

1. **`SQLStorage` / `SQLStorageImpl`** : cache RAM typé read-only chargé
   une fois au boot depuis une table SQL, indexé par PK, accédé en O(1)
   sans relock. Idéal pour tables statiques (loot, items, spells,
   creature_template).
2. **`SqlDelayThread`** : worker dédié qui consomme une file
   d'opérations SQL asynchrones (callback + futures). Le caller reçoit
   un `QueryResultFuture` non-bloquant.
3. **`SqlPreparedStatement`** : abstraction binding-typed avec cache
   des statements préparés par connexion. Réduit le parsing SQL côté
   serveur MySQL.

C'est un **P2 cross master+shard** — patterns réutilisables côté master
(accounts, characters) et côté shard (creature/spell/loot templates).

## Dépendances

- M00.1 (build base)
- `engine/server/db/ConnectionPool` (déjà existant, MySQL)
- `engine/server/db/DbHelpers` (déjà existant)

## Livrables

### Côté shared (`engine/server/db/`)

- `SQLStorage.{h,cpp}` (templated) — cache typé d'une table read-only :
  ```cpp
  template <typename T>
  class SQLStorage {
  public:
    void Load(ConnectionPool& pool, std::string_view tableName, std::string_view pkColumn);
    T const* Find(uint32_t pk) const;
    size_t Size() const;
    auto begin() const, end() const;  // itération
  };
  ```
  - Charge une fois au boot (1 SELECT *).
  - Stocke en `std::unordered_map<uint32_t, T>`.
  - Accès O(1) sans lock (read-only post-load).
  - Hot-reload optionnel via commande GM `.reload <storage_name>`.
- `SQLStorageDecl.h` — macro/templates aidant à déclarer le mapping
  table → struct. Ex : `DECLARE_SQL_FIELD(creature_template, level, INT)`.
- `SqlDelayThread.{h,cpp}` — worker async :
  ```cpp
  class SqlDelayThread {
  public:
    void Start();
    void Stop();
    QueryResultFuture EnqueueQuery(std::string sql);
    void EnqueueExecute(std::string sql, std::function<void(bool ok)> callback);
  };
  ```
- `SqlPreparedStatement.{h,cpp}` — wrapper de `MYSQL_STMT` avec API
  binding typed (`statement.Bind(0, accountId).Bind(1, "login").Execute()`).

### Tests

- `SQLStorageTests.cpp` — load + find ; ré-load (cache invalidé).
- `SqlDelayThreadTests.cpp` — enqueue 100 queries → toutes complètent.
- `SqlPreparedStatementTests.cpp` — bind 5 types (int, string, blob, float, datetime) → exec.

### Configuration (`config.json`)

```json
"db": {
  "delay_thread_enabled": true,
  "delay_thread_queue_size": 1024,
  "prepared_statement_cache_size_per_conn": 64,
  "sql_storage_log_load_durations": true
}
```

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. SQLStorage usage type

```cpp
struct CreatureTemplate {
  uint32_t entry;
  std::string name;
  int32_t level;
  uint32_t faction;
  // ...
};

SQLStorage<CreatureTemplate> g_creatureTemplates;

// au boot du shard :
g_creatureTemplates.Load(dbPool, "creature_template", "entry");

// au runtime :
if (auto* tpl = g_creatureTemplates.Find(creature.GetEntry())) {
  // utiliser tpl->level, tpl->name, ...
}
```

Pour des centaines de milliers d'entrées (ex. `item_template`), le coût
RAM est de l'ordre de 100-200 MB. Acceptable pour un shard.

### 2. SqlDelayThread

Pour les **écritures** non critiques (ex. logs d'activité, statistiques)
qui peuvent être différées 100ms sans impact gameplay.

```cpp
g_dbDelay.EnqueueExecute(
  "INSERT INTO player_login_log (...) VALUES (...)",
  [](bool ok) { if (!ok) LOG_ERROR(Db, "login log failed"); }
);
```

**Pas pour** les écritures critiques (auth, achat HV) — synchrone reste.

### 3. SqlPreparedStatement

Cache par connexion : la 1ʳᵉ exécution prépare, les suivantes
réutilisent le statement compilé côté MySQL.

```cpp
auto stmt = conn.Prepare("SELECT * FROM accounts WHERE id = ?");
stmt.Bind(0, accountId);
auto result = stmt.Execute();
```

Gain : 30-50% de débit DB sur les requêtes répétitives.

## Étapes d'implémentation

1. **Implémenter `SQLStorage`** templated header-only.
2. **Câbler chargement au boot** dans `main_shard_linux.cpp` pour 1-2 tables initiales (creature_template, item_template).
3. **Implémenter `SqlDelayThread`** + tests.
4. **Implémenter `SqlPreparedStatement`** + cache.
5. **Migration progressive** : remplacer les `SELECT * FROM creature_template WHERE entry = ?` ad hoc par `g_creatureTemplates.Find(entry)`.
6. Tests : 3 fichiers listés.
7. Doc : section « DB layer » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard)
- [ ] Tests passent (3 fichiers)
- [ ] Smoke test : 100k entries dans `item_template` chargées en < 5s
- [ ] `SqlDelayThread` traite 1000 queries sans bloquer le tick
- [ ] PreparedStatement réutilisé : check via log SQL count (devrait diminuer)
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **SQLStorage = read-only après load** : si une commande GM modifie une row, il faut **ré-load** complet. Ne pas tenter de patcher en RAM (incohérence garantie).
- **DelayThread et shutdown** : à l'arrêt, drainer la file (exécuter le restant) avant de fermer les connexions. Sinon perte d'écritures.
- **Connection per thread** : le SqlDelayThread tient sa propre connexion (séparée du pool). Pas de partage entre threads, pas de mutex.
- **PreparedStatement et schéma** : si une migration ajoute une colonne, les statements préparés peuvent devenir invalides. Recréer le cache au reload schema.
- **MySQL specific** : ces patterns assument MySQL. Si on veut PostgreSQL plus tard, rewrap. Cmangos a une couche multi-backend, on **ignore** par simplicité.
- **Mémoire** : 100-200 MB de cache RAM est raisonnable mais pas gratuit. Pour des tables énormes (loot avec 10M rows), envisager un cache LRU au lieu de tout charger.

## Références

- `CMANGOS_ANALYSIS.md` § Database (P2 cross)
- cmangos `src/shared/Database/SQLStorage.h`, `SqlDelayThread.cpp`,
  `SqlPreparedStatement.cpp`
