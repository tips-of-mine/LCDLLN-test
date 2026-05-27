-- ────────────────────────────────────────────────────────────────────────
-- 0069 — Ajoute l'index `ix_characters_server_id` sur `characters(server_id)`
-- ────────────────────────────────────────────────────────────────────────
--
-- Issu de l'audit codebase 2026-05-27 (PR 2, chantier C — scope ultra-minimal).
-- Réf : docs/superpowers/audits/2026-05-27-codebase-audit.md
--       docs/superpowers/plans/2026-05-27-pr2-sql-characters-server-id-index.md
--
-- Motivation :
--   `characters.server_id` est utilisé en `WHERE`/`AND` dans 3 hot handlers
--   (CharacterCreateHandler:67, :116 ; CharacterListHandler:79). Sans index,
--   ces queries font un table scan filtré ensuite par `account_id`. Avec
--   une table characters multi-comptes × multi-personnages qui grandit,
--   l'absence d'index sur server_id finit par dominer le coût des
--   requêtes character-list et character-create.
--
-- Idempotence : on vérifie INFORMATION_SCHEMA.STATISTICS avant CREATE.
-- Pattern repris de sql/migrations/0040_factions.sql:101-108 pour la
-- cohérence stylistique du repo.

SET @idx_exists := (
  SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS
  WHERE table_schema = DATABASE()
    AND table_name = 'characters'
    AND index_name = 'ix_characters_server_id'
);
SET @stmt := IF(@idx_exists = 0,
  'CREATE INDEX ix_characters_server_id ON characters (server_id)',
  'SELECT ''index ix_characters_server_id already exists, skipping'' AS msg');
PREPARE addidx FROM @stmt;
EXECUTE addidx;
DEALLOCATE PREPARE addidx;
