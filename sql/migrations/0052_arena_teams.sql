-- 0052 - Wave 5 Persistence Arena (Phase 5.21b) : table arena_teams
-- pour persister la progression ELO + stats hebdo/saison des teams
-- arene. Idempotent.
--
-- account_id_owner : compte qui a cree l'equipe. team_id reste un
-- identifiant logique partage par tous les players V1 (le seed
-- starter du handler reutilise les ids 1/2/3 pour 2v2/3v3/5v5
-- indistinctement). En sub-PR future on basculera vers une cle
-- composite (account_id_owner, local_team_id) ou un team_id global
-- AUTO_INCREMENT pour eviter la collision inter-account.
--
-- size : 2 / 3 / 5 (mode arena). rating : ELO courant.

CREATE TABLE IF NOT EXISTS arena_teams (
    team_id INT UNSIGNED NOT NULL,
    account_id_owner BIGINT UNSIGNED NOT NULL,
    name VARCHAR(64) NOT NULL,
    size TINYINT UNSIGNED NOT NULL,
    rating INT UNSIGNED NOT NULL DEFAULT 1500,
    weekly_games INT UNSIGNED NOT NULL DEFAULT 0,
    weekly_wins INT UNSIGNED NOT NULL DEFAULT 0,
    season_games INT UNSIGNED NOT NULL DEFAULT 0,
    season_wins INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (account_id_owner, team_id),
    INDEX idx_team_size (size),
    INDEX idx_rating (rating)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
