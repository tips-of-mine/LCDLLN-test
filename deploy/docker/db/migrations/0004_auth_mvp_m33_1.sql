-- M33.1 — Migration 0004: Auth MVP schema extension
-- Adds: servers, races tables; expands characters with server/race/class/level/appearance;
--        adds active_session to accounts.
-- Apply on top of 0003_guilds.sql. Execute in transaction; rollback on error.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- servers — game server registry (data-driven; can be seeded via config for MVP)
-- status: 0=offline, 1=online, 2=maintenance
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS servers (
  id         INT UNSIGNED     NOT NULL AUTO_INCREMENT,
  name       VARCHAR(64)      NOT NULL,
  region     VARCHAR(64)      NOT NULL DEFAULT '' COMMENT 'e.g. EU, NA, ASIA',
  status     TINYINT UNSIGNED NOT NULL DEFAULT 0
             COMMENT '0=offline, 1=online, 2=maintenance',
  created_at TIMESTAMP NULL   DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_servers_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- races — data-driven race definitions (not hardcoded)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS races (
  id          INT UNSIGNED  NOT NULL AUTO_INCREMENT,
  name        VARCHAR(64)   NOT NULL,
  description VARCHAR(512)  NOT NULL DEFAULT '',
  PRIMARY KEY (id),
  UNIQUE KEY uq_races_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- accounts: add active_session (session_id of the current active session)
-- ---------------------------------------------------------------------------
ALTER TABLE accounts
  ADD COLUMN active_session BIGINT UNSIGNED NULL DEFAULT NULL
    COMMENT 'session_id of the current active session (references sessions.id)';

-- ---------------------------------------------------------------------------
-- characters: expand placeholder table to full M33.1 spec
--   - Add server_id, race_id, class_id, level, appearance_json, modified_at
--   - Replace per-account slot uniqueness with per-account/server slot uniqueness
--   - Add name-unique-per-server constraint
--   - Add FK to servers and races
-- ---------------------------------------------------------------------------
ALTER TABLE characters
  ADD COLUMN server_id      INT UNSIGNED      NOT NULL DEFAULT 0
    COMMENT 'references servers.id (même type que servers.id)',
  ADD COLUMN race_id        INT UNSIGNED      NOT NULL DEFAULT 0
    COMMENT 'references races.id',
  ADD COLUMN class_id       SMALLINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'character class identifier (gameplay config; no FK — classes table reserved for future ticket)',
  ADD COLUMN level          SMALLINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT 'character level (>=1)',
  ADD COLUMN appearance_json TEXT NULL
    COMMENT 'JSON blob for appearance customisation (hair, skin, face…)',
  ADD COLUMN modified_at    TIMESTAMP NULL    DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP
    COMMENT 'last modification timestamp';

-- Drop old per-account-only slot uniqueness and replace with per-account/server uniqueness
ALTER TABLE characters
  DROP INDEX uq_characters_account_slot,
  ADD  UNIQUE KEY uq_characters_account_server_slot (account_id, server_id, slot)
    COMMENT 'max 5 characters per account per server (slot 0..4)',
  ADD  UNIQUE KEY uq_characters_name_server (name, server_id)
    COMMENT 'character name unique within a server',
  ADD  CONSTRAINT fk_characters_server_id
    FOREIGN KEY (server_id) REFERENCES servers (id),
  ADD  CONSTRAINT fk_characters_race_id
    FOREIGN KEY (race_id)   REFERENCES races   (id);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
