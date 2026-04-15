-- M36.2 — Player professions and crafting skill levels.
-- WIN32 shard uses file-backed INI persistence; this schema targets UNIX MySQL deployments.
CREATE TABLE IF NOT EXISTS player_professions (
  id              BIGINT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
  character_id    BIGINT UNSIGNED NOT NULL,
  profession_id   VARCHAR(64)     NOT NULL,
  skill_level     SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  is_primary      TINYINT(1)      NOT NULL DEFAULT 1,
  UNIQUE KEY uk_char_prof (character_id, profession_id),
  KEY idx_char_id (character_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
