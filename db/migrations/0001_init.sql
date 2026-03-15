-- M21.2 — Migration 0001: schéma initial (équivalent db/schema.sql, sans CREATE DATABASE).
-- À exécuter dans une base existante (ex: lcdlln_master). Chaque migration dans une transaction si possible.
-- Le checksum SHA-256 du fichier est enregistré dans schema_version par l'applicateur.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ---------------------------------------------------------------------------
-- accounts (auth) — aligné tickets/docs/accounts_schema_v1.md
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS accounts (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  email VARCHAR(256) NOT NULL,
  login VARCHAR(64) NOT NULL,
  password_hash VARCHAR(512) NOT NULL,
  account_status TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=active, 1=banned, 2=locked',
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_accounts_email (email),
  UNIQUE KEY uq_accounts_login (login)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- sessions (optionnel si persistant)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS sessions (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED NOT NULL,
  session_token VARCHAR(128) NOT NULL COMMENT 'opaque session identifier',
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMP NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_sessions_token (session_token),
  KEY ix_sessions_account_id (account_id),
  KEY ix_sessions_expires_at (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- characters (placeholder, max 5 par compte — slot 0..4)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS characters (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id BIGINT UNSIGNED NOT NULL,
  slot TINYINT UNSIGNED NOT NULL COMMENT '0..4, max 5 characters per account',
  name VARCHAR(64) NOT NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_characters_account_slot (account_id, slot),
  KEY ix_characters_account_id (account_id),
  CONSTRAINT chk_characters_slot CHECK (slot < 5)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- shards (pour M22 — registre shards)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS shards (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT,
  name VARCHAR(64) NOT NULL,
  endpoint VARCHAR(256) NULL COMMENT 'host:port or URL',
  max_capacity INT UNSIGNED NOT NULL DEFAULT 0,
  current_load INT UNSIGNED NOT NULL DEFAULT 0,
  last_heartbeat TIMESTAMP NULL,
  status TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=registering, 1=online, 2=degraded, 3=offline',
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_shards_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------------
-- schema_version (source of truth des migrations)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS schema_version (
  version INT UNSIGNED NOT NULL,
  applied_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  checksum CHAR(64) NOT NULL COMMENT 'SHA-256 hex du script appliqué',
  PRIMARY KEY (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

SET FOREIGN_KEY_CHECKS = 1;
