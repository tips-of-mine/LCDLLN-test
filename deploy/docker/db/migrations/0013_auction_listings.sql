-- M35.4 — Auction house listings (authoritative shard may use file-backed INI on WIN32; MySQL schema for UNIX deployments).
CREATE TABLE IF NOT EXISTS auction_listings (
  id BIGINT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
  seller_character_id BIGINT UNSIGNED NOT NULL,
  item_id INT UNSIGNED NOT NULL,
  quantity INT UNSIGNED NOT NULL DEFAULT 1,
  start_bid INT UNSIGNED NOT NULL,
  buyout INT UNSIGNED NOT NULL DEFAULT 0,
  current_bid INT UNSIGNED NOT NULL DEFAULT 0,
  high_bidder_character_id BIGINT UNSIGNED NULL,
  expires_at DATETIME NOT NULL,
  closed TINYINT(1) NOT NULL DEFAULT 0,
  KEY idx_auction_item (item_id),
  KEY idx_auction_price (current_bid, start_bid),
  KEY idx_auction_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
