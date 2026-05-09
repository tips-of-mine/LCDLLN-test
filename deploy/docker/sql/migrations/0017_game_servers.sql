-- 0017_game_servers.sql
-- Registre des serveurs de jeu disponibles.
-- Le master s'y inscrit au démarrage et s'y désactive a l'arret.

CREATE TABLE IF NOT EXISTS game_servers (
    server_id      INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name           VARCHAR(64)  NOT NULL,
    host           VARCHAR(128) NOT NULL,
    port           SMALLINT UNSIGNED NOT NULL,
    max_players    INT UNSIGNED NOT NULL DEFAULT 0,
    online_players INT UNSIGNED NOT NULL DEFAULT 0,
    status         ENUM('online','offline','maintenance') NOT NULL DEFAULT 'offline',
    last_heartbeat DATETIME NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY idx_game_servers_host_port (host, port)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
