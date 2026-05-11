-- 0048 — Phase 5 CMANGOS.23 QuestState : table account_quest_state pour
-- la machine d'etat (None/Available/Accepted/Completed/Rewarded/Failed).
-- Idempotent. PK composite (account_id, quest_id).

CREATE TABLE IF NOT EXISTS account_quest_state (
  account_id  BIGINT UNSIGNED NOT NULL,
  quest_id    INT UNSIGNED    NOT NULL,
  status      TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (account_id, quest_id),
  KEY ix_account_quest_state_account (account_id)
);
