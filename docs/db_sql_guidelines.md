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

## Phase 1b — Globals (CMANGOS.16)

Quatre utilitaires data-driven dans `engine/server/shard/globals/`,
chargés au boot du shard et consommés par les tickets P2 downstream.

### `ConditionMgr` — prédicats data-driven

Charge `conditions` + `condition_groups`. Évalue par ID via un
`EvaluationContext` rempli par le caller (loot, quête, AI EventAI...).

**Convention IDs** : `condition_id ∈ [1, 9999]`, `group_id ∈ [10000, ∞)`.
Le helper `Evaluate(id, ctx)` dispatche sur cette base.

5 ConditionTypes en Phase 1b : `LevelGE`, `LevelLE`, `HasItem`, `ZoneId`,
`InGroup`. Étendre via PR séparée au fil des besoins downstream.

### `ObjectAccessor` — façade thread-safe

Registre des entités en ligne (Player + Creature) côté shard.
Inscription au login/spawn via `Register(snapshot)`, désinscription au
logout/despawn via `Unregister(entityId)`. Lookups : `Find(entityId)`
(O(1)) et `FindByName(name)` (O(N), case-insensitive).

Thread-safety : `std::shared_mutex` — readers concurrents, writer
exclusif. Pour les hot paths existants (whisper par nom), continuer
d'utiliser `SessionCharacterMap`.

### `GraveyardManager` — closest valid graveyard

Charge `graveyards`. `ClosestGraveyard(mapId, pos, faction)` retourne
le graveyard valide (faction matchée OU neutral) le plus proche.
Stockage `std::vector` linéaire — N petit (centaines max), scan OK.

### `LocaleStrings` — i18n côté serveur

Charge `locale_strings`. `GetString(stringId, localeId)` avec fallback
sur `default_locale` (config). Si même default manque, sentinel
`"[stringId=<id>]"` (jamais empty pour debug).

`Format(stringId, locale, arg0..arg3)` : remplace `{0}/{1}/{2}/{3}`.
Limité à 4 args en Phase 1b.

### Convention IDs et tables

| Table | Range IDs | Note |
|---|---|---|
| `conditions.condition_id` | 1 — 9999 | Atomic predicates |
| `condition_groups.group_id` | 10000 — ∞ | Composition logique |
| `graveyards.id` | 1 — ∞ | Pas de range réservé |
| `locale_strings.string_id` | 1 — ∞ | Pas de range réservé |
| `locale_strings.locale_id` | 0=fr_FR, 1=en_US | Étendre selon besoin |

## Phase 1c — Account Roles (CMANGOS.06)

Hiérarchie 5 niveaux côté serveur :

| Rôle | Valeur | Persisté DB | Capacités |
|---|---|---|---|
| Player | 0 | oui | Gameplay normal |
| Moderator | 1 | oui | .mute, .kick, .warn |
| GameMaster | 2 | oui | + .ban, .tele, .spawn |
| Administrator | 3 | oui | + .account create/delete, .set role |
| Console | 4 | NON (runtime) | Toutes commandes (RCON, stdin process) |

### Règle d'or : `HasLowerSecurity`

**Toute action affectant un autre compte** (ban, kick, mute, set role,
inspect mail, whisper à GM caché) DOIT appeler `HasLowerSecurity(target,
source)` avant exécution.

```cpp
if (!roleService.HasLowerSecurity(targetId, callerId))
{
    LOG_WARN(Auth, "[AUDIT] denied_ban target={} by={}", targetId, callerId);
    return false;  // refus
}
// proceed
```

`HasLowerSecurity` retourne `true` UNIQUEMENT si `target.role < source.role`
strictement. Égalité = `false` (un GM ne peut pas ban un autre GM ;
nécessite un Administrator).

### Audit via SecurityAuditLog

Tout `SetRole` produit une ligne `role_change` dans `SecurityAuditLog`
via `LogModerationAction("role_change", actor_display, target_display,
"old=X new=Y")`.

### Migration progressive des handlers existants

Les handlers GM existants (avant Phase 1c) testent typiquement
`account.is_gm` ou `account.role == 'admin'`. Migration au cas par cas :

