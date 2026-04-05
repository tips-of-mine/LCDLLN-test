-- Migration 0006: accounts — locale e-mails + état vérification e-mail
-- Appliquer après 0005_auth_m33_2_reset_email.sql.
-- Aligné avec engine::server::AccountEmailLocale (LocalizedEmail.h) et InMemoryAccountStore (email_verified).
--
-- email_locale : langue des e-mails transactionnels (inscription, reset MDP).
-- email_verified : indicateur rapide ; le détail peut rester dans email_verifications (M33.2).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Idempotent (MySQL ≥ 8.0.29) : reprise après échec partiel sans « Duplicate column ».
ALTER TABLE accounts
  ADD COLUMN IF NOT EXISTS email_locale TINYINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'Transactional email language: 0=en 1=fr 2=es 3=de 4=pt 5=it',
  ADD COLUMN IF NOT EXISTS email_verified TINYINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT '0=not verified 1=verified (M33.2; source of truth may include email_verifications)';

SET FOREIGN_KEY_CHECKS = 1;
