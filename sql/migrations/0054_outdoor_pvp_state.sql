-- 0054 - Wave 5 Persistence OutdoorPvP (Phase 5.36b) : table
-- outdoor_pvp_state pour persister l'etat des objectifs par zone et
-- les scores Alliance/Horde. Idempotent.
--
-- (zone_id, objective_id) compose la cle primaire ; les scores par
-- faction sont stockes dans une table side outdoor_pvp_scores.
-- owner = 0 (Alliance), 1 (Horde), 0xFF (neutre) ; capturePct 0..100 ;
-- capturing_by = faction qui progresse (0xFF si aucune).

CREATE TABLE IF NOT EXISTS outdoor_pvp_state (
    zone_id INT UNSIGNED NOT NULL,
    objective_id INT UNSIGNED NOT NULL,
    owner TINYINT UNSIGNED NOT NULL DEFAULT 255,
    capture_pct INT UNSIGNED NOT NULL DEFAULT 0,
    capturing_by TINYINT UNSIGNED NOT NULL DEFAULT 255,
    PRIMARY KEY (zone_id, objective_id),
    INDEX idx_zone (zone_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS outdoor_pvp_scores (
    zone_id INT UNSIGNED NOT NULL,
    faction TINYINT UNSIGNED NOT NULL,
    score INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (zone_id, faction)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
