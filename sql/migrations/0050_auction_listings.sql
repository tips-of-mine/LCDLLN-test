-- 0050 - Wave 5 Persistence AuctionHouse (Phase 5.09b) : table
-- auction_listings_v2 pour la nouvelle generation de listings posees
-- par les handlers wire master (Phase 5.09b). La table 0013_auction_listings
-- est conservee pour l'ancien schema legacy ; ce migration ajoute une
-- nouvelle table avec un schema enrichi (item_name, owner_name,
-- highest_bidder_name, ended/won_by_buyout flags) au lieu de l'etendre
-- afin de preserver la coherence des deploiements deja partis.
-- Idempotent.
--
-- expires_at_unix_ms : ms epoch system_clock (le runtime gere un
-- steady_clock derive ; la persistance utilise wallclock pour survivre
-- au reboot). ended = 1 quand l'auction est cloturee (buyout / cancel /
-- scan ScanExpired) ; won_by_buyout = 1 seulement si le ended provient
-- d'un buyout immediat.

CREATE TABLE IF NOT EXISTS auction_listings_v2 (
    auction_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    item_template_id INT UNSIGNED NOT NULL,
    item_name VARCHAR(128) NOT NULL,
    count INT UNSIGNED NOT NULL DEFAULT 1,
    start_bid_copper BIGINT UNSIGNED NOT NULL DEFAULT 0,
    current_bid_copper BIGINT UNSIGNED NOT NULL DEFAULT 0,
    buyout_copper BIGINT UNSIGNED NOT NULL DEFAULT 0,
    owner_account_id BIGINT UNSIGNED NOT NULL,
    owner_name VARCHAR(64) NOT NULL,
    highest_bidder_account_id BIGINT UNSIGNED NULL,
    highest_bidder_name VARCHAR(64) NULL,
    expires_at_unix_ms BIGINT UNSIGNED NOT NULL,
    ended TINYINT(1) NOT NULL DEFAULT 0,
    won_by_buyout TINYINT(1) NOT NULL DEFAULT 0,
    INDEX idx_owner (owner_account_id),
    INDEX idx_expires (expires_at_unix_ms),
    INDEX idx_ended (ended)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
