-- Migration 0022 — Statistiques de jeu par personnage par serveur
-- Le serveur de jeu met à jour total_play_seconds au fil des sessions.
-- Le portail web lit ces données en lecture seule.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS character_stats (
  character_id       BIGINT UNSIGNED NOT NULL,
  server_id          INT UNSIGNED    NOT NULL,
  total_play_seconds BIGINT UNSIGNED NOT NULL DEFAULT 0
                     COMMENT 'Secondes de jeu cumulées sur ce serveur',
  last_seen          DATETIME        NULL DEFAULT NULL
                     COMMENT 'Dernière fois que le personnage a été vu sur ce serveur',
  PRIMARY KEY (character_id, server_id),
  CONSTRAINT fk_char_stats_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT fk_char_stats_server
    FOREIGN KEY (server_id) REFERENCES game_servers (server_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Temps de jeu cumulé et dernière activité par (personnage, serveur)';

SET FOREIGN_KEY_CHECKS = 1;
