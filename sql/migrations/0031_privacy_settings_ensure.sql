-- Migration 0031 — Garantie d'existence de account_privacy_settings
-- La migration 0025 pouvait échouer silencieusement si account_id était INT UNSIGNED
-- (incompatible avec accounts.id BIGINT UNSIGNED). Cette migration crée la table
-- de façon idempotente avec le bon type.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS account_privacy_settings (
  account_id         BIGINT UNSIGNED NOT NULL,
  profile_visibility ENUM('public','friends','none') NOT NULL DEFAULT 'public'
                     COMMENT 'public=visible à tous, friends=visible aux amis, none=caché',
  updated_at         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (account_id),
  CONSTRAINT fk_privacy_settings_account_v2
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Paramètres de confidentialité par compte';

SET FOREIGN_KEY_CHECKS = 1;
