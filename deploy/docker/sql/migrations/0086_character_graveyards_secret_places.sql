-- Lore v1.0.114 — exploit des 666 cimetières et lieux secrets découverts, PAR
-- PERSONNAGE (décisions du propriétaire du 13/11/2026, LORE_BD §16.41).
--
-- character_graveyard_visits : cimetières des Royaumes visités (entrée du personnage
--   vivant dans un rayon de 15 m, ou réapparition à ce cimetière après une mort).
--   graveyard_id = jeton gy=<id> de zones/<cellule>/respawn_points.txt (id de la
--   ville ou du hameau, isole_<cellule> pour un cimetière isolé).
-- character_secret_discoveries : lieux secrets (world/<monde>/secret_places.json)
--   découverts en entrant dans leur rayon ; révélés sur la carte de CE personnage.
-- Écrites par le SHARD (INSERT IGNORE dans la transaction de sauvegarde du
-- personnage), lues à l'entrée en jeu.
--
-- Exploit exploit_pelerin_des_666_tombes (scope character, metric_source
-- graveyards_visited, seuil 666) : débloqué par le shard (INSERT IGNORE dans
-- character_exploit_unlocks) quand les 666 cimetières sont visités. Titre de
-- joueur « Pèlerin des Tombes » (pas encore de colonne de titre au catalogue :
-- mentionné dans la description).
-- Idempotent : CREATE TABLE IF NOT EXISTS, INSERT IGNORE.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS character_graveyard_visits (
  character_id BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
  graveyard_id VARCHAR(96)     NOT NULL COMMENT 'identifiant gy=<id> de respawn_points.txt',
  visited_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (character_id, graveyard_id),
  CONSTRAINT fk_character_graveyard_visits_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Cimetieres des Royaumes visites par personnage (exploit des 666 cimetieres)';

CREATE TABLE IF NOT EXISTS character_secret_discoveries (
  character_id  BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
  place_id      VARCHAR(96)     NOT NULL COMMENT 'id du lieu dans secret_places.json',
  discovered_at TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (character_id, place_id),
  CONSTRAINT fk_character_secret_discoveries_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Lieux secrets decouverts par personnage (carte du joueur)';

INSERT IGNORE INTO exploits (code, title, description, scope, metric_source, threshold_value, sort_order, is_secret)
VALUES
  ('exploit_pelerin_des_666_tombes',
   'Le Pèlerin des Six Cent Soixante-Six Tombes',
   'Exploration : visiter les 666 cimetières des Royaumes (y entrer vivant ou y réapparaître). Titre : « Pèlerin des Tombes ».',
   'character', 'graveyards_visited', 666, 401, 0);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