```cpp
// AVANT
if (!record.is_gm) return RefusalReason::NotGM;

// APRÈS (via AccountRoleService)
if (!roleService.RequireMinRole(callerId, AccountRole::GameMaster))
    return RefusalReason::InsufficientRole;
```

### Convention pour Console (RCON, stdin)

Le caller spécial RCON/stdin n'a pas d'`account_id` réel. On utilise un
sentinel `0xFFFFFFFFFFFFFFFFu` ou un flag dédié `ChatContext::isConsole`
qui force `RequireMinRole` à retourner `true` quel que soit le min.

`Console` n'est **jamais** stocké en DB. Si une ligne y est trouvée
(corruption), le store doit la rejeter au load avec un warning et la
traiter comme `Player`.

## Phase 2.01a — Chat sanitizer + gate (CMANGOS.01 sub-PR 1)

Couche de validation côté master, en série avant tout routage par
`ChatRelayHandler`. Pas de wire-breaking : le protocole n'a pas
changé, seuls les rejets côté serveur sont nouveaux.

### `ChatSanitizer` — pure function

`engine/server/chat/ChatSanitizer.h`. Étapes en série :

1. Reject si vide → `"empty"`.
2. UTF-8 safe truncation à `cfg.maxMessageBytes` (défaut 255). Ne coupe
   **jamais** au milieu d'un codepoint multi-byte. Recule depuis
   `maxBytes` jusqu'au premier byte non-continuation (`& 0xC0 != 0x80`).
3. Strip zero-width characters si `cfg.stripZeroWidth` :
   `U+200B..U+200F`, `U+FEFF`, `U+202A..U+202E`. Re-encode en UTF-8 sans
   les codepoints filtrés.
4. Reject si hyperlink `|H<type>:...|h<text>|h` avec `<type>` hors
   whitelist `{item, quest, achievement, spell}` → `"hyperlink_blocked"`.
   Hyperlink mal formé (pas de `:` après `|H`) aussi rejeté.
5. Reject si post-strip le texte est vide → `"post_strip_empty"`.

Aucune dépendance externe (DB, pool, réseau) : thread-safe par nature,
appelable depuis n'importe quel thread.

### `ChatGate` — décision ban/mute/anti-flood

`engine/server/chat/ChatGate.h`. Trois rejets possibles, ordre de
priorité descendant :

| Décision | Source | Action côté client |
|---|---|---|
| `Banned` | `AccountStore.FindByAccountId().status == Locked` | silencieux (court-circuit) |
| `Muted` | table `chat_mutes` (PK `account_id`, `until_ts ms UTC`) | notice "Server" + raison |
| `Flooding` | sliding window RAM (default 5 msg / 5s) | notice "Slow down" |

**API publique** :
- `Decide(accountId, nowMs)` : check pur, sans mutation. Pour preview.
- `DecideAndRecord(accountId, nowMs)` : check + record atomique. C'est
  l'API pour le hot path.
- `WireProduction(pool, accounts)` : câble les callbacks SQL+Store en
  prod (un seul appel au boot dans `main_server_linux.cpp`).

**Tuning** dans `ChatGateConfig` : `floodWindowMs`, `floodMaxMessages`,
`maxTrackedAccounts` (cap soft pour la mémoire).

### Table `chat_mutes`

```sql
CREATE TABLE IF NOT EXISTS chat_mutes (
  account_id  BIGINT UNSIGNED NOT NULL,
  until_ts    BIGINT NOT NULL,           -- 0 = permanent
  reason      VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (account_id),
  KEY ix_chat_mutes_until (until_ts)
);
```

Migration `0044_chat_mutes.sql`. Idempotente. Insertion d'un mute :
```sql
REPLACE INTO chat_mutes (account_id, until_ts, reason) VALUES (?, ?, ?);
```

`until_ts = 0` = mute permanent (jamais expiré). Sinon epoch ms UTC.

### Intégration

`ChatRelayHandler::HandlePacket` appelle `Sanitize` puis `Gate` avant
tout routage de canal (Say/Whisper/Guild/Friends/...). Le sanitizer
opère sur `parsed->text` ; le gate utilise `*accountId`. Les notices
"Server" muted/flooding ne sont visibles que par l'expéditeur.
