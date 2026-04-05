-- M33.1 — Migration 0004: Auth MVP schema extension
-- Adds: servers, races tables; expands characters with server/race/class/level/appearance;
--        adds active_session to accounts.
--
-- Idempotent : colonnes / index / FK ajoutés seulement s’ils manquent (information_schema + PREPARE).
-- Évite ADD COLUMN IF NOT EXISTS / DROP … IF EXISTS (non supportés ou mal reconnus selon versions / builds).

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
-- accounts: active_session
-- ---------------------------------------------------------------------------
SET @m4_ac := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'active_session'
);
SET @m4_as := IF(@m4_ac = 0,
  'ALTER TABLE accounts ADD COLUMN active_session BIGINT UNSIGNED NULL DEFAULT NULL COMMENT ''session_id of the current active session (references sessions.id)''',
  'SELECT 1');
PREPARE m4_pa FROM @m4_as;
EXECUTE m4_pa;
DEALLOCATE PREPARE m4_pa;

-- ---------------------------------------------------------------------------
-- characters: expand placeholder table to full M33.1 spec
-- ---------------------------------------------------------------------------
SET @m4_x1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'server_id'
);
SET @m4_q1 := IF(@m4_x1 = 0,
  'ALTER TABLE characters ADD COLUMN server_id INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''references servers.id (même type que servers.id)''',
  'SELECT 1');
PREPARE m4_p1 FROM @m4_q1;
EXECUTE m4_p1;
DEALLOCATE PREPARE m4_p1;

SET @m4_x2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'race_id'
);
SET @m4_q2 := IF(@m4_x2 = 0,
  'ALTER TABLE characters ADD COLUMN race_id INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''references races.id''',
  'SELECT 1');
PREPARE m4_p2 FROM @m4_q2;
EXECUTE m4_p2;
DEALLOCATE PREPARE m4_p2;

SET @m4_x3 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'class_id'
);
SET @m4_q3 := IF(@m4_x3 = 0,
  'ALTER TABLE characters ADD COLUMN class_id SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''character class identifier (gameplay config; no FK)''',
  'SELECT 1');
PREPARE m4_p3 FROM @m4_q3;
EXECUTE m4_p3;
DEALLOCATE PREPARE m4_p3;

SET @m4_x4 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'level'
);
SET @m4_q4 := IF(@m4_x4 = 0,
  'ALTER TABLE characters ADD COLUMN level SMALLINT UNSIGNED NOT NULL DEFAULT 1 COMMENT ''character level (>=1)''',
  'SELECT 1');
PREPARE m4_p4 FROM @m4_q4;
EXECUTE m4_p4;
DEALLOCATE PREPARE m4_p4;

SET @m4_x5 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'appearance_json'
);
SET @m4_q5 := IF(@m4_x5 = 0,
  'ALTER TABLE characters ADD COLUMN appearance_json TEXT NULL COMMENT ''JSON blob for appearance customisation''',
  'SELECT 1');
PREPARE m4_p5 FROM @m4_q5;
EXECUTE m4_p5;
DEALLOCATE PREPARE m4_p5;

SET @m4_x6 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'modified_at'
);
SET @m4_q6 := IF(@m4_x6 = 0,
  'ALTER TABLE characters ADD COLUMN modified_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT ''last modification timestamp''',
  'SELECT 1');
PREPARE m4_p6 FROM @m4_q6;
EXECUTE m4_p6;
DEALLOCATE PREPARE m4_p6;

-- Ancienne 0004 : server_id pouvait être BIGINT (FK refusée) ; retirer FK puis normaliser le type.
SET @m4_fk1 := (
  SELECT COUNT(*) FROM information_schema.table_constraints
  WHERE constraint_schema = DATABASE() AND table_name = 'characters'
    AND constraint_name = 'fk_characters_server_id' AND constraint_type = 'FOREIGN KEY'
);
SET @m4_df1 := IF(@m4_fk1 > 0,
  'ALTER TABLE characters DROP FOREIGN KEY fk_characters_server_id',
  'SELECT 1');
PREPARE m4_pdf1 FROM @m4_df1;
EXECUTE m4_pdf1;
DEALLOCATE PREPARE m4_pdf1;

SET @m4_fk2 := (
  SELECT COUNT(*) FROM information_schema.table_constraints
  WHERE constraint_schema = DATABASE() AND table_name = 'characters'
    AND constraint_name = 'fk_characters_race_id' AND constraint_type = 'FOREIGN KEY'
);
SET @m4_df2 := IF(@m4_fk2 > 0,
  'ALTER TABLE characters DROP FOREIGN KEY fk_characters_race_id',
  'SELECT 1');
PREPARE m4_pdf2 FROM @m4_df2;
EXECUTE m4_pdf2;
DEALLOCATE PREPARE m4_pdf2;

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

SET @m4_ixold := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND index_name = 'uq_characters_account_slot'
);
SET @m4_dix := IF(@m4_ixold > 0,
  'ALTER TABLE characters DROP INDEX uq_characters_account_slot',
  'SELECT 1');
PREPARE m4_pdix FROM @m4_dix;
EXECUTE m4_pdix;
DEALLOCATE PREPARE m4_pdix;

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
