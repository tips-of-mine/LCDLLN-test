-- Migration 0025 — Paramètres de confidentialité des comptes (visibilité profil)
-- Nouvelle table : account_privacy_settings
-- Permet de gérer : visibilité du profil joueur (public, friends, none)

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS account_privacy_settings (
  account_id BIGINT UNSIGNED NOT NULL,
  profile_visibility ENUM('public','friends','none') NOT NULL DEFAULT 'public'
                     COMMENT 'public=visible à tous, friends=visible aux amis seulement, none=caché',
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (account_id),
  CONSTRAINT fk_privacy_settings_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Paramètres de confidentialité par compte';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
