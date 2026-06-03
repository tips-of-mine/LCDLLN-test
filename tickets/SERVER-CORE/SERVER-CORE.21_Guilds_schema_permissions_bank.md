# SERVER-CORE.21_Guilds_schema_permissions_bank

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/masterd/handlers/guild/GuildHandler.h
> - Manque : Guild/ranks/bank/eventlog absents
> - Resume : Guilds partiel

## Objectif

Mettre en place le **système de guildes persistantes** côté master
LCDLLN, inspiré de `src/game/Guilds` server-core. Cinq piliers :

1. **Schéma DB séparé** : `guild`, `guild_member`, `guild_rank`,
   `guild_bank_item`, `guild_bank_tab`, `guild_eventlog` —
   séparation propre rank/membre/banque.
2. **Permissions par rank en bitmask** : chaque rank a un bitfield
   (kick, invite, withdraw_money, edit_motd…) ; ajouter une perm =
   ajouter un bit, pas une colonne.
3. **Event log circulaire en DB** : table avec rotation par taille max,
   sert d'audit + d'UI client.
4. **`GuildMgr` singleton** charge toutes les guildes au boot —
   acceptable car peu nombreuses (~100k max sur un serveur AAA).
5. **Banque multi-onglets** avec permissions par onglet par rank —
   flexibilité forte pour peu de complexité.

C'est un **P2 master**, pré-requis dès qu'on veut une vie sociale
durable.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.06 (Accounts)
- SERVER-CORE.13 (Database)
- SERVER-CORE.18 (Mails) — invitations / messages broadcast
- Économie (gold/items) existante.

## Livrables

### Côté master (`engine/server/guilds/`)

- `Guild.{h,cpp}` :
  ```cpp
  class Guild {
  public:
    bool AddMember(uint32_t accountId, std::string_view rankName);
    bool RemoveMember(uint32_t accountId);
    bool ChangeRank(uint32_t accountId, std::string_view newRank);
    bool DepositBank(uint32_t accountId, uint8_t tabIdx, ObjectGuid item);
    bool WithdrawBank(uint32_t accountId, uint8_t tabIdx, ObjectGuid item);
    void LogEvent(GuildEvent event, uint32_t actorAccountId, std::string_view detail);
    bool HasPermission(uint32_t accountId, GuildPermission perm) const;
  private:
    uint64_t m_guildId;
    std::string m_name;
    std::string m_motd;
    std::string m_charter;
    uint32_t m_leaderAccountId;
    std::vector<GuildRank> m_ranks;
    std::vector<GuildMember> m_members;
    std::vector<GuildBankTab> m_bankTabs;
    GuildEventLog m_eventLog;
  };
  ```
- `GuildRank.h` :
  ```cpp
  struct GuildRank {
    std::string name;
    GuildPermissionMask permissions;     // bitmask
    std::array<GuildBankTabPermissions, 6> bankTabPermissions;
  };
  ```
- `GuildPermission.h` — enum class bitmask :
  ```cpp
  enum class GuildPermission : uint32 {
    None         = 0,
    Invite       = 1u << 0,
    Kick         = 1u << 1,
    EditMotd     = 1u << 2,
    Promote      = 1u << 3,
    Demote       = 1u << 4,
    EditRanks    = 1u << 5,
    DepositBank  = 1u << 6,
    WithdrawBank = 1u << 7,
    EditCharter  = 1u << 8,
    KickHigher   = 1u << 9,    // peut kick un rank égal/supérieur (admin)
  };
  ```
- `GuildManager.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge toutes les guildes au boot.
  - `Guild* CreateGuild(uint32_t leaderAccountId, std::string_view name)`
  - `void Disband(uint64_t guildId)`
  - `Guild* GetGuildForAccount(uint32_t accountId)`
- `GuildHandler.{h,cpp}` — opcodes (15+).

### Migration DB

```sql
CREATE TABLE guild (
  guild_id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  name              VARCHAR(64) NOT NULL UNIQUE,
  motd              VARCHAR(255),
  charter           TEXT,
  leader_account_id INT UNSIGNED NOT NULL,
  created_ts        BIGINT NOT NULL,
  PRIMARY KEY (guild_id)
);

CREATE TABLE guild_rank (
  guild_id      BIGINT UNSIGNED NOT NULL,
  rank_idx      TINYINT UNSIGNED NOT NULL,
  name          VARCHAR(32) NOT NULL,
  permissions   BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (guild_id, rank_idx)
);

CREATE TABLE guild_member (
  guild_id      BIGINT UNSIGNED NOT NULL,
  account_id    INT UNSIGNED NOT NULL,
  rank_idx      TINYINT UNSIGNED NOT NULL,
  joined_ts     BIGINT NOT NULL,
  public_note   VARCHAR(255),
  officer_note  VARCHAR(255),
  PRIMARY KEY (account_id),                        -- 1 account = 1 guilde max
  KEY idx_guild (guild_id)
);

