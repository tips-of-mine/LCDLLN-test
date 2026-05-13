-- M100.43 — Dungeon Portal System (Phase 11 « Volumes 3D »).
--
-- Crée la table `dungeon_instances` qui stocke les donjons actifs (instances
-- créées par les joueurs au passage par un portail). Idempotente :
-- CREATE TABLE IF NOT EXISTS. Réjouable au boot serveur après upgrade.
--
-- Une instance est créée à la demande quand un joueur déclenche un portail :
-- handler `EnterDungeonHandler` (à câbler en M100.44) INSERT-era une ligne ici
-- avec le `owner_character_id` du joueur et le `dungeon_template_id` du portail
-- (résolu depuis `instances/dungeon_portals.bin` côté éditeur). L'expiration
-- est gérée par `expires_at` (NULL = pas d'expiration).
--
-- Note : `shard_endpoint` reste vide en M100.43. La résolution shard
-- multi-instance (un shard dédié par dungeon-instance) est M100.44+.
--
-- Pas de wire-breaking : aucun opcode existant ne lit ces colonnes.
-- Les opcodes 197/198 (kOpcodeEnterDungeon{Request,Response}) sont réservés
-- par M100.43 mais ne sont pas encore câblés à un handler — un client qui
-- les enverrait recevrait BAD_REQUEST tant que M100.44 n'aura pas câblé
-- `EnterDungeonHandler`.

CREATE TABLE IF NOT EXISTS dungeon_instances (
    id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    dungeon_template_id VARCHAR(64) NOT NULL,
    owner_character_id BIGINT UNSIGNED NOT NULL,
    difficulty TINYINT UNSIGNED NOT NULL DEFAULT 1,
    shard_endpoint VARCHAR(255) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_dungeon_instances_owner (owner_character_id),
    INDEX idx_dungeon_instances_template (dungeon_template_id),
    INDEX idx_dungeon_instances_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
