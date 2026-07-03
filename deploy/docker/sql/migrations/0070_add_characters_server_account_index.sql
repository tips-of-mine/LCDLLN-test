-- ────────────────────────────────────────────────────────────────────────
-- 0070 — Ajoute l'index compound `ix_characters_server_account`
--         sur `characters(server_id, account_id)`
-- ────────────────────────────────────────────────────────────────────────
--
-- Issu de l'audit codebase 2026-05-28 (finding N3, ouvert depuis l'audit
-- 2026-05-27 finding F3 — partiellement résolu par la PR #722 / migration
-- 0069 qui n'avait ajouté que l'index simple `ix_characters_server_id`).
-- Réf : docs/superpowers/audits/2026-05-28-codebase-audit-fresh.md
--
-- Motivation :
--   Plusieurs hot handlers du master font des SELECT sur `characters`
--   avec un WHERE composite `server_id` + `account_id` :
--     - CharacterListHandler.cpp:78-81
--         WHERE c.account_id = X AND c.server_id = Y AND c.deleted_at IS NULL
--         ORDER BY c.slot ASC
--     - CharacterCreateHandler.cpp:115-117 (FindNextSlot)
--         WHERE account_id = X AND server_id = Y AND deleted_at IS NULL
--         ORDER BY slot ASC
--
--   Avec uniquement les index single-column `ix_characters_account_id`
--   (migration 0001) et `ix_characters_server_id` (migration 0069), MySQL
--   doit choisir l'un OU l'autre et fait un seek puis une lecture de
--   toutes les lignes correspondantes pour filtrer le second prédicat.
--   À mesure que la table grossit (multi-comptes × multi-personnages ×
--   multi-shards), cette double-passe domine le coût de ces requêtes.
--
--   Un index compound `(server_id, account_id)` permet à l'optimiseur
--   de faire un seek unique pour les 2 prédicats. L'ordre des colonnes
--   suit la recommandation de l'audit 2026-05-27 ; l'ordre du WHERE en
--   code C++ ne contraint pas l'optimiseur MySQL (qui re-ordonne).
--
-- Note de redondance (informationnelle, pas un nettoyage à faire ici) :
--   L'index single `ix_characters_server_id` ajouté en migration 0069
--   devient utilisable via le prefix de `ix_characters_server_account` ;
--   il reste néanmoins en place pour ne pas casser le rollback éventuel
--   de la migration 0070. Un nettoyage explicite peut être fait dans
--   une migration ultérieure une fois cette PR validée en prod.
--   L'index single `ix_characters_account_id` (migration 0001) reste
--   utile pour les SELECT solo sur `WHERE account_id = ?` (ex :
--   CharacterEnterWorldHandler:88) où server_id n'est pas dans le WHERE.
--
-- Idempotence : on vérifie INFORMATION_SCHEMA.STATISTICS avant CREATE.
-- Pattern repris de sql/migrations/0040_factions.sql:101-108 et
-- sql/migrations/0069_add_characters_server_id_index.sql.

SET @idx_exists := (
  SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS
  WHERE table_schema = DATABASE()
    AND table_name = 'characters'
    AND index_name = 'ix_characters_server_account'
);
SET @stmt := IF(@idx_exists = 0,
  'CREATE INDEX ix_characters_server_account ON characters (server_id, account_id)',
  'SELECT ''index ix_characters_server_account already exists, skipping'' AS msg');
PREPARE addidx FROM @stmt;
EXECUTE addidx;
DEALLOCATE PREPARE addidx;
