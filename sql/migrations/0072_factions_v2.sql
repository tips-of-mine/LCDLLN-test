-- Migration 0072 — Factions v2 : alignement sur les ids courts du design
-- (Système de Personnages). Renomme les slugs longs de 0040, ajoute les
-- nouvelles factions, repasse Chevaliers-Dragons en race humains, et
-- backfill characters.faction_str. Idempotent.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;
START TRANSACTION;

-- 1) Renommer les slugs existants vers les ids courts + corriger race_lock.
UPDATE factions SET name='lumiere'    WHERE name='chevaliers_lumiere';
UPDATE factions SET name='justice'    WHERE name='chevaliers_justice';
UPDATE factions SET name='legion', display_name='Légion infernale' WHERE name='demons';
UPDATE factions SET name='dragons', race_lock='humains' WHERE name='chevaliers_dragons';
-- lune_noire, dzorak, empire_hynn : ids déjà alignés.

-- 2) Ajouter les nouvelles factions (INSERT IGNORE = idempotent sur uq_factions_name).
INSERT IGNORE INTO factions (name, display_name, race_lock, parent_faction_id, description) VALUES
  ('serpent', 'Maison du Serpent', 'humains', NULL, 'Maison humaine versée dans la magie et l''ombre.'),
  ('naine',   'Faction Naine',     'nains',   NULL, 'Peuple nain : guerriers tenaces et artisans.'),
  ('elfe',    'Faction Elfe',      'elfes',   NULL, 'Peuple elfe : agilité, magie et discrétion.');

-- 3) empire_hynn : conservée, NON sélectionnable. Ajouter la colonne `selectable`
--    si absente (défaut 1), puis marquer empire_hynn à 0.
SET @col_exists := (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
  WHERE table_schema = DATABASE() AND table_name='factions' AND column_name='selectable');
SET @stmt := IF(@col_exists = 0,
  'ALTER TABLE factions ADD COLUMN selectable TINYINT(1) NOT NULL DEFAULT 1 COMMENT ''0 = présente mais non sélectionnable à la création''',
  'SELECT ''column factions.selectable already exists, skipping''');
PREPARE a FROM @stmt; EXECUTE a; DEALLOCATE PREPARE a;
UPDATE factions SET selectable = 0 WHERE name = 'empire_hynn';

-- 4) Backfill characters.faction_str pour les slugs renommés (persos existants).
UPDATE characters SET faction_str='lumiere' WHERE faction_str='chevaliers_lumiere';
UPDATE characters SET faction_str='justice' WHERE faction_str='chevaliers_justice';
UPDATE characters SET faction_str='legion'  WHERE faction_str='demons';
UPDATE characters SET faction_str='dragons' WHERE faction_str='chevaliers_dragons';

COMMIT;
SET FOREIGN_KEY_CHECKS = 1;
