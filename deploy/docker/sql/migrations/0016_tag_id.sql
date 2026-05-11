-- Migration 0016 — TAG-ID et country_code sur les comptes (Plan D).
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Ajout country_code si absent.
SET @m16_cc := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'country_code'
);
SET @m16_cc_s := IF(@m16_cc = 0,
  'ALTER TABLE accounts ADD COLUMN country_code VARCHAR(2) NOT NULL DEFAULT '''' AFTER email_verified',
  'SELECT 1');
PREPARE m16_cc_p FROM @m16_cc_s; EXECUTE m16_cc_p; DEALLOCATE PREPARE m16_cc_p;

-- Ajout tag_id si absent.
SET @m16_ti := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'tag_id'
);
SET @m16_ti_s := IF(@m16_ti = 0,
  'ALTER TABLE accounts ADD COLUMN tag_id VARCHAR(10) NOT NULL DEFAULT '''' AFTER country_code',
  'SELECT 1');
PREPARE m16_ti_p FROM @m16_ti_s; EXECUTE m16_ti_p; DEALLOCATE PREPARE m16_ti_p;

-- Index unique sur tag_id.
-- IMPORTANT : Sur une base non vide, les lignes existantes auront tag_id = '' ce qui viole
-- la contrainte UNIQUE. Backfiller avant de lancer sur un environnement avec des comptes :
--   UPDATE accounts SET tag_id = LPAD(id, 10, '0') WHERE tag_id = '';

SET @m16_ix := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND index_name = 'idx_accounts_tag_id'
);
SET @m16_ix_s := IF(@m16_ix = 0,
  'CREATE UNIQUE INDEX idx_accounts_tag_id ON accounts(tag_id)',
  'SELECT 1');
PREPARE m16_ix_p FROM @m16_ix_s; EXECUTE m16_ix_p; DEALLOCATE PREPARE m16_ix_p;

SET FOREIGN_KEY_CHECKS = 1;
