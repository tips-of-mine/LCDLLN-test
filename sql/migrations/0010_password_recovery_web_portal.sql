-- La base active est celle de la connexion du master (pas de USE codé en dur).

CREATE TABLE IF NOT EXISTS account_recovery_profiles (
  account_id BIGINT UNSIGNED NOT NULL,
  birth_date DATE NULL,
  street_address VARCHAR(255) NULL,
  city VARCHAR(128) NULL,
  postal_code VARCHAR(32) NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (account_id),
  CONSTRAINT fk_account_recovery_profiles_account
    FOREIGN KEY (account_id) REFERENCES accounts(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS account_recovery_secret_questions (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED NOT NULL,
  question VARCHAR(255) NOT NULL,
  answer_hash CHAR(64) NOT NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_account_recovery_secret_questions_account (account_id),
  CONSTRAINT fk_account_recovery_secret_questions_account
    FOREIGN KEY (account_id) REFERENCES accounts(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS account_password_reset_tokens (
  token_hash CHAR(64) NOT NULL,
  account_id BIGINT UNSIGNED NOT NULL,
  expires_at TIMESTAMP NOT NULL,
  used_at TIMESTAMP NULL DEFAULT NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (token_hash),
  KEY ix_account_password_reset_tokens_account (account_id),
  KEY ix_account_password_reset_tokens_expires_at (expires_at),
  CONSTRAINT fk_account_password_reset_tokens_account
    FOREIGN KEY (account_id) REFERENCES accounts(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
