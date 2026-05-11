-- 0051 - Wave 5 Persistence GameEvents (Phase 5.31b) : table game_events
-- pour persister la definition des events saisonniers (Halloween, Winter
-- Veil, etc.). Au boot, le master prefere charger depuis la DB plutot
-- que le seed hardcode. Si la table est vide ou la DB indisponible, le
-- seed in-memory du handler s'applique (fallback degrade).
--
-- duration_ms / recur_ms : 0 = one-shot / pas de recurrence.
-- requires_lunar_phase_mask : bitmask 16 bits (0xFFFF = pas de gate).
--
-- Le seed V1 est insere via INSERT IGNORE pour rester idempotent : les
-- ids 1..5 correspondent aux 5 events seedees par SeedV1Events du
-- GameEventHandler (Halloween, Winter Veil, Lunar Festival, Midsummer
-- Fire, Nuit de la Lune Noire).

CREATE TABLE IF NOT EXISTS game_events (
    event_id INT UNSIGNED NOT NULL PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    start_ts_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    duration_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    recur_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    requires_lunar_phase_mask SMALLINT UNSIGNED NOT NULL DEFAULT 65535
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Seed V1 (5 events). INSERT IGNORE : idempotent sur re-application de
-- la migration. Les valeurs sont identiques aux constantes hardcodees
-- dans GameEventHandler::SeedV1Events ; si la DB est vide (table cree
-- mais pas seedee), le handler fallback sur son seed in-memory.
INSERT IGNORE INTO game_events
    (event_id, name, start_ts_ms, duration_ms, recur_ms, requires_lunar_phase_mask)
VALUES
    (1, 'Halloween',               1791849600000, 1209600000,  31536000000, 65535),
    (2, 'Winter Veil',              1797206400000, 1814400000,  31536000000, 65535),
    (3, 'Lunar Festival',           1769817600000, 1209600000,  31536000000, 65535),
    (4, 'Midsummer Fire Festival',  1781913600000, 1209600000,  31536000000, 65535),
    (5, 'Nuit de la Lune Noire',    0,             9223372036854775807, 0, 49153);
