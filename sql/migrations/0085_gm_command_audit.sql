-- ADM.0 — Journal d'audit des commandes de jeu (aide, MJ, modération, serveur).
-- Le master y écrit une ligne pour chaque commande réservée (rôle minimum
-- modérateur ou plus) exécutée ou refusée, et pour chaque refus faute de rôle,
-- quelle que soit la commande. Consulté en jeu par « /gmaudit [n] [login] »
-- (administrateur). Le journal technique (logs master/shard) garde aussi une
-- trace ; cette table rend l'historique interrogeable et durable.
--
-- result : 'ok' (exécutée par le master ou relayée au shard), 'denied' (rôle
-- insuffisant). target_name : cible nommée de la commande quand elle est connue
-- (personnage ou login), sinon NULL.
-- Idempotent : CREATE TABLE IF NOT EXISTS.

SET NAMES utf8mb4;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS gm_command_audit (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  created_at   DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT 'UTC',
  account_id   BIGINT UNSIGNED NOT NULL COMMENT 'compte appelant',
  role         VARCHAR(16) NOT NULL COMMENT 'role de l appelant au moment de la commande',
  command      VARCHAR(48) NOT NULL COMMENT 'nom tape, sans /',
  args         VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'arguments (tronques a 255 octets)',
  result       VARCHAR(16) NOT NULL COMMENT 'ok | denied',
  target_name  VARCHAR(64) NULL COMMENT 'cible nommee (personnage ou login) si connue',
  PRIMARY KEY (id),
  KEY idx_gm_command_audit_created (created_at),
  KEY idx_gm_command_audit_account (account_id, created_at),
  KEY idx_gm_command_audit_target (target_name, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ADM.0 : audit des commandes de jeu reservees';

COMMIT;
