-- Migration 0021 — Colonnes profil joueur + table email_change_tokens
-- Appliquer après 0020_accounts_role.sql

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- first_name
SET @m21_a := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'first_name'
);
SET @m21_sa := IF(@m21_a = 0,
  'ALTER TABLE accounts ADD COLUMN first_name VARCHAR(100) NOT NULL DEFAULT \'\'',
  'SELECT 1');
PREPARE m21_pa FROM @m21_sa; EXECUTE m21_pa; DEALLOCATE PREPARE m21_pa;

-- last_name
SET @m21_b := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'last_name'
);
SET @m21_sb := IF(@m21_b = 0,
  'ALTER TABLE accounts ADD COLUMN last_name VARCHAR(100) NOT NULL DEFAULT \'\'',
  'SELECT 1');
PREPARE m21_pb FROM @m21_sb; EXECUTE m21_pb; DEALLOCATE PREPARE m21_pb;

-- email_pending (nouvel email en attente de validation)
SET @m21_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending'
);
SET @m21_sc := IF(@m21_c = 0,
  'ALTER TABLE accounts ADD COLUMN email_pending VARCHAR(256) NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pc FROM @m21_sc; EXECUTE m21_pc; DEALLOCATE PREPARE m21_pc;

-- profile_visibility
SET @m21_d := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'profile_visibility'
);
SET @m21_sd := IF(@m21_d = 0,
  'ALTER TABLE accounts ADD COLUMN profile_visibility ENUM(\'public\',\'friends\',\'private\') NOT NULL DEFAULT \'public\'',
  'SELECT 1');
PREPARE m21_pd FROM @m21_sd; EXECUTE m21_pd; DEALLOCATE PREPARE m21_pd;

-- parental_email
SET @m21_e := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_email'
);
SET @m21_se := IF(@m21_e = 0,
  'ALTER TABLE accounts ADD COLUMN parental_email VARCHAR(256) NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pe FROM @m21_se; EXECUTE m21_pe; DEALLOCATE PREPARE m21_pe;

-- parental_consent_at
SET @m21_f := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_consent_at'
);
SET @m21_sf := IF(@m21_f = 0,
  'ALTER TABLE accounts ADD COLUMN parental_consent_at TIMESTAMP NULL DEFAULT NULL',
  'SELECT 1');
PREPARE m21_pf FROM @m21_sf; EXECUTE m21_pf; DEALLOCATE PREPARE m21_pf;

-- Table email_change_tokens
CREATE TABLE IF NOT EXISTS email_change_tokens (
  id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED NOT NULL,
  new_email  VARCHAR(256)    NOT NULL,
  code       CHAR(6)         NOT NULL COMMENT '6 chiffres',
  expires_at TIMESTAMP       NOT NULL,
  used_at    TIMESTAMP       NULL DEFAULT NULL,
  created_at TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_email_change_account (account_id),
  KEY idx_email_change_expires (expires_at),
  KEY idx_email_change_lookup (account_id, code),
  CONSTRAINT fk_email_change_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Tokens de changement d''email — code 6 chiffres envoyé au nouvel email';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
