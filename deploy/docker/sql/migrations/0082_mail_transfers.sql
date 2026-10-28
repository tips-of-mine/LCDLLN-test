-- Migration 0082 — Or joint à un courrier, en deux phases (ETAPE5.6, chantier 5.2).
--
-- Le courrier est géré par le master (comptes), mais seul le shard propriétaire
-- d'un personnage écrit sa bourse (ETAPE1.2). Un envoi d'or se fait donc en deux
-- phases, et cette table est le VERROU commun aux deux côtés :
--
--   pending   : le master a créé le courrier et demandé au shard de l'expéditeur
--               de prélever l'or (opcode 213) ; l'or n'est pas encore retirable ;
--   locked    : le shard a prélevé l'or DANS LA MÊME TRANSACTION que ce passage
--               (UPDATE … WHERE state = 'pending') : l'or est en dépôt ;
--   delivered : le destinataire l'a retiré ; le master a déposé l'or dans
--               character_mailbox (même transaction), le shard du destinataire
--               le crédite (tout de suite s'il est en jeu, sinon à la connexion) ;
--   cancelled : pas de prélèvement (refus du shard, délai dépassé, personnage
--               absent) — l'expéditeur n'a rien perdu ;
--   refunded  : courrier expiré avec l'or en dépôt : rendu à l'expéditeur
--               (character_mailbox, même transaction).
--
-- Chaque passage est un « compare-and-set » (UPDATE … WHERE state = <attendu>) :
-- le prélèvement par le shard et l'annulation par le master s'excluent, une
-- annulation est donc toujours réelle des deux côtés. L'identifiant d'opération
-- (mail-<mail_id>) rend chaque étape idempotente.
--
-- character_mailbox reçoit un identifiant d'opération unique : un même dépôt
-- rejoué (nouvel essai, double clic) n'est inséré qu'une fois.
--
-- Idempotent (CREATE TABLE IF NOT EXISTS, colonne / index ajoutés s'ils manquent).

SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS mail_transfers (
    operation_id        VARCHAR(80)     NOT NULL COMMENT 'mail-<mail_id>',
    mail_id             BIGINT UNSIGNED NOT NULL,
    sender_account_id   BIGINT UNSIGNED NOT NULL,
    sender_character_id BIGINT UNSIGNED NOT NULL,
    sender_shard_id     INT UNSIGNED    NOT NULL COMMENT 'shard qui prélève (vérifié à sa réponse)',
    receiver_account_id BIGINT UNSIGNED NOT NULL,
    gold                INT UNSIGNED    NOT NULL,
    state               VARCHAR(16)     NOT NULL DEFAULT 'pending' COMMENT 'pending, locked, delivered, cancelled, refunded',
    deadline_ms         BIGINT UNSIGNED NOT NULL COMMENT 'au-delà, un pending est annulé (epoch ms UTC)',
    receiver_character_id BIGINT UNSIGNED NULL DEFAULT NULL COMMENT 'personnage crédité (delivered)',
    created_at          DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (operation_id),
    UNIQUE KEY uq_mail_transfers_mail (mail_id),
    KEY idx_mail_transfers_state_deadline (state, deadline_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE5.6 — or joint à un courrier (deux phases master/shard)';

SET @m82_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'character_mailbox' AND column_name = 'operation_id'
);
SET @m82_s := IF(@m82_c = 0,
  'ALTER TABLE character_mailbox ADD COLUMN operation_id VARCHAR(96) NULL DEFAULT NULL COMMENT ''dépôt idempotent (NULL = historique)''',
  'SELECT 1');
PREPARE m82_p FROM @m82_s;
EXECUTE m82_p;
DEALLOCATE PREPARE m82_p;

SET @m82_c := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'character_mailbox' AND index_name = 'uq_character_mailbox_operation'
);
SET @m82_s := IF(@m82_c = 0,
  'CREATE UNIQUE INDEX uq_character_mailbox_operation ON character_mailbox (operation_id)',
  'SELECT 1');
PREPARE m82_p FROM @m82_s;
EXECUTE m82_p;
DEALLOCATE PREPARE m82_p;
