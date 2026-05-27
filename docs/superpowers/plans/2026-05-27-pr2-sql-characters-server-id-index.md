# PR 2 — Index `characters.server_id` (scope ultra-minimal)

> Plan exécuté directement (sans subagent-driven-development) vu la simplicité — 1 fichier SQL + 1 plan, ~30 minutes.

**Goal:** Ajouter un index `ix_characters_server_id` sur `characters(server_id)` via une migration SQL idempotente. C'est la seule lacune d'index réellement confirmée par l'audit codebase 2026-05-27 — les autres tables des hot queries (friends, guild_members) sont déjà bien indexées.

**Spec source :** [docs/superpowers/audits/2026-05-27-codebase-audit.md](../audits/2026-05-27-codebase-audit.md) section 8 PR 2, **scope ultra-minimal** validé par utilisateur (option A : "migration d'index seule") vu l'absence de toolchain C++ locale pour valider du refactor de handlers.

**Architecture:** Migration MySQL 8.0 idempotente via le pattern déjà utilisé dans `0040_factions.sql:101-108` (`SET @idx_exists := SELECT FROM INFORMATION_SCHEMA.STATISTICS` + `PREPARE`/`EXECUTE` conditionnel). Pas de DELIMITER (le `MigrationRunner` du repo gère le multi-statement mais pas les procédures avec body custom).

**Tech Stack:** MySQL 8.0 (image Docker `mysql:8.0`), MigrationRunner C++ avec checksum SHA-256 par fichier.

---

## Justification

### Query bénéficiant de l'index

`characters.server_id` est utilisé en `WHERE` ou `AND` dans 3 hot handlers :

| Handler | Référence | Query |
|---|---|---|
| `CharacterCreateHandler.cpp:67` | [lien](../../../src/masterd/handlers/character/CharacterCreateHandler.cpp) | `SELECT id FROM characters WHERE server_id = X AND ...` |
| `CharacterCreateHandler.cpp:116` | idem | `... AND server_id = X ...` |
| `CharacterListHandler.cpp:79` | [lien](../../../src/masterd/handlers/character/CharacterListHandler.cpp) | `JOIN ... AND c.server_id = X` |

Sans index, ces queries font un table scan filtré ensuite par `account_id` (qui lui est indexé via `ix_characters_account_id`). Avec une grande table `characters` (multi-comptes × multi-personnages), c'est sous-optimal.

### Pas de composite index pour l'instant

Le plan d'audit pouvait suggérer un composite `(server_id, account_id)`. **YAGNI** :
- L'index existant `ix_characters_account_id` couvre déjà `WHERE account_id = X` pour la query character list.
- Pour `WHERE account_id = X AND server_id = Y`, MySQL peut utiliser `ix_characters_account_id` puis filtrer Y en mémoire (rapide si le scan account_id est petit).
- Un index single-column `server_id` est suffisant pour les queries directes server-wide.
- Si un benchmark montre que c'est insuffisant, on ajoutera un composite plus tard.

### Choix du numéro de migration

Dernière migration existante : `0068_characters_skin_color.sql`. Nouvelle migration : **`0069_add_characters_server_id_index.sql`**.

---

## File structure

| Fichier | Action | Responsabilité |
|---|---|---|
| `sql/migrations/0069_add_characters_server_id_index.sql` | **Create** | Migration idempotente qui ajoute `ix_characters_server_id` si absent. |

---

## Déploiement

⚠️ **REDÉPLOIEMENT SERVEUR (master) requis** — la migration s'applique au boot du master via `MigrationRunner`. Pas de bump `kProtocolVersion` (pas de wire change). Shard non concerné.

L'ajout d'index sur une table existante peut prendre quelques secondes selon la taille de `characters`. MySQL 8 supporte `INSTANT` ALGORITHM pour beaucoup d'opérations DDL, mais l'ajout d'index reste un `INPLACE` algorithm avec un lock partiel — table reste lisible pendant la création, mais les writes sont brièvement bloqués. Sur une table de quelques milliers de characters, l'opération devrait être instantanée.

---

## Suivi

PR 2 sur 4 du plan d'audit. Cette PR clôt le scope minimaliste accepté par l'utilisateur. Reste hors scope (à programmer en PRs séparées avec build C++ local disponible) :
- Helper `DbPreparedQuery` dans `src/shared/db/`
- Migration des 5 handlers les plus exposés vers prepared statements
- Audit d'autres index manquants (s'il y en a — investigation jusqu'ici n'en a pas confirmé d'autres)
