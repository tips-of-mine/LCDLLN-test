-- Migration 0020 — Colonne role sur les comptes (portail web).
-- Appliquer après 0019. Idempotente.
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

SET @m20_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND column_name  = 'role'
);
SET @m20_s := IF(@m20_c = 0,
  'ALTER TABLE accounts ADD COLUMN role ENUM(''player'',''admin'',''moderator'') NOT NULL DEFAULT ''player'' AFTER tag_id',
  'SELECT 1');
PREPARE m20_p FROM @m20_s; EXECUTE m20_p; DEALLOCATE PREPARE m20_p;

SET FOREIGN_KEY_CHECKS = 1;
