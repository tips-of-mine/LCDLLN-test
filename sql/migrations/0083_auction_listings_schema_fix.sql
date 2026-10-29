-- Migration 0083 — Remise au bon schéma de auction_listings (FIX.9).
--
-- Problème (production, 29/10/2026) : la migration 0013 avait créé une ancienne
-- table auction_listings (colonnes id, buyout, closed, sans shard_id). La 0081
-- (ETAPE2.8) utilise CREATE TABLE IF NOT EXISTS : sur une base qui avait déjà la
-- 0013, elle n'a rien fait. Le shard échouait alors à préparer
-- « SELECT listing_id … WHERE shard_id = ? » (colonnes inconnues) et démarrait
-- avec un hôtel des ventes vide (« Auction listings load FAILED »).
--
-- Correction : si auction_listings n'a pas la colonne shard_id (= ancien schéma
-- 0013), elle est renommée auction_listings_legacy_0013 (rien n'est supprimé),
-- la table au schéma 0081 est créée, et les annonces encore ouvertes (closed = 0)
-- y sont recopiées pour le shard 1 (seul shard de l'ancien modèle). Sur une base
-- qui a déjà le bon schéma, la migration ne fait rien.
--
-- Idempotent : chaque étape est conditionnée (information_schema + PREPARE).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- 1. Ancien schéma présent ? (table existe ET pas de colonne shard_id)
SET @m83_table := (
  SELECT COUNT(*) FROM information_schema.tables
  WHERE table_schema = DATABASE() AND table_name = 'auction_listings'
);
SET @m83_has_shard := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'auction_listings' AND column_name = 'shard_id'
);
SET @m83_legacy_exists := (
  SELECT COUNT(*) FROM information_schema.tables
  WHERE table_schema = DATABASE() AND table_name = 'auction_listings_legacy_0013'
);
SET @m83_rename := (@m83_table = 1 AND @m83_has_shard = 0 AND @m83_legacy_exists = 0);

-- 2. Mise de côté de l'ancienne table (jamais supprimée).
SET @m83_s1 := IF(@m83_rename,
  'RENAME TABLE auction_listings TO auction_listings_legacy_0013',
  'SELECT 1');
PREPARE m83_p1 FROM @m83_s1;
EXECUTE m83_p1;
DEALLOCATE PREPARE m83_p1;

-- 3. Table au schéma ETAPE2.8 (identique à 0081).
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

-- 4. Reprise des annonces ouvertes de l'ancienne table (seulement si elle vient
--    d'être mise de côté par cette migration).
SET @m83_s2 := IF(@m83_rename,
  'INSERT INTO auction_listings (shard_id, seller_character_id, item_id, quantity, start_bid, buyout_price, current_bid, high_bidder_character_id, expires_at)
   SELECT 1, seller_character_id, item_id, quantity, start_bid, buyout, current_bid, COALESCE(high_bidder_character_id, 0), expires_at
   FROM auction_listings_legacy_0013 WHERE closed = 0',
  'SELECT 1');
PREPARE m83_p2 FROM @m83_s2;
EXECUTE m83_p2;
DEALLOCATE PREPARE m83_p2;

SET FOREIGN_KEY_CHECKS = 1;
