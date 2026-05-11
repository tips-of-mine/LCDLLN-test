-- M32.3 — Migration 0003: guild tables (guilds, guild_members, guild_ranks).
-- guilds       : one row per guild (name unique, 3-20 chars).
-- guild_members: one row per (guild, player) pair; rank_id references guild_ranks.
-- guild_ranks  : rank definitions with permission bitfields per guild.
-- Creation cost (1000 gold) and max-members (500) are enforced in the application layer.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ---------------------------------------------------------------------------
-- guilds
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS guilds (
  id               BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
                   COMMENT 'guild id (auto-increment)',
  name             VARCHAR(20)     NOT NULL
                   COMMENT 'unique guild name, 3-20 characters',
  motd             VARCHAR(512)    NOT NULL DEFAULT ''
                   COMMENT 'message of the day, editable by Officer+',
  master_player_id BIGINT UNSIGNED NOT NULL
                   COMMENT 'account id of the current Guild Master',
  created_at       TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_guilds_name (name),
  KEY ix_guilds_master (master_player_id),
  CONSTRAINT chk_guilds_name_length CHECK (CHAR_LENGTH(name) BETWEEN 3 AND 20)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- guild_members
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS guild_members (
  guild_id  BIGINT UNSIGNED  NOT NULL
            COMMENT 'references guilds.id',
  player_id BIGINT UNSIGNED  NOT NULL
            COMMENT 'references accounts.id',
  rank_id   TINYINT UNSIGNED NOT NULL DEFAULT 3
            COMMENT '0=GuildMaster, 1=Officer, 2=Member, 3=Recruit',
  joined_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (guild_id, player_id),
  KEY ix_guild_members_player (player_id),
  CONSTRAINT chk_guild_members_rank CHECK (rank_id <= 3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- guild_ranks  (customizable rank definitions per guild)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS guild_ranks (
  guild_id             BIGINT UNSIGNED  NOT NULL
                       COMMENT 'references guilds.id',
  rank_id              TINYINT UNSIGNED NOT NULL
                       COMMENT '0-3 in the default scheme; extensible',
  rank_name            VARCHAR(32)      NOT NULL
                       COMMENT 'display label for this rank',
  permissions_bitfield INT UNSIGNED     NOT NULL DEFAULT 0
                       COMMENT 'OR of GuildPermission bits: Invite=1, Kick=2, Promote=4, WithdrawBank=8, EditMotd=16',
  PRIMARY KEY (guild_id, rank_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

SET FOREIGN_KEY_CHECKS = 1;
