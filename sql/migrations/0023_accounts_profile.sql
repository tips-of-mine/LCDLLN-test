-- Migration 0023 — Profil joueur (informations personnelles, gestion email, rôle, contrôle parental)
-- Colonnes ajoutées : first_name, last_name, adresse complète, email_pending flow, disabled_reason, role, parental_*
-- Idempotent : colonnes ajoutées uniquement si manquantes (vérification information_schema + PREPARE).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Profile information: first_name
SET @m23_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'first_name'
);
SET @m23_s1 := IF(@m23_c1 = 0,
  'ALTER TABLE accounts ADD COLUMN first_name VARCHAR(100) NULL COMMENT ''Prénom du joueur''',
  'SELECT 1');
PREPARE m23_p1 FROM @m23_s1;
EXECUTE m23_p1;
DEALLOCATE PREPARE m23_p1;

-- Profile information: last_name
SET @m23_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'last_name'
);
SET @m23_s2 := IF(@m23_c2 = 0,
  'ALTER TABLE accounts ADD COLUMN last_name VARCHAR(100) NULL COMMENT ''Nom de famille du joueur''',
  'SELECT 1');
PREPARE m23_p2 FROM @m23_s2;
EXECUTE m23_p2;
DEALLOCATE PREPARE m23_p2;

-- Address: street
SET @m23_c3 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_street'
);
SET @m23_s3 := IF(@m23_c3 = 0,
  'ALTER TABLE accounts ADD COLUMN address_street VARCHAR(255) NULL COMMENT ''Numéro et rue''',
  'SELECT 1');
PREPARE m23_p3 FROM @m23_s3;
EXECUTE m23_p3;
DEALLOCATE PREPARE m23_p3;

-- Address: city
SET @m23_c4 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_city'
);
SET @m23_s4 := IF(@m23_c4 = 0,
  'ALTER TABLE accounts ADD COLUMN address_city VARCHAR(100) NULL COMMENT ''Ville''',
  'SELECT 1');
PREPARE m23_p4 FROM @m23_s4;
EXECUTE m23_p4;
DEALLOCATE PREPARE m23_p4;

-- Address: postal code
SET @m23_c5 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_zip'
);
SET @m23_s5 := IF(@m23_c5 = 0,
  'ALTER TABLE accounts ADD COLUMN address_zip VARCHAR(20) NULL COMMENT ''Code postal''',
  'SELECT 1');
PREPARE m23_p5 FROM @m23_s5;
EXECUTE m23_p5;
DEALLOCATE PREPARE m23_p5;

-- Address: country
SET @m23_c6 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'address_country'
);
SET @m23_s6 := IF(@m23_c6 = 0,
  'ALTER TABLE accounts ADD COLUMN address_country VARCHAR(100) NULL COMMENT ''Pays''',
  'SELECT 1');
PREPARE m23_p6 FROM @m23_s6;
EXECUTE m23_p6;
DEALLOCATE PREPARE m23_p6;

-- Email change flow: email_pending
SET @m23_c7 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending'
);
SET @m23_s7 := IF(@m23_c7 = 0,
  'ALTER TABLE accounts ADD COLUMN email_pending VARCHAR(255) NULL COMMENT ''Nouvel e-mail en attente de validation''',
  'SELECT 1');
PREPARE m23_p7 FROM @m23_s7;
EXECUTE m23_p7;
DEALLOCATE PREPARE m23_p7;

-- Email change flow: email_pending_token
SET @m23_c8 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending_token'
);
SET @m23_s8 := IF(@m23_c8 = 0,
  'ALTER TABLE accounts ADD COLUMN email_pending_token VARCHAR(128) NULL COMMENT ''Token de validation pour nouveau e-mail''',
  'SELECT 1');
PREPARE m23_p8 FROM @m23_s8;
EXECUTE m23_p8;
DEALLOCATE PREPARE m23_p8;

-- Email change flow: email_pending_expires_at
SET @m23_c9 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'email_pending_expires_at'
);
SET @m23_s9 := IF(@m23_c9 = 0,
  'ALTER TABLE accounts ADD COLUMN email_pending_expires_at DATETIME NULL COMMENT ''Expiration du token de validation d''''e-mail''',
  'SELECT 1');
PREPARE m23_p9 FROM @m23_s9;
EXECUTE m23_p9;
DEALLOCATE PREPARE m23_p9;

-- Account management: disabled_reason
SET @m23_c10 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'disabled_reason'
);
SET @m23_s10 := IF(@m23_c10 = 0,
  'ALTER TABLE accounts ADD COLUMN disabled_reason TEXT NULL COMMENT ''Raison de la désactivation du compte (si compte désactivé)''',
  'SELECT 1');
PREPARE m23_p10 FROM @m23_s10;
EXECUTE m23_p10;
DEALLOCATE PREPARE m23_p10;

-- Account management: role
SET @m23_c11 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'role'
);
SET @m23_s11 := IF(@m23_c11 = 0,
  'ALTER TABLE accounts ADD COLUMN role ENUM(''player'',''admin'') NOT NULL DEFAULT ''player'' COMMENT ''Rôle du compte: player ou admin''',
  'SELECT 1');
PREPARE m23_p11 FROM @m23_s11;
EXECUTE m23_p11;
DEALLOCATE PREPARE m23_p11;

-- Parental control: parental_email
SET @m23_c12 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_email'
);
SET @m23_s12 := IF(@m23_c12 = 0,
  'ALTER TABLE accounts ADD COLUMN parental_email VARCHAR(255) NULL COMMENT ''E-mail du parent (pour contrôle parental)''',
  'SELECT 1');
PREPARE m23_p12 FROM @m23_s12;
EXECUTE m23_p12;
DEALLOCATE PREPARE m23_p12;

-- Parental control: parental_validated
SET @m23_c13 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_validated'
);
SET @m23_s13 := IF(@m23_c13 = 0,
  'ALTER TABLE accounts ADD COLUMN parental_validated TINYINT(1) NOT NULL DEFAULT 0 COMMENT ''0=non validé, 1=validé par le parent''',
  'SELECT 1');
PREPARE m23_p13 FROM @m23_s13;
EXECUTE m23_p13;
DEALLOCATE PREPARE m23_p13;

-- Parental control: parental_token
SET @m23_c14 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_token'
);
SET @m23_s14 := IF(@m23_c14 = 0,
  'ALTER TABLE accounts ADD COLUMN parental_token VARCHAR(128) NULL COMMENT ''Token de validation parental''',
  'SELECT 1');
PREPARE m23_p14 FROM @m23_s14;
EXECUTE m23_p14;
DEALLOCATE PREPARE m23_p14;

-- Parental control: parental_token_expires_at
SET @m23_c15 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'parental_token_expires_at'
);
SET @m23_s15 := IF(@m23_c15 = 0,
  'ALTER TABLE accounts ADD COLUMN parental_token_expires_at DATETIME NULL COMMENT ''Expiration du token de validation parental''',
  'SELECT 1');
PREPARE m23_p15 FROM @m23_s15;
EXECUTE m23_p15;
DEALLOCATE PREPARE m23_p15;

SET FOREIGN_KEY_CHECKS = 1;
