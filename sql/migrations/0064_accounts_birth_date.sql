-- Migration 0064 — Ajout `birth_date` sur la table `accounts`.
--
-- Historique : ce contenu vivait dans `0023_accounts_profile_fields.sql` (cf.
-- audit 2026-05-18 + git blame), mais MigrationRunner ne peut appliquer qu'UNE
-- migration par version `int` (cf. `src/masterd/migrations/MigrationRunner.cpp`
-- ligne 270 `version <= maxApplied`). Or `0023_accounts_profile.sql` existait
-- aussi en version 23 et gagnait l'application par tri alphabetique. L'ancien
-- `0023_accounts_profile_fields.sql` n'etait JAMAIS execute en prod, donc
-- `accounts.birth_date` n'existait pas — alors que `MysqlAccountStore`
-- (`src/masterd/account/MysqlAccountStore.cpp:262`) tente de l'INSERT a chaque
-- register, soit echec SQL silencieux.
--
-- Cette migration repare en ajoutant uniquement `birth_date`. Les ALTER
-- `first_name` / `last_name` ont ete retires car ils faisaient double emploi
-- avec `0023_accounts_profile.sql` (deja appliquee, qui a cree les colonnes
-- en `VARCHAR(100) NULL`). On garde donc la divergence de type
-- (audit suggerait `VARCHAR(64) NOT NULL DEFAULT ''`) : le schema reel en prod
-- est `VARCHAR(100) NULL`, on s'y aligne.
--
-- `birth_date` est stockee comme chaine "yyyy-mm-dd" — non validee cote
-- serveur (cf. commentaire historique 0023_accounts_profile_fields.sql).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Ajout birth_date si absent.
SET @m64_bd := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'accounts' AND column_name = 'birth_date'
);
SET @m64_bd_s := IF(@m64_bd = 0,
  'ALTER TABLE accounts ADD COLUMN birth_date VARCHAR(10) NOT NULL DEFAULT '''' AFTER last_name',
  'SELECT 1');
PREPARE m64_bd_p FROM @m64_bd_s; EXECUTE m64_bd_p; DEALLOCATE PREPARE m64_bd_p;

SET FOREIGN_KEY_CHECKS = 1;
