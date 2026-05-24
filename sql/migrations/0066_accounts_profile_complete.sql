-- Migration 0066 — Complète les colonnes de profil sur `accounts` (résout le doublon 0023).
--
-- Contexte : deux migrations portaient le numéro 0023 (0023_accounts_profile.sql et
-- 0023_accounts_profile_fields.sql). Le MigrationRunner n'appliquant qu'UN script par
-- numéro, l'une des deux n'a jamais été appliquée → un sous-ensemble de colonnes manque
-- selon la base. Cette migration ajoute, de façon IDEMPOTENTE, l'UNION de toutes les
-- colonnes des deux fichiers 0023 : quelle que soit celle déjà appliquée, les colonnes
-- manquantes sont comblées (les colonnes déjà présentes sont laissées telles quelles).
--
-- Idempotent : chaque colonne n'est ajoutée que si absente (information_schema + PREPARE).
-- Pas de COMMENT (évite l'échappement d'apostrophes) ; les colonnes restent fonctionnelles.

-- first_name (présent dans les deux 0023 ; ajouté seulement si totalement absent)
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'first_name');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN first_name VARCHAR(100) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- last_name
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'last_name');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN last_name VARCHAR(100) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- birth_date (uniquement dans 0023_accounts_profile_fields.sql)
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'birth_date');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN birth_date VARCHAR(10) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- address_street (uniquement dans 0023_accounts_profile.sql)
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_street');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN address_street VARCHAR(255) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- address_city
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_city');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN address_city VARCHAR(100) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- address_zip
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_zip');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN address_zip VARCHAR(20) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- address_country
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_country');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN address_country VARCHAR(100) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- email_pending
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN email_pending VARCHAR(255) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- email_pending_token
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending_token');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN email_pending_token VARCHAR(128) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- email_pending_expires_at
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending_expires_at');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN email_pending_expires_at DATETIME NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- disabled_reason
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'disabled_reason');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN disabled_reason TEXT NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- role (ENUM player/admin ; NOT NULL DEFAULT remplit les lignes existantes)
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'role');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN role ENUM(''player'',''admin'') NOT NULL DEFAULT ''player''', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- parental_email
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_email');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN parental_email VARCHAR(255) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- parental_validated (TINYINT NOT NULL DEFAULT 0 remplit les lignes existantes)
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_validated');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN parental_validated TINYINT(1) NOT NULL DEFAULT 0', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- parental_token
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_token');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN parental_token VARCHAR(128) NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;

-- parental_token_expires_at
SET @c := (SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_token_expires_at');
SET @s := IF(@c = 0, 'ALTER TABLE accounts ADD COLUMN parental_token_expires_at DATETIME NULL', 'SELECT 1');
PREPARE p FROM @s; EXECUTE p; DEALLOCATE PREPARE p;
