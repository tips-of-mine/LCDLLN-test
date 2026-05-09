-- 0046 — Phase 5 CMANGOS.32 GmTickets : table `gm_tickets` pour la queue
-- de support GM (reporter, body, etat, GM assigne, timestamps). Idempotent.

CREATE TABLE IF NOT EXISTS gm_tickets (
  ticket_id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  reporter_account_id  BIGINT UNSIGNED NOT NULL,
  body                 MEDIUMTEXT      NOT NULL,
  created_ts_ms        BIGINT UNSIGNED NOT NULL,
  resolved_ts_ms       BIGINT UNSIGNED NOT NULL DEFAULT 0,
  assigned_gm          BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state                TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (ticket_id),
  KEY ix_gm_tickets_state    (state),
  KEY ix_gm_tickets_assigned (assigned_gm),
  KEY ix_gm_tickets_reporter (reporter_account_id)
);
