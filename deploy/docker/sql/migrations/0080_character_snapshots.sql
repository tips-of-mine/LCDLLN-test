-- Migration 0080 — Instantanés restaurables des personnages (ETAPE2.6, chantier 3.6).
--
-- character_snapshots : à chaque déconnexion, le shard propriétaire enregistre
--   une copie complète et datée de l'état du personnage (même texte clé=valeur
--   que l'ancien fichier .ini : position, bourse, sac, équipement, quêtes,
--   métiers…). Les 20 plus récents sont gardés par personnage.
--
-- character_restore_requests : demande de restauration posée par un MJ depuis
--   le master (/restore <personnage> <id>). Le master n'écrit jamais l'état d'un
--   personnage : la demande est appliquée par le shard propriétaire à la
--   prochaine connexion du personnage, dans sa transaction de sauvegarde
--   (applied_at renseigné). Une seule demande en attente par personnage.
--
-- Idempotent (CREATE TABLE IF NOT EXISTS).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS character_snapshots (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    character_id BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
    reason       VARCHAR(32)     NOT NULL DEFAULT '' COMMENT 'logout, kick…',
    shard_id     INT UNSIGNED    NOT NULL DEFAULT 0,
    payload      MEDIUMTEXT      NOT NULL COMMENT 'état complet, format clé=valeur (SerializeCharacterState)',
    created_at   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_character_snapshots_character (character_id, id),
    CONSTRAINT fk_character_snapshots_character
        FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.6 — copies datées de l''état des personnages (support joueur)';

CREATE TABLE IF NOT EXISTS character_restore_requests (
    character_id         BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
    snapshot_id          BIGINT UNSIGNED NOT NULL COMMENT 'character_snapshots.id',
    requested_by_account BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'compte du MJ',
    requested_at         DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    applied_at           DATETIME        NULL DEFAULT NULL COMMENT 'NULL = en attente',
    PRIMARY KEY (character_id),
    CONSTRAINT fk_character_restore_requests_character
        FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.6 — restaurations demandées par un MJ, appliquées par le shard';

SET FOREIGN_KEY_CHECKS = 1;
