-- Migration 0024 — Soft delete et force_rename pour les personnages
-- deleted_at : NULL = actif, non-NULL = suppression logique (soft delete)
-- force_rename : lu par lcdlln.exe à la connexion pour forcer le joueur à choisir un nouveau nom
-- Idempotent : colonnes ajoutées uniquement si manquantes.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Soft delete: deleted_at
SET @m24_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'deleted_at'
);
SET @m24_s1 := IF(@m24_c1 = 0,
  'ALTER TABLE characters ADD COLUMN deleted_at DATETIME NULL DEFAULT NULL COMMENT ''Suppression logique (NULL = actif)''',
  'SELECT 1');
PREPARE m24_p1 FROM @m24_s1;
EXECUTE m24_p1;
DEALLOCATE PREPARE m24_p1;

-- Force rename flag
SET @m24_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'force_rename'
);
SET @m24_s2 := IF(@m24_c2 = 0,
  'ALTER TABLE characters ADD COLUMN force_rename TINYINT(1) NOT NULL DEFAULT 0 COMMENT ''Lu par lcdlln.exe : force le joueur à choisir un nouveau nom''',
  'SELECT 1');
PREPARE m24_p2 FROM @m24_s2;
EXECUTE m24_p2;
DEALLOCATE PREPARE m24_p2;

SET FOREIGN_KEY_CHECKS = 1;
