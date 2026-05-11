-- Migration 0030 — terms_editions.published_at nullable
-- Un brouillon n'a pas de date de publication ; la colonne doit accepter NULL.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

SET @m30_c1 := (
  SELECT IS_NULLABLE FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'terms_editions' AND column_name = 'published_at'
);
SET @m30_s1 := IF(@m30_c1 = 'NO',
  'ALTER TABLE terms_editions MODIFY COLUMN published_at TIMESTAMP NULL DEFAULT NULL COMMENT ''date de publication effective (NULL si brouillon)''',
  'SELECT 1');
PREPARE m30_p1 FROM @m30_s1;
EXECUTE m30_p1;
DEALLOCATE PREPARE m30_p1;

SET FOREIGN_KEY_CHECKS = 1;
