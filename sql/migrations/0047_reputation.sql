-- 0047 — Phase 3 CMANGOS.24 Reputation : table account_reputation pour
-- persister la valeur de reputation par (account, faction). Idempotent.
--
-- Une ligne par (account_id, faction_id), value -42000..41999 (signed int).
-- Le ReputationManager runtime gere les bornes/clamp ; cette table est
-- juste le store. Les regles de spillover restent code-driven (pas
-- en DB) car elles dependent de la version de jeu, pas de l'account.

CREATE TABLE IF NOT EXISTS account_reputation (
  account_id  BIGINT UNSIGNED NOT NULL,
  faction_id  INT UNSIGNED   NOT NULL,
  value       INT             NOT NULL DEFAULT 0,
  PRIMARY KEY (account_id, faction_id),
  KEY ix_account_reputation_account (account_id)
);
