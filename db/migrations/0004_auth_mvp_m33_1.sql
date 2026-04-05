-- M33.1 — Migration 0004: Auth MVP schema extension
-- Adds: servers, races tables; expands characters with server/race/class/level/appearance;
--        adds active_session to accounts.
--
-- Idempotent (MySQL ≥ 8.0.29 : ADD/DROP COLUMN/INDEX IF [NOT] EXISTS) pour reprendre après
-- un échec partiel : le DDL InnoDB peut valider des étapes avant l’échec d’une contrainte.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

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
  ADD COLUMN IF NOT EXISTS active_session BIGINT UNSIGNED NULL DEFAULT NULL
    COMMENT 'session_id of the current active session (references sessions.id)';

-- ---------------------------------------------------------------------------
-- characters: expand placeholder table to full M33.1 spec
-- ---------------------------------------------------------------------------
ALTER TABLE characters
  ADD COLUMN IF NOT EXISTS server_id      INT UNSIGNED      NOT NULL DEFAULT 0
    COMMENT 'references servers.id (même type que servers.id)',
  ADD COLUMN IF NOT EXISTS race_id        INT UNSIGNED      NOT NULL DEFAULT 0
    COMMENT 'references races.id',
  ADD COLUMN IF NOT EXISTS class_id       SMALLINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'character class identifier (gameplay config; no FK — classes table reserved for future ticket)',
  ADD COLUMN IF NOT EXISTS level          SMALLINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT 'character level (>=1)',
  ADD COLUMN IF NOT EXISTS appearance_json TEXT NULL
    COMMENT 'JSON blob for appearance customisation (hair, skin, face…)',
  ADD COLUMN IF NOT EXISTS modified_at    TIMESTAMP NULL    DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP
    COMMENT 'last modification timestamp';

-- Ancienne 0004 : server_id pouvait être BIGINT (FK refusée) ; normaliser avant les contraintes.
ALTER TABLE characters DROP FOREIGN KEY IF EXISTS fk_characters_server_id;
ALTER TABLE characters DROP FOREIGN KEY IF EXISTS fk_characters_race_id;

SET @has_srv := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'server_id'
);
SET @sqlm := IF(@has_srv > 0,
  'ALTER TABLE characters MODIFY COLUMN server_id INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''references servers.id (même type que servers.id)''',
  'SELECT 1');
PREPARE pm FROM @sqlm;
EXECUTE pm;
DEALLOCATE PREPARE pm;

ALTER TABLE characters DROP INDEX IF EXISTS uq_characters_account_slot;

SET @idx1 := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND index_name = 'uq_characters_account_server_slot'
);
SET @sql1 := IF(@idx1 = 0,
  'ALTER TABLE characters ADD UNIQUE KEY uq_characters_account_server_slot (account_id, server_id, slot)',
  'SELECT 1');
PREPARE ps1 FROM @sql1;
EXECUTE ps1;
DEALLOCATE PREPARE ps1;

SET @idx2 := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND index_name = 'uq_characters_name_server'
);
SET @sql2 := IF(@idx2 = 0,
  'ALTER TABLE characters ADD UNIQUE KEY uq_characters_name_server (name, server_id)',
  'SELECT 1');
PREPARE ps2 FROM @sql2;
EXECUTE ps2;
DEALLOCATE PREPARE ps2;

SET @fks := (
  SELECT COUNT(*) FROM information_schema.table_constraints
  WHERE constraint_schema = DATABASE() AND table_name = 'characters' AND constraint_name = 'fk_characters_server_id'
);
SET @sqlf1 := IF(@fks = 0,
  'ALTER TABLE characters ADD CONSTRAINT fk_characters_server_id FOREIGN KEY (server_id) REFERENCES servers (id)',
  'SELECT 1');
PREPARE pf1 FROM @sqlf1;
EXECUTE pf1;
DEALLOCATE PREPARE pf1;

SET @fkr := (
  SELECT COUNT(*) FROM information_schema.table_constraints
  WHERE constraint_schema = DATABASE() AND table_name = 'characters' AND constraint_name = 'fk_characters_race_id'
);
SET @sqlf2 := IF(@fkr = 0,
  'ALTER TABLE characters ADD CONSTRAINT fk_characters_race_id FOREIGN KEY (race_id) REFERENCES races (id)',
  'SELECT 1');
PREPARE pf2 FROM @sqlf2;
EXECUTE pf2;
DEALLOCATE PREPARE pf2;

SET FOREIGN_KEY_CHECKS = 1;
