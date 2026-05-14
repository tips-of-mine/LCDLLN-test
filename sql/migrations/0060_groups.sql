-- 0060 — Wave 22 Groups master-side : groupes (party/raid) + members + audit
-- des loot rolls. Master-side car les groupes peuvent etre cross-shard
-- (raid future).
--
-- Idempotente : CREATE TABLE IF NOT EXISTS. Rejouable sans erreur.
--
-- Note : la table `groups` est backtick-quotee partout — `GROUPS` est un
-- mot reserve depuis MySQL 8.0.2 (window functions). Sans les backticks,
-- `CREATE TABLE groups` et `REFERENCES groups` sont des syntax errors sur
-- MySQL 8.x (OK sur 5.7, d'ou le passage CI initial).

-- Table groups : 1 ligne par groupe actif (party ou raid). Disband =
-- DELETE (cascade member rows via FK ON DELETE CASCADE).
CREATE TABLE IF NOT EXISTS `groups`
(
	group_id     BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
	group_type   TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0=Party, 1=Raid
	leader_id    BIGINT UNSIGNED NOT NULL,
	loot_method  TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0=FFA, 1=RR, 2=ML, 3=NBG
	created_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (group_id),
	KEY idx_groups_leader (leader_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Table group_members : (group_id, player_id) avec role optionnel.
-- ON DELETE CASCADE assure le cleanup quand un groupe est dissolved.
CREATE TABLE IF NOT EXISTS group_members
(
	group_id    BIGINT UNSIGNED NOT NULL,
	player_id   BIGINT UNSIGNED NOT NULL,
	role        TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0=Unknown, 1=Tank, 2=Heal, 3=Dps
	joined_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (group_id, player_id),
	-- Un player ne peut etre que dans 1 groupe a la fois : index unique
	-- sur player_id pour enforce cette contrainte au niveau DB aussi.
	UNIQUE KEY uniq_group_members_player (player_id),
	KEY idx_group_members_group (group_id),
	CONSTRAINT fk_group_members_group
		FOREIGN KEY (group_id) REFERENCES `groups` (group_id)
		ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Table group_loot_rolls : audit des decisions roll (Need/Greed/Pass) pour
-- le LootMethod NeedBeforeGreed. Permet de revisiter une dispute ulterieure.
-- Pas de FK vers groups (un loot roll peut survivre au disband group pour
-- conserver l'historique audit).
CREATE TABLE IF NOT EXISTS group_loot_rolls
(
	roll_id     BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
	group_id    BIGINT UNSIGNED NOT NULL,
	item_id     BIGINT UNSIGNED NOT NULL,
	player_id   BIGINT UNSIGNED NOT NULL,
	choice      TINYINT UNSIGNED NOT NULL,  -- 0=Pass, 1=Greed, 2=Need
	rolled_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (roll_id),
	KEY idx_group_loot_rolls_group (group_id),
	KEY idx_group_loot_rolls_item (item_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
