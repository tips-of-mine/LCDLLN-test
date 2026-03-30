-- M35.4 — Auction house: listings, bids, pending deliveries.
-- auction_listings: one row per active or recently-expired auction.
-- ah_pending_deliveries: items/gold to deliver to players on next login.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- auction_listings : one row per auction (active | expired | cancelled)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS auction_listings (
  id               BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  seller_id        BIGINT UNSIGNED NOT NULL COMMENT 'characters.id of the listing owner',
  item_id          BIGINT UNSIGNED NOT NULL COMMENT 'Item identifier being auctioned',
  item_quantity    INT UNSIGNED    NOT NULL DEFAULT 1,
  start_bid        BIGINT UNSIGNED NOT NULL COMMENT 'Minimum starting bid (gold)',
  buyout           BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = no buyout price set',
  current_bid      BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Current highest bid amount',
  high_bidder_id   BIGINT UNSIGNED          DEFAULT NULL COMMENT 'characters.id of current high bidder; NULL when no bids yet',
  duration_hours   TINYINT UNSIGNED NOT NULL DEFAULT 24 COMMENT 'Chosen duration: 12, 24 or 48 hours',
  expires_at       DATETIME        NOT NULL COMMENT 'UTC timestamp when the auction ends',
  status           VARCHAR(16)     NOT NULL DEFAULT 'active' COMMENT 'active|sold|expired|cancelled',
  created_at       TIMESTAMP NULL  DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_ah_status_expires  (status, expires_at),
  KEY ix_ah_item_id         (item_id),
  KEY ix_ah_seller          (seller_id),
  KEY ix_ah_current_bid     (current_bid),
  CONSTRAINT fk_ah_seller
    FOREIGN KEY (seller_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='M35.4 — Active and recently-completed auction house listings';

-- ---------------------------------------------------------------------------
-- ah_bid_history : immutable audit trail of every bid placed
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ah_bid_history (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  listing_id   BIGINT UNSIGNED NOT NULL,
  bidder_id    BIGINT UNSIGNED NOT NULL COMMENT 'characters.id of the bidder',
  bid_amount   BIGINT UNSIGNED NOT NULL,
  placed_at    TIMESTAMP NULL  DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_ah_bid_listing (listing_id, placed_at),
  KEY ix_ah_bid_bidder  (bidder_id, placed_at),
  CONSTRAINT fk_ah_bid_listing
    FOREIGN KEY (listing_id) REFERENCES auction_listings (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='M35.4 — Immutable bid history for each auction listing';

-- ---------------------------------------------------------------------------
-- ah_pending_deliveries : items/gold queued for offline players
-- Delivered automatically when the player next logs in.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ah_pending_deliveries (
  id             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  recipient_id   BIGINT UNSIGNED NOT NULL COMMENT 'characters.id to receive the delivery',
  listing_id     BIGINT UNSIGNED          DEFAULT NULL COMMENT 'Source auction listing (nullable for manual injections)',
  gold_amount    BIGINT UNSIGNED NOT NULL DEFAULT 0   COMMENT 'Gold to credit on delivery',
  item_id        BIGINT UNSIGNED          DEFAULT NULL COMMENT 'Item to add to inventory; NULL when gold-only',
  item_quantity  INT UNSIGNED             DEFAULT NULL,
  reason         VARCHAR(64)     NOT NULL DEFAULT '' COMMENT 'sold|outbid|expired_no_bid|cancelled',
  created_at     TIMESTAMP NULL  DEFAULT CURRENT_TIMESTAMP,
  delivered_at   TIMESTAMP NULL  DEFAULT NULL         COMMENT 'Set when the row has been processed',
  PRIMARY KEY (id),
  KEY ix_ah_delivery_recipient (recipient_id, delivered_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='M35.4 — Pending AH item/gold deliveries for offline players';

SET FOREIGN_KEY_CHECKS = 1;

COMMIT;
