-- Migration 0040 — Systeme de factions (race-lockees + sous-maisons)
--
-- Introduit le concept de faction. Les factions sont race-lockees (chaque
-- faction n'est rejoignable que par une race specifique), conformement au
-- design de la Lune Noire :
--
--   * Chevaliers de la Lumiere     (humains)
--   * Chevaliers de la Justice     (humains)
--   * La Lune Noire                (humains)
--   * L'Empire de L'Hynn           (humains)
--   * Dzorak                       (orcs ; cf. 0036 alias lore deja documente)
--   * Demons                       (demons)
--   * Chevaliers-Dragons           (chevaliers_dragons)
--
-- Races sans faction definie : elfes, nains -> faction_str = '' (unaligned).
-- A definir plus tard quand le lore sera arrete pour ces peuples.
--
-- Schema :
--   * Table `factions` (id, name, display_name, race_lock, parent_faction_id,
--     description) avec auto-reference parent_faction_id pour les maisons
--     (ex. "Maison Verdragon" sous "Chevaliers-Dragons").
--   * Colonne `characters.faction_str` (VARCHAR, alignee sur factions.name).
--
-- Note : 'name' est un slug ASCII technique (chevaliers_lumiere, dzorak, ...) ;
-- 'display_name' contient le libelle UTF-8 affiche au joueur (« Chevaliers
-- de la Lumiere »). Le code compare via 'name' (stable) ; le client lit
-- 'display_name' pour l'UI.
--
-- Idempotent : CREATE TABLE IF NOT EXISTS + INSERT IGNORE + guard sur la colonne.
-- Reversible : DROP TABLE factions ; ALTER TABLE characters DROP COLUMN faction_str.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ────────────────────────────────────────────────────────────────────────
-- 1) Table `factions`
-- ────────────────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS factions (
  id                 INT UNSIGNED NOT NULL AUTO_INCREMENT,
  name               VARCHAR(48)  NOT NULL,
  display_name       VARCHAR(96)  NOT NULL DEFAULT '',
  race_lock          VARCHAR(32)  NOT NULL DEFAULT ''
                     COMMENT 'Race exclusivement autorisee (cf. races.name). Vide = pas de verrou.',
  parent_faction_id  INT UNSIGNED NULL DEFAULT NULL
                     COMMENT 'Faction parente (auto-reference). Permet la hierarchie maison < faction.',
  description        VARCHAR(512) NOT NULL DEFAULT '',
  PRIMARY KEY (id),
  UNIQUE KEY uq_factions_name (name),
  KEY idx_factions_race_lock (race_lock),
  CONSTRAINT fk_factions_parent FOREIGN KEY (parent_faction_id)
    REFERENCES factions(id) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Factions race-lockees. Hierarchie maison<faction via parent_faction_id.';

SET @prev_sql_mode = @@SESSION.sql_mode;
SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_AUTO_VALUE_ON_ZERO');

INSERT IGNORE INTO factions (id, name, display_name, race_lock, parent_faction_id, description) VALUES
  (0,  'unaligned',
       'Sans allegeance',
       '',                    NULL,
       'Faction par defaut technique : personnages crees avant cette migration ou races sans faction definie (elfes, nains). Comportement neutre tant qu''aucune affiliation n''est choisie.'),
  (1,  'chevaliers_lumiere',
       'Chevaliers de la Lumiere',
       'humains',             NULL,
       'Ordre saint et chevaleresque jurant fidelite a la Lumiere. Defenseurs des cites humaines, croises contre les ombres.'),
  (2,  'chevaliers_justice',
       'Chevaliers de la Justice',
       'humains',             NULL,
       'Ordre judiciaire arme. Mainteneurs de l''ordre, traqueurs de criminels, gardiens des routes du royaume.'),
  (3,  'lune_noire',
       'La Lune Noire',
       'humains',             NULL,
       'Culte humain qui voue son destin a la Lune Noire. Discrets, sectaires, manipulateurs ; rivaux declares des ordres lumineux.'),
  (4,  'empire_hynn',
       'L''Empire de L''Hynn',
       'humains',             NULL,
       'Empire militaire et bureaucratique. Conquerants pragmatiques, plus politiques que religieux.'),
  (5,  'dzorak',
       'Dzorak',
       'orcs',                NULL,
       'Confederation des hordes orcs. Le mot Dzorak designe a la fois le peuple, la culture et l''alliance des clans (cf. 0036).'),
  (6,  'demons',
       'Demons',
       'demons',              NULL,
       'Legions des plans inferieurs. Tribus rivales unifiees au gre des incursions vers les terres mortelles.'),
  (7,  'chevaliers_dragons',
       'Chevaliers-Dragons',
       'chevaliers_dragons',  NULL,
       'Ordre legendaire des chevaliers lies a la puissance draconique. Race-faction (la race entiere forme une seule faction unique).');

SET SESSION sql_mode = @prev_sql_mode;

-- ────────────────────────────────────────────────────────────────────────
-- 2) Colonne `characters.faction_str`
-- ────────────────────────────────────────────────────────────────────────

SET @col_exists := (
  SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'faction_str'
);
SET @stmt := IF(@col_exists = 0,
  'ALTER TABLE characters ADD COLUMN faction_str VARCHAR(48) NOT NULL DEFAULT '''' COMMENT ''Identifiant chaine de la faction (cf. factions.name). Vide = unaligned.''',
  'SELECT ''column characters.faction_str already exists, skipping''');
PREPARE altr FROM @stmt; EXECUTE altr; DEALLOCATE PREPARE altr;

-- ────────────────────────────────────────────────────────────────────────
-- 3) Backfill : pre-renseigner `faction_str` pour les personnages existants
--    selon leur race. Un humain existant n'a pas encore "choisi" sa faction
--    -> on laisse vide (unaligned) plutot que de le mettre arbitrairement
--    dans Chevaliers de la Lumiere. Le joueur fera son choix au prochain
--    login via une UI de selection (PR ulterieure).
--    Pour les autres races, la faction est unique, on l'attribue.
-- ────────────────────────────────────────────────────────────────────────

UPDATE characters SET faction_str = 'dzorak'
  WHERE faction_str = '' AND race_str = 'orcs';
UPDATE characters SET faction_str = 'demons'
  WHERE faction_str = '' AND race_str = 'demons';
UPDATE characters SET faction_str = 'chevaliers_dragons'
  WHERE faction_str = '' AND race_str = 'chevaliers_dragons';
-- humains, elfes, nains : on laisse vide (selection a faire au prochain login).

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
