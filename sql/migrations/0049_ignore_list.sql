-- 0049 — Phase 3 CMANGOS.25 IgnoreList : table account_ignore_list
-- (chaque ligne = 1 paire owner -> target ignored). Idempotent.
-- Limite logique 50 ignored par account (verifiee dans IgnoreListManager).

CREATE TABLE IF NOT EXISTS account_ignore_list (
  owner_account_id    BIGINT UNSIGNED NOT NULL,
  target_account_id   BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (owner_account_id, target_account_id),
  KEY ix_ignore_list_owner (owner_account_id)
);
