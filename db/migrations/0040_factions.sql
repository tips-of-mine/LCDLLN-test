-- Migration 0040 — Systeme de factions
--
-- Introduit le concept de faction (Lumiere / Ombres / Crepuscule) qui
-- structure le PvP, la diplomatie inter-zones et determine les points
-- de spawn par defaut au premier login.
--
-- Schema :
--   * Table `factions` (3 lignes seedees + id 0 'unaligned' fallback).
--   * Colonne `characters.faction_str` (VARCHAR, alignee sur factions.name)
--     -- meme strategie que `race_str` (cf. 0033) : pas de FK numerique pour
--     simplifier les imports / la migration de donnees ulterieurement.
--
-- Alignement faction <-> race par defaut (modifiable a la creation du perso) :
--   Lumiere    : humains, nains, chevaliers_dragons, elfes
--   Ombres     : orcs, demons
--   Crepuscule : aucune race par defaut (neutres / mercenaires)
--
-- Note : la valeur par defaut '' (chaine vide) est preferee a 'unaligned'
-- pour qu'un personnage existant cree avant cette migration n'apparaisse
-- pas comme appartenant a une faction. Le code traite '' comme synonyme
-- de 'crepuscule' (neutre) pour le gameplay.
--
-- Idempotent : INSERT IGNORE + INFORMATION_SCHEMA guard sur la colonne.
-- Reversible : DROP TABLE factions ; ALTER TABLE characters DROP COLUMN faction_str.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ────────────────────────────────────────────────────────────────────────
-- 1) Table `factions`
-- ────────────────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS factions (
  id          INT UNSIGNED NOT NULL AUTO_INCREMENT,
  name        VARCHAR(32)  NOT NULL,
  description VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (id),
  UNIQUE KEY uq_factions_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Factions PvP / diplomatiques. Seedee par 0040.';

SET @prev_sql_mode = @@SESSION.sql_mode;
SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_AUTO_VALUE_ON_ZERO');

INSERT IGNORE INTO factions (id, name, description) VALUES
  (0, 'unaligned',  'Faction par defaut technique : personnages crees avant la migration ou sans allegeance assignee. Equivalent a crepuscule pour le gameplay.'),
  (1, 'lumiere',    'Alliance de l''ordre et de la justice. Defenseurs du jour, gardiens des cites humaines, des forteresses naines, des bois elfiques et des arches des chevaliers-dragons.'),
  (2, 'ombres',     'Disciples des tenebres et du chaos. Hordes orcs, legions demoniaques et necromanciens unifies sous l''ombre de la Lune Noire.'),
  (3, 'crepuscule', 'Sans allegeance. Mercenaires, voyageurs solitaires, eclaireurs hors-la-loi. Libres d''alliances changeantes.');

SET SESSION sql_mode = @prev_sql_mode;

-- ────────────────────────────────────────────────────────────────────────
-- 2) Colonne `characters.faction_str`
-- ────────────────────────────────────────────────────────────────────────

SET @col_exists := (
  SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'faction_str'
);
SET @stmt := IF(@col_exists = 0,
  'ALTER TABLE characters ADD COLUMN faction_str VARCHAR(32) NOT NULL DEFAULT '''' COMMENT ''Identifiant chaine de la faction (cf. factions.name). Vide = unaligned/crepuscule.''',
  'SELECT ''column characters.faction_str already exists, skipping''');
PREPARE altr FROM @stmt; EXECUTE altr; DEALLOCATE PREPARE altr;

-- ────────────────────────────────────────────────────────────────────────
-- 3) Backfill : pre-renseigner `faction_str` pour les personnages existants
--    selon leur race par defaut. Permet a un perso ancien de basculer dans
--    la faction logique sans intervention manuelle.
-- ────────────────────────────────────────────────────────────────────────

UPDATE characters SET faction_str = 'lumiere'
  WHERE faction_str = '' AND race_str IN ('humains', 'nains', 'elfes', 'chevaliers_dragons');
UPDATE characters SET faction_str = 'ombres'
  WHERE faction_str = '' AND race_str IN ('orcs', 'demons');

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
