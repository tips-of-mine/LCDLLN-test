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
