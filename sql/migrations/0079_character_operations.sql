-- Migration 0079 — Journal des opérations entre deux personnages (ETAPE2.4, chantier 3.4).
--
-- Un échange entre joueurs, ou un transfert de monnaie, modifie deux personnages.
-- Le shard les écrit dans UNE transaction (ServerApp::SaveCharacterPairToDb) et y
-- insère une ligne ici, identifiée par un identifiant d'opération unique : si la
-- même opération est rejouée (nouvel essai après une erreur réseau, double
-- validation), l'insertion échoue sur la clé primaire et rien n'est appliqué une
-- deuxième fois. Une opération est donc écrite entièrement, ou pas du tout, et
-- jamais deux fois : ni objet dupliqué, ni objet perdu entre deux sauvegardes.
--
-- Sert aussi de journal (qui a échangé avec qui, quand).
--
-- Idempotent (CREATE TABLE IF NOT EXISTS).

SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS character_operations (
    operation_id VARCHAR(80)     NOT NULL COMMENT 'unique : <type>-s<shard>-<ms UTC>-<compteur>',
    kind         VARCHAR(32)     NOT NULL COMMENT 'trade, wallet_transfer…',
    character_a  BIGINT UNSIGNED NOT NULL,
    character_b  BIGINT UNSIGNED NOT NULL,
    shard_id     INT UNSIGNED    NOT NULL,
    created_at   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (operation_id),
    KEY idx_character_operations_a (character_a, created_at),
    KEY idx_character_operations_b (character_b, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.4 — opérations à deux personnages (anti-duplication + journal)';
