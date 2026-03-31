-- M39.x — Character creation: forbidden names moderation list

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS forbidden_character_names (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  name VARCHAR(64) NOT NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_forbidden_character_names_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO forbidden_character_names (name) VALUES
  ('admin'),
  ('admins'),
  ('administrator'),
  ('administrateur'),
  ('system'),
  ('support'),
  ('supportclient'),
  ('moderator'),
  ('mod'),
  ('modo'),
  ('gm'),
  ('game_master'),
  ('gamemaster'),
  ('mj'),
  ('master'),
  ('maitre'),
  ('staff'),
  ('dev'),
  ('developer'),
  ('developpeur'),
  ('test'),
  ('tester'),
  ('null'),
  ('undefined'),
  ('root'),
  ('console'),
  ('server'),
  ('serveur'),
  ('shard'),
  ('lcdlln');

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
