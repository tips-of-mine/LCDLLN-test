-- M21.4 — Utilisateurs MySQL : master (RW toutes tables) et shard (RW limité aux tables gameplay).
-- À exécuter par un compte ayant les droits CREATE USER et GRANT (ex: root).
-- Principe du moindre privilège.

-- Base doit exister (créée par db/schema.sql ou migrations).
USE lcdlln_master;

-- ---------------------------------------------------------------------------
-- master_user : lecture/écriture complète + migrations (DDL + schema_version)
-- ---------------------------------------------------------------------------
CREATE USER IF NOT EXISTS 'master_user'@'%' IDENTIFIED BY 'CHANGEME_master';
GRANT ALL PRIVILEGES ON lcdlln_master.* TO 'master_user'@'%';

-- ---------------------------------------------------------------------------
-- shard_user : limité aux tables gameplay (characters, etc.) — pas accounts/sessions
-- ---------------------------------------------------------------------------
CREATE USER IF NOT EXISTS 'shard_user'@'%' IDENTIFIED BY 'CHANGEME_shard';
GRANT SELECT, INSERT, UPDATE, DELETE ON lcdlln_master.characters TO 'shard_user'@'%';

FLUSH PRIVILEGES;
