-- 0062 — Wave 24 Movement scaffold : table creature_movement pour les
-- patrouilles waypoints des creatures.
--
-- Idempotente : CREATE TABLE IF NOT EXISTS. Rejouable sans erreur.
--
-- L'integration recast/detour (path inter-waypoint avec obstacles) viendra
-- dans une PR ulterieure avec ajout vcpkg recastnavigation. Cette table
-- est utilisable des maintenant via PathFollowMotion + StubNavmeshProvider
-- (qui se contente du segment direct).

CREATE TABLE IF NOT EXISTS creature_movement
(
	creature_guid  BIGINT UNSIGNED NOT NULL,
	point_idx      INT UNSIGNED NOT NULL,
	pos_x          FLOAT NOT NULL,
	pos_y          FLOAT NOT NULL,
	pos_z          FLOAT NOT NULL,
	wait_ms        INT UNSIGNED NOT NULL DEFAULT 0,
	script_id      INT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = pas de DBScript a executer
	PRIMARY KEY (creature_guid, point_idx),
	KEY idx_creature_movement_script (script_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
