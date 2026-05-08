# Conventions SQL (raw, no ORM) — M21.6

Règles d’accès à la base en SQL brut : sécurité et cohérence.

## 1. Requêtes préparées pour tout input utilisateur

- **Toute donnée provenant du client (login, email, paramètres de requête, etc.) doit être passée via des prepared statements**, jamais concaténée dans une chaîne SQL.
- Utiliser l’API MySQL (C : `mysql_stmt_*` ; C++ : paramètres bind) pour lier les valeurs aux placeholders (`?`).
- **Interdit** : `"SELECT * FROM accounts WHERE login = '" + userInput + "'"` (risque d’injection).
- **Requis** : `PREPARE` / `EXECUTE` avec paramètres bind, ou équivalent du driver utilisé.

## 2. Transactions explicites pour les opérations multi-table

- Dès qu’une opération métier touche **plusieurs tables** (ex. `accounts` + `sessions`, ou `characters` + `inventory`), utiliser une **transaction explicite** :
  - `DbBeginTransaction(mysql)` ou `ScopedTransaction` ;
  - exécuter les requêtes ;
  - `DbCommit(mysql)` en cas de succès, `DbRollback(mysql)` en cas d’erreur.
- Ne pas laisser plusieurs écritures consécutives sans délimitation de transaction lorsque elles forment une unité logique.

## 3. Pool et concurrence

- Utiliser le **connection pool** (`engine::server::db::ConnectionPool`) pour toutes les requêtes applicatives (pas de connexion dédiée par thread sauf besoin explicite).
- Chaque thread qui a besoin d’une connexion : `Acquire()` → utilisation avec `DbExecute` / `DbQuery` / prepared statements → libération automatique à la destruction du `Guard`.
- Le pool gère le **health check** (ping) et la **reconnexion** à l’acquisition ; pas de retry manuel nécessaire dans le code métier.

## 4. Helpers disponibles

- `DbExecute(mysql, sql)` : exécution sans résultat (INSERT/UPDATE/DELETE/DDL).
- `DbQuery(mysql, sql)` : exécution avec résultat ; appeler `DbFreeResult` sur le résultat.
- `DbBeginTransaction`, `DbCommit`, `DbRollback` : transactions explicites.
- `ScopedTransaction` : RAII Begin → Commit ou Rollback en cas d’exception.

Pour les requêtes avec paramètres utilisateur, utiliser les prepared statements du driver (mysql_stmt_* en C, ou équivalent) en plus de ces helpers.

## Phase 1a — SQLStorage / SqlPreparedStatement / SqlDelayThread (CMANGOS.13)

Trois utilitaires DB ajoutés dans `engine/server/db/` :

### `SQLStorage<T>` — cache RAM typé read-only

Pour les tables **statiques** consultées en hot path (templates créatures,
items, factions, etc.). Charge une fois au boot via `Load(pool, table, pk,
mapper)`, puis `Find(pk)` est O(1) sans lock.

```cpp
struct CreatureTemplate { uint32_t entry; std::string name; int32_t level; };

SQLStorage<CreatureTemplate> g_creatureTemplates;
g_creatureTemplates.Load(pool, "creature_template", "entry",
    [](char** row) -> CreatureTemplate {
        CreatureTemplate t{};
        t.entry = std::strtoul(row[0], nullptr, 10);
        t.name  = row[1] ? row[1] : "";
        t.level = std::atoi(row[2]);
        return t;
    });

const CreatureTemplate* tmpl = g_creatureTemplates.Find(42);
```

**Convention** : un `SQLStorage` par table statique, instancié comme global
ou membre de `ServerApp`. Pas de hot-reload pour l'instant — refonte du
storage = redéploiement.

### `SqlPreparedStatement` — bindings type-safe + cache LRU

Pour les **queries hot path** avec paramètres. Évite le parsing SQL côté
MySQL à chaque appel.

```cpp
SqlPreparedStatementCache cache(64);  // par worker DB
auto* stmt = cache.Acquire(mysql, "SELECT name FROM accounts WHERE id = ?");
stmt->Bind(0, accountId);
stmt->Execute();
while (stmt->FetchRow())
{
    std::string name = stmt->GetString(0);
}
```

**Convention** : un cache par worker thread (pas de mutex interne). Si
plusieurs threads partagent une connexion, ils partagent un cache —
seriaaliser via mutex applicatif.

### `SqlDelayThread` — worker async pour DB hors tick

Pour les opérations **non-bloquantes** (audit log, save différé). Le tick
n'attend pas la complétion DB.

```cpp
SqlDelayThread worker(pool, 1024);
worker.Start();

worker.EnqueueExecute("INSERT INTO audit (...) VALUES (...)",
    [](bool ok) { if (!ok) LOG_WARN(Core, "audit failed"); });

// fin du process :
worker.Stop();  // drain queue puis join
```

**Politique queue pleine** : `EnqueueExecute` retourne `false`. Le caller
décide (drop, retry, log). Ne **jamais** bloquer le tick en attente de
slot.
