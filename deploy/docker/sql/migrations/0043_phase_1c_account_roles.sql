-- 0043 — Phase 1c CMANGOS Accounts : étend l'ENUM `accounts.role` de
-- ('player','admin') à 4 valeurs ('player','moderator','game_master',
-- 'administrator'). Migre 'admin' → 'administrator'. Idempotent via
-- information_schema check.

SET NAMES utf8mb4;

-- 0) Garantir l'existence de la colonne `role`. Selon lequel des deux fichiers
--    0023 a été appliqué (doublon de numéro, cf. MigrationRunner + 0066), la
--    colonne peut ne PAS exister ici : 0023_accounts_profile.sql la crée, mais
--    0023_accounts_profile_fields.sql non, et 0066 (qui comble le manque) tourne
--    APRÈS 0043. Sans cette garde, l'UPDATE en (2) échoue avec
--    « Unknown column 'role' » et le master boucle au boot. On la crée au format
--    binaire historique ('player','admin') ; l'étape (1) l'étendra ensuite.
SET @m43_c0 := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND column_name  = 'role'
);
SET @m43_s0 := IF(@m43_c0 = 0,
  'ALTER TABLE accounts ADD COLUMN role '
  'ENUM(''player'',''admin'') NOT NULL DEFAULT ''player'' '
  'COMMENT ''Role du compte (sera etendu par 0043)''',
  'SELECT 1');
PREPARE m43_p0 FROM @m43_s0;
EXECUTE m43_p0;
DEALLOCATE PREPARE m43_p0;

-- 1) Vérifier l'état actuel de la colonne `role` et étendre l'ENUM si
--    elle est encore au format binaire ('player','admin').

SET @m43_c1 := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND column_name  = 'role'
    AND column_type  = "enum('player','admin')"
);
SET @m43_s1 := IF(@m43_c1 = 1,
  'ALTER TABLE accounts MODIFY COLUMN role '
  'ENUM(''player'',''moderator'',''game_master'',''administrator'') '
  'NOT NULL DEFAULT ''player'' '
  'COMMENT ''Role hierarchique 4 niveaux (CMANGOS.06 Phase 1c). '
  'Console est sentinel runtime, pas persiste.''',
  'SELECT 1');
PREPARE m43_p1 FROM @m43_s1;
EXECUTE m43_p1;
DEALLOCATE PREPARE m43_p1;

-- 2) Backfill : tout compte avec role='admin' devient 'administrator'.
--    Idempotent : si déjà migré, le UPDATE ne touche aucune ligne.

UPDATE accounts SET role = 'administrator' WHERE role = 'admin';

-- 3) Index sur role pour les futurs lookups GM (ChatCommandRouter, etc.)
--    Idempotent via information_schema.

SET @m43_c2 := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND index_name   = 'ix_accounts_role'
);
SET @m43_s2 := IF(@m43_c2 = 0,
  'ALTER TABLE accounts ADD KEY ix_accounts_role (role)',
  'SELECT 1');
PREPARE m43_p2 FROM @m43_s2;
EXECUTE m43_p2;
DEALLOCATE PREPARE m43_p2;
