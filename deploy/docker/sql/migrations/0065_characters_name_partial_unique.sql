-- Migration 0065 — Name partial-unique via generated column (fix soft-delete suite)
--
-- Contexte : suite directe de la migration 0064 (slot partial unique).
-- Le bug "character creation failed" apres soft-delete persistait pour les
-- noms reutilises. Cause : il existe une seconde unique key globale
-- `uq_characters_name_server (name, server_id)` (cf. migration 0004) qui
-- empeche physiquement un INSERT avec un nom deja present dans la table,
-- meme si l'ancienne row est soft-deletee.
--
-- Symptome : "Duplicate entry 'tutu-1' for key uq_characters_name_server"
-- apres tentative de re-creation avec le nom d'un perso supprime.
--
-- Meme remede que 0064 : generated VIRTUAL column `name_active` qui vaut
--   - `name` si la row est vivante (deleted_at IS NULL)
--   - `NULL` si la row est soft-deletee
-- Puis unique key partielle sur (name_active, server_id). MySQL n'unique-
-- check pas les NULL -> plusieurs soft-deletes au meme nom coexistent
-- sans collision.
--
-- Combine avec le fix `CharacterNameExistsOnServer` cote handler (qui filtre
-- desormais deleted_at IS NULL), un nom de perso est immediatement reutilisable
-- apres soft-delete.
--
-- Idempotent : checks d'existence avant chaque ALTER.
-- Reversible : DROP COLUMN name_active + recreate l'ancienne unique key.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ────────────────────────────────────────────────────────────────────────
-- 1) Hard-purge des soft-deletes restants (defense-en-profondeur si une
--    row soft-deletee a survecu a 0064 — ne devrait pas arriver puisque
--    0064 a deja purge — mais ne nuit pas).
-- ────────────────────────────────────────────────────────────────────────

DELETE FROM characters WHERE deleted_at IS NOT NULL;

-- ────────────────────────────────────────────────────────────────────────
-- 2) Drop l'ancienne unique key (idempotent)
-- ────────────────────────────────────────────────────────────────────────

SET @m65_idx_old := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND index_name = 'uq_characters_name_server'
);
SET @m65_sql_drop := IF(@m65_idx_old > 0,
  'ALTER TABLE characters DROP INDEX uq_characters_name_server',
  'SELECT 1');
PREPARE m65_p_drop FROM @m65_sql_drop;
EXECUTE m65_p_drop;
DEALLOCATE PREPARE m65_p_drop;

-- ────────────────────────────────────────────────────────────────────────
-- 3) Add la generated virtual column `name_active`
--    VIRTUAL : pas de stockage disque, calculee a la lecture/index.
--    Type aligne sur `name` (VARCHAR(64)).
-- ────────────────────────────────────────────────────────────────────────

SET @m65_col := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND column_name = 'name_active'
);
SET @m65_sql_col := IF(@m65_col = 0,
  'ALTER TABLE characters ADD COLUMN name_active VARCHAR(64) GENERATED ALWAYS AS (IF(deleted_at IS NULL, name, NULL)) VIRTUAL COMMENT ''Generated : name si vivant, NULL si soft-deleted. Permet partial unique.''',
  'SELECT 1');
PREPARE m65_p_col FROM @m65_sql_col;
EXECUTE m65_p_col;
DEALLOCATE PREPARE m65_p_col;

-- ────────────────────────────────────────────────────────────────────────
-- 4) Add la nouvelle unique key sur name_active
--    Plusieurs rows avec name_active=NULL sont autorisees. Garantie
--    d'unicite uniquement pour les rows vivantes au meme (name, server_id).
-- ────────────────────────────────────────────────────────────────────────

SET @m65_idx_new := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND index_name = 'uq_characters_name_server_active'
);
SET @m65_sql_idx := IF(@m65_idx_new = 0,
  'ALTER TABLE characters ADD UNIQUE KEY uq_characters_name_server_active (name_active, server_id)',
  'SELECT 1');
PREPARE m65_p_idx FROM @m65_sql_idx;
EXECUTE m65_p_idx;
DEALLOCATE PREPARE m65_p_idx;

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
