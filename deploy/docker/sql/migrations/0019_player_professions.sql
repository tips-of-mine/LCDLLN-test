-- M36.2: player profession skill tracking
-- Records the known professions and skill levels per character.
-- This table is used by Linux/MySQL deployments; WIN32 uses INI-backed persistence.

START TRANSACTION;

CREATE TABLE IF NOT EXISTS player_professions (
    id                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    character_id      BIGINT UNSIGNED NOT NULL COMMENT 'FK → characters.id',
    profession_key    VARCHAR(64)     NOT NULL COMMENT 'Stable profession identifier (e.g. blacksmithing)',
    skill_level       SMALLINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Current skill level (1-300)',
    is_primary        TINYINT(1)      NOT NULL DEFAULT 0 COMMENT '1 when this is a primary profession slot',
    learned_at        DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_character_profession (character_id, profession_key),
    KEY idx_profession_character (character_id),
    CONSTRAINT fk_professions_character FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='M36.2 — per-character profession and skill level tracking';

COMMIT;
