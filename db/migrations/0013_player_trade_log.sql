-- M35.3 — Journal d'audit des échanges directs player-to-player.
-- Les entrées sont insérées côté serveur de jeu après chaque trade_swap réussi.
-- Pour les annulations, le reason_code indique la cause.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- player_trade_log : enregistrement de chaque session de trade terminée
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_trade_log (
  id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  session_id      BIGINT UNSIGNED NOT NULL COMMENT 'Identifiant runtime de la session de trade',
  initiator_id    BIGINT UNSIGNED NOT NULL COMMENT 'characters.id du joueur qui a initié le trade',
  responder_id    BIGINT UNSIGNED NOT NULL COMMENT 'characters.id du joueur qui a accepté',
  outcome         VARCHAR(32)     NOT NULL DEFAULT '' COMMENT 'complete|cancelled|review_expired|swap_failed_*',
  -- Offre de l'initiateur
  init_gold       BIGINT UNSIGNED NOT NULL DEFAULT 0,
  init_items_json TEXT                     COMMENT 'JSON [{item_id,qty}, …] des items offerts par l\'initiateur',
  -- Offre du répondant
  resp_gold       BIGINT UNSIGNED NOT NULL DEFAULT 0,
  resp_items_json TEXT                     COMMENT 'JSON [{item_id,qty}, …] des items offerts par le répondant',
  created_at      TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_player_trade_log_initiator (initiator_id, created_at),
  KEY ix_player_trade_log_responder (responder_id, created_at),
  CONSTRAINT fk_trade_log_initiator
    FOREIGN KEY (initiator_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT fk_trade_log_responder
    FOREIGN KEY (responder_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Audit trail des échanges directs — ne jamais supprimer sans archivage';

SET FOREIGN_KEY_CHECKS = 1;

COMMIT;
