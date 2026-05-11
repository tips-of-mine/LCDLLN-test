-- 0058 - Wave 11 Persistence Cinematics : table cinematic_seen pour tracker
-- les cutscenes deja vues par un account (evite de rejouer l'intro a chaque
-- login). Cle composite (account_id, sequence_id).

CREATE TABLE IF NOT EXISTS cinematic_seen (
    account_id         BIGINT UNSIGNED NOT NULL,
    sequence_id        INT UNSIGNED    NOT NULL,
    first_seen_ts_ms   BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (account_id, sequence_id),
    KEY idx_account (account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
