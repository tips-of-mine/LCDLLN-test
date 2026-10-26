-- Migration 0078 — État complet du personnage en base (ETAPE2.1, chantier 3.1).
--
-- Règle : la base est la seule source de vérité de l'état d'un personnage,
-- écrite uniquement par le shard propriétaire (verrou de la migration 0077).
-- Le fichier persistence/characters/character_<id>.ini n'est plus écrit quand
-- le shard a une base ; il n'est relu qu'une fois, pour importer un personnage
-- qui n'a pas encore de ligne character_progress.
--
-- Déjà en base avant cette migration : position et niveau (characters),
-- bourse (player_wallet), sac (character_inventory), équipement
-- (character_equipment).
--
-- character_progress : une ligne par personnage.
--   zone_id, experience_points, current_health, max_health, chat_moderator_role :
--     valeurs scalaires.
--   progress_payload : quêtes, joueurs ignorés, barre d'action, ceinture,
--     expirations des gâteaux, compétences de classe, métiers, au format
--     clé=valeur du fichier .ini (lu par ApplyCharacterProgressPayload).
--   Présence de la ligne = le personnage est entièrement en base.
--
-- character_mailbox : courrier en attente (or et/ou objet), ajouté par n'importe
--   quel shard (remboursement d'enchère, objet gagné, vente expirée) sans lire
--   l'état du destinataire. Le shard propriétaire le fusionne à la connexion et
--   supprime les lignes lues dans la transaction de sauvegarde suivante.
--
-- Idempotent (CREATE TABLE IF NOT EXISTS).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS character_progress (
    character_id        BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
    format_version      INT UNSIGNED    NOT NULL DEFAULT 1 COMMENT 'version du format de progress_payload',
    zone_id             INT UNSIGNED    NOT NULL DEFAULT 1,
    experience_points   INT UNSIGNED    NOT NULL DEFAULT 0,
    current_health      INT UNSIGNED    NOT NULL DEFAULT 100,
    max_health          INT UNSIGNED    NOT NULL DEFAULT 100,
    chat_moderator_role TINYINT(1)      NOT NULL DEFAULT 0,
    progress_payload    MEDIUMTEXT      NOT NULL COMMENT 'quêtes, ignorés, barres, ceinture, gâteaux, compétences, métiers (clé=valeur)',
    updated_at          DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (character_id),
    CONSTRAINT fk_character_progress_character
        FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.1 — état du personnage hors bourse/sac/équipement/position';

CREATE TABLE IF NOT EXISTS character_mailbox (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    character_id BIGINT UNSIGNED NOT NULL COMMENT 'destinataire (characters.id)',
    gold         INT UNSIGNED    NOT NULL DEFAULT 0,
    item_id      INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT '0 = pas d''objet',
    quantity     INT UNSIGNED    NOT NULL DEFAULT 0,
    source       VARCHAR(32)     NOT NULL DEFAULT '' COMMENT 'origine (auction_refund, auction_won…)',
    created_at   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_character_mailbox_character (character_id, id),
    CONSTRAINT fk_character_mailbox_character
        FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.1 — courrier en attente, fusionné à la connexion';

SET FOREIGN_KEY_CHECKS = 1;
