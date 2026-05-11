-- 0045 — Phase 3 CMANGOS.18 Mails : tables `mail` + `mail_items` pour
-- la messagerie in-game (sender/receiver, items, copperGold, COD,
-- expiration, state). Idempotent.

CREATE TABLE IF NOT EXISTS mail (
  mail_id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  sender_account_id    BIGINT UNSIGNED NOT NULL,
  receiver_account_id  BIGINT UNSIGNED NOT NULL,
  subject              VARCHAR(255)  NOT NULL DEFAULT '',
  body                 MEDIUMTEXT    NOT NULL,
  copper_gold          BIGINT UNSIGNED NOT NULL DEFAULT 0,
  copper_cod           BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sent_ts_ms           BIGINT UNSIGNED NOT NULL,
  expires_ts_ms        BIGINT UNSIGNED NOT NULL DEFAULT 0,
  state                TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (mail_id),
  KEY ix_mail_receiver (receiver_account_id),
  KEY ix_mail_expires  (expires_ts_ms)
);

CREATE TABLE IF NOT EXISTS mail_items (
  mail_id              BIGINT UNSIGNED NOT NULL,
  slot                 SMALLINT UNSIGNED NOT NULL,
  item_template_id     INT UNSIGNED NOT NULL,
  count                INT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (mail_id, slot),
  KEY ix_mail_items_mail (mail_id)
);
