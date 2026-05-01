-- Migration 0036 — Seed de la table `races` (creee vide par 0004)
--
-- Suite du fix 'Character creation failed' (cf. 0035) : la creation insere
-- aussi `race_id = 0` en dur dans `characters`, qui declenche la contrainte FK
-- `fk_characters_race_id` (`races.id`). La table `races` est elle aussi vide
-- de seeds : sans ligne `id = 0` ('default'), l'INSERT echoue silencieusement
-- cote DB et le client recoit l'erreur generique 'character creation failed'.
--
-- Seeder une ligne `id = 0, name = 'default'` permet a l'INSERT de passer
-- sans modifier le code. Le caractere reel choisit ensuite sa race via
-- `race_str` (champ texte ajoute par 0033, ex. 'humains' / 'elfes' / etc.).
--
-- Note : `races.id` est INT UNSIGNED AUTO_INCREMENT, donc inserer la valeur
-- explicite 0 necessite `SET sql_mode = NO_AUTO_VALUE_ON_ZERO` le temps de
-- l'insertion. On le restaure ensuite.
--
-- Idempotent : INSERT IGNORE sur la PRIMARY KEY (id).
-- Reversible : DELETE FROM races WHERE id = 0 AND name = 'default'.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

SET @prev_sql_mode = @@SESSION.sql_mode;
SET SESSION sql_mode = CONCAT(@@SESSION.sql_mode, ',NO_AUTO_VALUE_ON_ZERO');

INSERT IGNORE INTO races (id, name, description) VALUES
  (0, 'default', 'Race par defaut, choisie quand le client ne specifie pas de race_id numerique.');

SET SESSION sql_mode = @prev_sql_mode;

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
