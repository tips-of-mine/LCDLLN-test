-- M35.1 — Portefeuille multi-devise + journal d'audit (transactions atomiques côté application via START TRANSACTION).
-- player_id référence characters.id (personnage MySQL).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- player_wallet : solde par (personnage, devise)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_wallet (
  player_id   BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
  currency_id TINYINT UNSIGNED NOT NULL COMMENT '1=gold,2=honor,3=badges,4=premium (aligné config jeu)',
  amount      BIGINT UNSIGNED NOT NULL DEFAULT 0,
  updated_at  TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (player_id, currency_id),
  KEY ix_player_wallet_currency (currency_id),
  CONSTRAINT fk_player_wallet_character
    FOREIGN KEY (player_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Soldes runtime ; caps appliqués par le serveur de jeu';

-- ---------------------------------------------------------------------------
-- player_currency_log : audit des mouvements (après START TRANSACTION avec player_wallet)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_currency_log (
  id             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  player_id      BIGINT UNSIGNED NOT NULL,
  currency_id    TINYINT UNSIGNED NOT NULL,
  delta          BIGINT NOT NULL COMMENT 'positif = crédit, négatif = débit',
  balance_after  BIGINT UNSIGNED NOT NULL DEFAULT 0,
  peer_player_id BIGINT UNSIGNED NULL DEFAULT NULL COMMENT 'autre partie pour transfer, sinon NULL',
  reason         VARCHAR(64) NOT NULL DEFAULT '',
  created_at     TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_player_currency_log_player_time (player_id, created_at),
  CONSTRAINT fk_player_currency_log_character
    FOREIGN KEY (player_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Journal audit ; insérer dans la même transaction que les UPDATE player_wallet';

SET FOREIGN_KEY_CHECKS = 1;

COMMIT;
