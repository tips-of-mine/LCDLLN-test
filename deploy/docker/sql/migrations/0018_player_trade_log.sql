-- M35.3: player trade audit log
-- Records every completed direct player-to-player trade for anti-scam and customer support.
-- Each completed trade produces exactly two rows: one for each participant.

START TRANSACTION;

CREATE TABLE IF NOT EXISTS player_trade_log (
    id                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    trade_id          CHAR(36)        NOT NULL COMMENT 'UUID shared by both rows of the same trade',
    character_id      BIGINT UNSIGNED NOT NULL COMMENT 'FK → characters.id (the giving side)',
    partner_id        BIGINT UNSIGNED NOT NULL COMMENT 'FK → characters.id (the receiving side)',
    gold_given        INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT 'Gold transferred from this character',
    items_given_json  TEXT            NOT NULL COMMENT 'JSON array [{item_id, quantity}] given by this character',
    created_at        DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_trade_log_character (character_id),
    KEY idx_trade_log_partner   (partner_id),
    KEY idx_trade_log_trade_id  (trade_id),
    KEY idx_trade_log_created   (created_at),
    CONSTRAINT fk_trade_log_character FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
    CONSTRAINT fk_trade_log_partner   FOREIGN KEY (partner_id)   REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='M35.3 — audit trail for all completed direct player trades';

COMMIT;
