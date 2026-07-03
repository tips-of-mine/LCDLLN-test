-- 0059 — Wave 20 Pool extensions : nested pools (table pool_pool) +
-- runtime state persistence (table pool_member_state).
--
-- Idempotente : CREATE TABLE IF NOT EXISTS. Rejouable sans erreur.

-- Table pool_pool : permet a un pool d'avoir des sous-pools comme entries.
-- Quand RollImpl tire un entry "nested", il recurse dans le child_pool_id.
-- Cycle detection cote moteur (PoolManager.h), pas au niveau DB.
CREATE TABLE IF NOT EXISTS pool_pool
(
	parent_pool_id  INT UNSIGNED NOT NULL,
	child_pool_id   INT UNSIGNED NOT NULL,
	weight          FLOAT NOT NULL DEFAULT 1.0,
	PRIMARY KEY (parent_pool_id, child_pool_id),
	-- Index inverse pour query "quels parents pointent vers ce pool ?"
	-- (utile lors d'un cleanup/refactor de la hierarchie).
	KEY idx_pool_pool_child (child_pool_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Table pool_member_state : etat runtime persiste des membres d'une pool.
-- Permet de survivre a un reboot shard sans tout re-roller (les rare
-- spawns deja kill restent dead jusqu'a leur respawn_at).
CREATE TABLE IF NOT EXISTS pool_member_state
(
	pool_id        INT UNSIGNED NOT NULL,
	spawn_id       INT UNSIGNED NOT NULL,
	status         TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0=Alive, 1=Dead, 2=Respawning
	respawn_at_sec BIGINT NOT NULL DEFAULT 0,             -- Unix timestamp seconds, 0 = aucun
	updated_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
	               ON UPDATE CURRENT_TIMESTAMP,
	PRIMARY KEY (pool_id, spawn_id),
	-- Index pour query par status (ex : "trouve tous les spawns Respawning
	-- a re-creer au prochain tick").
	KEY idx_pool_member_state_status (status),
	-- Index par respawn timer (purge cleanup ou tick respawn scheduler).
	KEY idx_pool_member_state_respawn (respawn_at_sec)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
