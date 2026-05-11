-- Migration 0006: accounts — locale e-mails + état vérification e-mail
-- Appliquer après 0005_auth_m33_2_reset_email.sql.
-- Aligné avec engine::server::AccountEmailLocale (LocalizedEmail.h) et InMemoryAccountStore (email_verified).
--
-- email_locale : langue des e-mails transactionnels (inscription, reset MDP).
-- email_verified : indicateur rapide ; le détail peut rester dans email_verifications (M33.2).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Idempotent : sans ADD COLUMN IF NOT EXISTS (compatibilité parseur).
SET @m6_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_locale'
);
SET @m6_s1 := IF(@m6_c1 = 0,
  'ALTER TABLE accounts ADD COLUMN email_locale TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''Transactional email language: 0=en 1=fr 2=es 3=de 4=pt 5=it''',
  'SELECT 1');
PREPARE m6_p1 FROM @m6_s1;
EXECUTE m6_p1;
DEALLOCATE PREPARE m6_p1;

SET @m6_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_verified'
);
SET @m6_s2 := IF(@m6_c2 = 0,
  'ALTER TABLE accounts ADD COLUMN email_verified TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''0=not verified 1=verified (M33.2)''',
  'SELECT 1');
PREPARE m6_p2 FROM @m6_s2;
EXECUTE m6_p2;
DEALLOCATE PREPARE m6_p2;

SET FOREIGN_KEY_CHECKS = 1;
