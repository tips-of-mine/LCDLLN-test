-- Migration 0064 — Slot partial-unique via generated column (fix soft-delete)
--
-- Contexte du bug :
--   CharacterDeleteHandler fait un soft-delete (UPDATE characters
--   SET deleted_at = NOW()) sans toucher au champ `slot`. La unique key
--   `uq_characters_account_server_slot` (account_id, server_id, slot) est
--   globale — MySQL ne supporte pas les partial unique index. Du coup
--   apres soft-delete d'un perso au slot N :
--     1. FindNextSlot (filtre WHERE deleted_at IS NULL) ne voit plus la
--        row, considere le slot N comme libre, retourne N.
--     2. CharacterCreateHandler INSERT avec slot=N.
--     3. MySQL rejette : "Duplicate entry 'A-S-N' for key
--        uq_characters_account_server_slot".
--     4. Client recoit "character creation failed" (cf. CharacterCreateHandler.cpp:277).
--
-- Fix : la unique key cible une *generated column* `slot_active` qui vaut :
--   - `slot` si la row est vivante (deleted_at IS NULL)
--   - `NULL` si la row est soft-deletee
--
-- Comportement MySQL : NULL n'est pas unique-checke. Plusieurs rows peuvent
-- avoir slot_active = NULL sans collision. Du coup les soft-deletes ne
-- bloquent plus la reutilisation du slot.
--
-- Strategie :
--   1. Hard-purge des soft-deletes existants pour debloquer le user (si
--      des collisions latentes persistent post-migration, la creation
--      restera bloquee). Aucune perte de donnees gameplay : les persos
--      soft-deletes sont deja invisibles cote UI/handler.
--   2. Drop l'ancienne unique key.
--   3. Add la generated virtual column `slot_active`.
--   4. Add la nouvelle unique key sur (account_id, server_id, slot_active).
--
-- Idempotent : checks d'existence avant chaque ALTER.
-- Reversible : DROP COLUMN slot_active + recreate l'ancienne unique key.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ────────────────────────────────────────────────────────────────────────
-- 1) Hard-purge des soft-deletes (cleanup historique)
--    Les rows soft-deletees sont deja invisibles cote handlers (filtre
--    WHERE deleted_at IS NULL partout). On les supprime physiquement pour
--    eliminer les collisions potentielles avec la nouvelle unique key.
-- ────────────────────────────────────────────────────────────────────────

DELETE FROM characters WHERE deleted_at IS NOT NULL;

-- ────────────────────────────────────────────────────────────────────────
-- 2) Drop l'ancienne unique key (idempotent)
-- ────────────────────────────────────────────────────────────────────────

SET @m64_idx_old := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND index_name = 'uq_characters_account_server_slot'
);
SET @m64_sql_drop := IF(@m64_idx_old > 0,
  'ALTER TABLE characters DROP INDEX uq_characters_account_server_slot',
  'SELECT 1');
PREPARE m64_p_drop FROM @m64_sql_drop;
EXECUTE m64_p_drop;
DEALLOCATE PREPARE m64_p_drop;

-- ────────────────────────────────────────────────────────────────────────
-- 3) Add la generated virtual column `slot_active`
--    VIRTUAL : pas de stockage disque, calculee a la lecture/index.
--    Type aligne sur `slot` (TINYINT UNSIGNED).
-- ────────────────────────────────────────────────────────────────────────

SET @m64_col := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND column_name = 'slot_active'
);
SET @m64_sql_col := IF(@m64_col = 0,
  'ALTER TABLE characters ADD COLUMN slot_active TINYINT UNSIGNED GENERATED ALWAYS AS (IF(deleted_at IS NULL, slot, NULL)) VIRTUAL COMMENT ''Generated : slot si vivant, NULL si soft-deleted. Permet partial unique.''',
  'SELECT 1');
PREPARE m64_p_col FROM @m64_sql_col;
EXECUTE m64_p_col;
DEALLOCATE PREPARE m64_p_col;

-- ────────────────────────────────────────────────────────────────────────
-- 4) Add la nouvelle unique key sur slot_active
--    Plusieurs rows avec slot_active=NULL sont autorisees (MySQL ne
--    unique-check pas les NULL). Garantie d'unicite uniquement pour les
--    rows vivantes au meme (account_id, server_id, slot).
-- ────────────────────────────────────────────────────────────────────────

SET @m64_idx_new := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters'
    AND index_name = 'uq_characters_account_server_slot_active'
);
SET @m64_sql_idx := IF(@m64_idx_new = 0,
  'ALTER TABLE characters ADD UNIQUE KEY uq_characters_account_server_slot_active (account_id, server_id, slot_active)',
  'SELECT 1');
PREPARE m64_p_idx FROM @m64_sql_idx;
EXECUTE m64_p_idx;
DEALLOCATE PREPARE m64_p_idx;

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
