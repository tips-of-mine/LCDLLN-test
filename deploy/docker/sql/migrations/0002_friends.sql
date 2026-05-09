-- M32.1 — Migration 0002: friends table (friend list + online status).
-- friends stores bilateral friend relationships; each direction is one row.
-- status: 0=pending, 1=accepted, 2=declined.
-- Max 200 friends per player enforced in application layer (FriendSystem).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ---------------------------------------------------------------------------
-- friends — bilateral friend relationships
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS friends (
  player_id  BIGINT UNSIGNED NOT NULL COMMENT 'account id of the requester',
  friend_id  BIGINT UNSIGNED NOT NULL COMMENT 'account id of the target',
  status     TINYINT UNSIGNED NOT NULL DEFAULT 0
             COMMENT '0=pending, 1=accepted, 2=declined',
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (player_id, friend_id),
  KEY ix_friends_friend_id (friend_id),
  CONSTRAINT chk_friends_no_self CHECK (player_id <> friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

SET FOREIGN_KEY_CHECKS = 1;
