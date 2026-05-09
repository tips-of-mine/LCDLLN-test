-- Migration 0036 — Seed de la table `races` (creee vide par 0004)
--
-- Suite du fix 'Character creation failed' (cf. 0035) :
--
-- 1) `CharacterCreateHandler.cpp` insere `race_id = 0` en dur dans
--    `characters`, qui declenche la contrainte FK `fk_characters_race_id`
--    (`races.id`). La table `races` etant vide, l'INSERT echoue et le client
--    recoit l'erreur generique 'character creation failed'.
--
-- 2) Le jeu definit 6 races jouables, alignees avec les ids strings utilises
--    deja par `game/data/ui/races/<id>/theme.json` et les libelles
--    `auth.character_select.race.<id>` dans fr.json/en.json :
--      * humains              (humain)
--      * elfes                (elfe)
--      * orcs                 (alias 'dzorak' dans le lore)
--      * nains                (nain)
--      * demons               (demon, sans accent pour glyph Windlass)
--      * chevaliers_dragons   (NOUVEAU, ajoute par cette migration)
--
-- Strategie : seeder 7 lignes (id=0 'default' + les 6 races du jeu). L'id=0
-- 'default' permet au code existant (race_id=0) de passer la FK sans changement.
-- Les ids 1..6 fixes permettront a une future evolution du code de mapper
-- race_str -> race_id sans collision de PK auto-incremente.
--
-- Note : `races.id` est INT UNSIGNED AUTO_INCREMENT, donc inserer la valeur
-- explicite 0 necessite `SET sql_mode = NO_AUTO_VALUE_ON_ZERO` le temps de
-- l'insertion. On le restaure ensuite.
--
-- Idempotent : INSERT IGNORE sur la PRIMARY KEY (id) et UNIQUE KEY (name).
-- Reversible : DELETE FROM races WHERE id IN (0, 1, 2, 3, 4, 5, 6).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

SET @prev_sql_mode = @@SESSION.sql_mode;
SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_AUTO_VALUE_ON_ZERO');

INSERT IGNORE INTO races (id, name, description) VALUES
  (0, 'default',            'Race par defaut, choisie quand le client ne specifie pas de race_id numerique.'),
  (1, 'humains',            'Race humaine, polyvalente et adaptable, equilibree sur toutes les voies.'),
  (2, 'elfes',              'Peuple ancien et magique, agile et longeve, proche des forces de la nature.'),
  (3, 'orcs',               'Peuple guerrier robuste (alias dzorak dans le lore de la Lune Noire).'),
  (4, 'nains',              'Peuple des montagnes, resistant et tenace, maitre du metal et de la pierre.'),
  (5, 'demons',             'Entites des plans inferieurs, puissants en magie sombre et combat brutal.'),
  (6, 'chevaliers_dragons', 'Ordre des chevaliers-dragons : guerriers lies a la puissance draconique.');

SET SESSION sql_mode = @prev_sql_mode;

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
