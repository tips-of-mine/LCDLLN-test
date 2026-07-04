-- 0056 - Wave 5 Persistence BattleGround (Phase 5.10b) : table
-- bg_match_history pour persister les resultats de matchs joues. Le
-- BattleGroundHandler insere une ligne via MysqlBattleGroundStore::InsertMatch
-- a chaque MatchEnd push. Read API LoadRecent(limit) est expose pour
-- de futures UI leaderboard / stats (out-of-scope de cette PR cote
-- wire / opcode).
--
-- match_id : auto-increment, sert d'identifiant unique persistant
-- (distinct du matchId in-memory atomic compteur qui reset au reboot).
-- bg_type : 1=Gorge de Feyhin, 2=Bassin des Ombres, 3=Vallee Gelee V1.
-- winner_faction : 0=Alliance, 1=Horde, 2=Draw.
-- started_at_unix_ms : timestamp de creation du match (avant Push Start).
-- duration_sec : duree totale entre Start et End push.
--
-- Pas de seed : la table reste vide jusqu'au premier MatchEnd execute
-- par le handler. Une DB vide est un etat valide (premier boot).

CREATE TABLE IF NOT EXISTS bg_match_history (
    match_id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    bg_type             SMALLINT UNSIGNED NOT NULL,
    map_name            VARCHAR(64) NOT NULL,
    alliance_score      INT UNSIGNED NOT NULL DEFAULT 0,
    horde_score         INT UNSIGNED NOT NULL DEFAULT 0,
    winner_faction      TINYINT UNSIGNED NOT NULL DEFAULT 2,
    duration_sec        INT UNSIGNED NOT NULL DEFAULT 0,
    started_at_unix_ms  BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (match_id),
    KEY idx_started (started_at_unix_ms),
    KEY idx_bg_type (bg_type)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
