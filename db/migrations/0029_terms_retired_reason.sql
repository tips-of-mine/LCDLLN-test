-- Migration 0029 — Raison de retrait pour les éditions de CGU
-- Ajout de colonne : retired_reason
-- Permet de documenter pourquoi une édition de CGU a été retirée (replaced by v2, obsolete, etc.)

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Retired reason for terms editions
SET @m29_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'terms_editions' AND column_name = 'retired_reason'
);
SET @m29_s1 := IF(@m29_c1 = 0,
  'ALTER TABLE terms_editions ADD COLUMN retired_reason TEXT NULL COMMENT ''Raison du retrait de l''édition (status=retired)''',
  'SELECT 1');
PREPARE m29_p1 FROM @m29_s1;
EXECUTE m29_p1;
DEALLOCATE PREPARE m29_p1;

SET FOREIGN_KEY_CHECKS = 1;
