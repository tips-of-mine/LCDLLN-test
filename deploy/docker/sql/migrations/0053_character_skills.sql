-- 0053 - Wave 5 Persistence Skills (Phase 4.39b) : table character_skills
-- pour persister la progression per-character (V1 cle = account_id en
-- attendant le multi-character full ; sub-PR future bascule vers
-- character_id quand le CharacterStore sera branche). Idempotent.
--
-- value : valeur courante du skill (0..cap). cap : plafond apprenable.
-- bonus : modifier temporaire (item bonus, buff). Le runtime applique
-- value clamp cap a chaque update via le handler.
--
-- Note V1 : le handler in-memory n'expose pas character_id (le master
-- raisonne par account). On enregistre donc account_id dans la colonne
-- character_id pour minimiser le refactor. Le rename de la colonne
-- arrivera quand le CharacterStore sera cable (sub-PR Wave 6).

CREATE TABLE IF NOT EXISTS character_skills (
    character_id BIGINT UNSIGNED NOT NULL,
    skill_id INT UNSIGNED NOT NULL,
    value INT UNSIGNED NOT NULL DEFAULT 0,
    cap INT UNSIGNED NOT NULL DEFAULT 75,
    bonus INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, skill_id),
    INDEX idx_character (character_id),
    INDEX idx_skill (skill_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
