-- Migration 0023 — Champs profil joueur sur les comptes (first_name, last_name, birth_date)
-- Ces colonnes étaient collectées à l'inscription mais ignorées côté serveur (void casts).
-- birth_date est stockée comme chaîne "yyyy-mm-dd" — non validée côté serveur.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Ajout first_name si absent.
SET @m23_fn := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'first_name'
);
SET @m23_fn_s := IF(@m23_fn = 0,
  'ALTER TABLE accounts ADD COLUMN first_name VARCHAR(64) NOT NULL DEFAULT '''' AFTER tag_id',
  'SELECT 1');
PREPARE m23_fn_p FROM @m23_fn_s; EXECUTE m23_fn_p; DEALLOCATE PREPARE m23_fn_p;

-- Ajout last_name si absent.
SET @m23_ln := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'last_name'
);
SET @m23_ln_s := IF(@m23_ln = 0,
  'ALTER TABLE accounts ADD COLUMN last_name VARCHAR(64) NOT NULL DEFAULT '''' AFTER first_name',
  'SELECT 1');
PREPARE m23_ln_p FROM @m23_ln_s; EXECUTE m23_ln_p; DEALLOCATE PREPARE m23_ln_p;

-- Ajout birth_date si absent.
-- Format attendu : "yyyy-mm-dd" (non validé côté serveur, stocké tel quel).
SET @m23_bd := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'birth_date'
);
SET @m23_bd_s := IF(@m23_bd = 0,
  'ALTER TABLE accounts ADD COLUMN birth_date VARCHAR(10) NOT NULL DEFAULT '''' AFTER last_name',
  'SELECT 1');
PREPARE m23_bd_p FROM @m23_bd_s; EXECUTE m23_bd_p; DEALLOCATE PREPARE m23_bd_p;

SET FOREIGN_KEY_CHECKS = 1;