CREATE TABLE guild_bank_tab (
  guild_id      BIGINT UNSIGNED NOT NULL,
  tab_idx       TINYINT UNSIGNED NOT NULL,
  name          VARCHAR(32),
  icon          VARCHAR(64),
  PRIMARY KEY (guild_id, tab_idx)
);

CREATE TABLE guild_bank_item (
  guild_id      BIGINT UNSIGNED NOT NULL,
  tab_idx       TINYINT UNSIGNED NOT NULL,
  slot_idx      SMALLINT UNSIGNED NOT NULL,
  item_guid     BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (guild_id, tab_idx, slot_idx)
);

CREATE TABLE guild_eventlog (
  log_id        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  guild_id      BIGINT UNSIGNED NOT NULL,
  event_type    TINYINT UNSIGNED NOT NULL,    -- enum GuildEvent
  actor_id      INT UNSIGNED NOT NULL,
  target_id     INT UNSIGNED,
  detail        VARCHAR(255),
  ts            BIGINT NOT NULL,
  PRIMARY KEY (log_id),
  KEY idx_guild_ts (guild_id, ts)
);
```

### Configuration (`config.json`)

```json
"guild": {
  "max_members": 1000,
  "max_ranks": 10,
  "default_ranks": ["Recrue", "Membre", "Vétéran", "Officier", "Maître de guilde"],
  "max_bank_tabs": 6,
  "eventlog_retention_count": 200
}
```

### Tests

- `GuildManagerTests.cpp` — create + add member + kick.
- `GuildPermissionsTests.cpp` — bitmask : un membre sans `Invite` ne peut pas inviter.
- `GuildBankTests.cpp` — deposit/withdraw avec permission par tab.
- `GuildEventLogTests.cpp` — rotation : > 200 events → purge des plus anciens.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Permissions bitmask + rank

```cpp
GuildRank rankMembre {
  .name = "Membre",
  .permissions = GuildPermission::DepositBank,    // peut deposit, pas withdraw
};
```

Un check : `if (!guild->HasPermission(accountId, GuildPermission::Kick)) return reject;`

### 2. Event log circulaire

```cpp
void LogEvent(GuildEvent type, uint32_t actor, std::string_view detail) {
  m_log.push_back({type, actor, detail, now()});
  if (m_log.size() > kMaxLogEntries) {
    m_log.erase(m_log.begin(), m_log.begin() + (m_log.size() - kMaxLogEntries));
    DeleteOldestFromDb();   // purge async via SqlDelayThread
  }
}
```

### 3. Bank tab permissions

Chaque rank a 6 sous-perms par tab :
```cpp
struct GuildBankTabPermissions {
  bool canView;
  bool canDeposit;
  bool canWithdraw;
  uint32_t withdrawDailyLimit;     // gold + items
};
```

Permet « les officiers ont accès au tab 4 (loot raid), les membres
non ».

## Étapes d'implémentation

1. Créer `engine/server/guilds/`.
2. Migrations DB (6 tables).
3. Implémenter `Guild` + `GuildRank` + `GuildPermission`.
4. Implémenter `GuildManager::Load` (charge toutes au boot).
5. Implémenter opcodes + handler.
6. Implémenter banque multi-onglets.
7. Implémenter event log avec rotation.
8. Tests : 4 fichiers.
9. Doc : section « Guilds master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : create guild → invite → promote → set bank perms → withdraw item
- [ ] Permissions : membre sans `Kick` ne peut pas kick
- [ ] Eventlog rotation : > 200 events → 200 plus récents conservés
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **`KickHigher` permission** : un Officier ne peut pas kick le GM. Sauf si `KickHigher` activé sur son rank — réservé aux admins.
- **Permissions cumulatives** : un GM a forcément toutes les perms (sinon il peut se piéger lui-même).
- **Banque race** : 2 membres deposit en même temps dans le même slot → un seul réussit, l'autre erreur.
- **Daily withdraw limit** : reset quotidien à `daily_reset_hour_utc` (cf. quests config).
- **MOTD diffusion** : changement MOTD → broadcast à tous les membres online via opcode chat.
- **Disband** : disband d'une guilde retourne tous les items de banque par mail à leur dernier déposant si trackable, sinon au GM. Décider la politique.
- **Guild charter** : à la création, le founder doit signer avec X membres (10 dans WoW). À adapter selon vision LCDLLN.
- **Cross-shard guild chat** : le guild chat passe par le `ChatRelayHandler` (SERVER-CORE.01) — déjà master-side, naturel.

## Références

- `SERVER-CORE_ANALYSIS.md` § Guilds (P2 master)
- server-core `src/game/Guilds/Guild.cpp`, `GuildMgr.cpp`,
  `GuildBank*.cpp`, `GuildEventLogEntry.h`
