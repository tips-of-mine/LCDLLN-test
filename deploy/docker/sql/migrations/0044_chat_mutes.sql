-- 0044 — Phase 2 CMANGOS.01 Chat (sub-PR 1) : table chat_mutes pour
-- ChatGate. Un compte peut être muté jusqu'à `until_ts` (epoch ms UTC),
-- avec une raison libre (audit). Idempotent.

CREATE TABLE IF NOT EXISTS chat_mutes (
  account_id  BIGINT UNSIGNED NOT NULL,
  until_ts    BIGINT NOT NULL,           -- epoch ms UTC ; 0 = mute permanent
  reason      VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (account_id),
  KEY ix_chat_mutes_until (until_ts)
);
