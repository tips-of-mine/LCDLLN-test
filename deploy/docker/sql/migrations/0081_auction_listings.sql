-- Migration 0081 — Hôtel des ventes en base (ETAPE2.8).
--
-- Avant : les annonces de chaque shard étaient dans un fichier
-- (auction/listings.ini) et chaque opération (mise en vente, enchère, achat
-- immédiat, fin d'enchère) touchait l'annonce, les joueurs et le courrier par des
-- écritures séparées : un arrêt entre deux écritures pouvait dupliquer ou perdre
-- de l'or ou un objet.
--
-- Maintenant : une annonce = une ligne. Chaque opération écrit, dans UNE
-- transaction, l'annonce, les joueurs en ligne concernés (état complet) et les
-- lignes de courrier (character_mailbox) des joueurs hors ligne.
-- expires_at est en UTC (avant : un numéro de tick, faux après un redémarrage).
-- Les annonces du fichier sont importées une fois au démarrage du shard, puis le
-- fichier est renommé en .imported.
--
-- Idempotent (CREATE TABLE IF NOT EXISTS).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS auction_listings (
    listing_id               INT UNSIGNED    NOT NULL AUTO_INCREMENT,
    shard_id                 INT UNSIGNED    NOT NULL COMMENT 'hôtel des ventes du shard',
    seller_character_id      BIGINT UNSIGNED NOT NULL,
    item_id                  INT UNSIGNED    NOT NULL,
    quantity                 INT UNSIGNED    NOT NULL,
    start_bid                INT UNSIGNED    NOT NULL,
    buyout_price             INT UNSIGNED    NOT NULL DEFAULT 0 COMMENT '0 = pas d''achat immédiat',
    current_bid              INT UNSIGNED    NOT NULL DEFAULT 0,
    high_bidder_character_id BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = aucune enchère',
    expires_at               DATETIME        NOT NULL COMMENT 'UTC',
    created_at               DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (listing_id),
    KEY idx_auction_listings_shard (shard_id, expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.8 — annonces de l''hôtel des ventes (objet et enchère en séquestre)';

-- auction_operations : une ligne par opération validée (identifiant unique
-- <type>-s<shard>-<ms UTC>-<compteur>). Insérée dans la transaction de
-- l'opération : si la réponse du COMMIT est perdue et que l'opération est
-- rejouée, sa présence dit « déjà appliquée » et rien n'est refait (pas de
-- double remboursement, pas d'annonce en double). Sert aussi de marqueur
-- d'import unique de l'ancien fichier (« import-file-s<shard> »).
CREATE TABLE IF NOT EXISTS auction_operations (
    operation_id VARCHAR(80)  NOT NULL,
    listing_id   INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'annonce concernée (créée pour une mise en vente)',
    kind         VARCHAR(16)  NOT NULL COMMENT 'list, bid, settle, import',
    created_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (operation_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='ETAPE2.8 — opérations de l''hôtel des ventes déjà appliquées';

SET FOREIGN_KEY_CHECKS = 1;
