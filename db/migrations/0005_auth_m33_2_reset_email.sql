-- M33.2 — Migration 0005: Password reset tokens + email verification
-- Adds: reset_tokens, email_verifications tables.
-- Apply on top of 0004_auth_mvp_m33_1.sql. Execute in transaction; rollback on error.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- reset_tokens — single-use password reset tokens (UUID-like hex, 1h expiry)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS reset_tokens (
  id         BIGINT UNSIGNED   NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED   NOT NULL
             COMMENT 'references accounts.id',
  token      CHAR(64)          NOT NULL
             COMMENT 'hex-encoded 32-byte random token (single-use)',
  expires_at TIMESTAMP         NOT NULL
             COMMENT 'token expiry: created_at + 1 hour',
  used_at    TIMESTAMP NULL    DEFAULT NULL
             COMMENT 'set when the token is consumed; null = still valid',
  created_at TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_reset_token (token),
  KEY idx_reset_tokens_account (account_id),
  KEY idx_reset_tokens_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Single-use password reset tokens (M33.2)';

-- ---------------------------------------------------------------------------
-- email_verifications — 6-digit verification codes sent on registration
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS email_verifications (
  id          BIGINT UNSIGNED   NOT NULL AUTO_INCREMENT,
  account_id  BIGINT UNSIGNED   NOT NULL
              COMMENT 'references accounts.id',
  code        CHAR(6)           NOT NULL
              COMMENT '6-digit numeric verification code',
  expires_at  TIMESTAMP         NOT NULL
              COMMENT 'code expiry: created_at + 24 hours',
  verified_at TIMESTAMP NULL    DEFAULT NULL
              COMMENT 'set when the account successfully verifies the code',
  created_at  TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_email_verif_account (account_id),
  KEY idx_email_verif_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Email verification codes sent on registration (M33.2)';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
