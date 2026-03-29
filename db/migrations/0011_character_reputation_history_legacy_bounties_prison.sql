-- M0011 — Réputation (character-scoped), historique, mémoire PNJ, legacy compte, primes, prison, purge 6 mois.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- characters : réputation globale, outlaw, cycle de vie suppression
-- ---------------------------------------------------------------------------
ALTER TABLE characters
  ADD COLUMN global_reputation INT NOT NULL DEFAULT 0
    COMMENT 'réputation agrégée affichée (détail par scope dans character_reputation)',
  ADD COLUMN is_outlaw TINYINT(1) NOT NULL DEFAULT 0
    COMMENT '1=hors-la-loi (flag gameplay)',
  ADD COLUMN deleted_at TIMESTAMP NULL DEFAULT NULL
    COMMENT 'soft delete : horodatage effacement',
  ADD COLUMN status ENUM('active','pending_deletion','deleted') NOT NULL DEFAULT 'active'
    COMMENT 'active | pending_deletion | deleted',
  ADD KEY ix_characters_status_deleted (status, deleted_at),
  ADD KEY ix_characters_global_reputation (global_reputation);

-- ---------------------------------------------------------------------------
-- character_reputation : réputation par scope (faction, région, PNJ, global)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS character_reputation (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  character_id  BIGINT UNSIGNED NOT NULL,
  scope_type    ENUM('global','faction','region','npc') NOT NULL,
  scope_id      BIGINT NOT NULL DEFAULT 0
                COMMENT '0 pour global ; sinon id faction/région/npc selon scope_type',
  value         INT NOT NULL DEFAULT 0,
  last_update   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_char_rep_scope (character_id, scope_type, scope_id),
  KEY ix_character_reputation_character_id (character_id),
  KEY ix_character_reputation_scope (scope_type, scope_id),
  CONSTRAINT fk_character_reputation_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT chk_character_reputation_value CHECK (value BETWEEN -2147483648 AND 2147483647)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Réputation par personnage et par scope (non liée au compte)';

-- ---------------------------------------------------------------------------
-- character_history : crimes, réputation, interactions marquantes
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS character_history (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  character_id  BIGINT UNSIGNED NOT NULL,
  account_id    BIGINT UNSIGNED NOT NULL
                COMMENT 'dénormalisé pour requêtes / audit par compte',
  event_type    VARCHAR(64) NOT NULL,
  severity      INT NOT NULL DEFAULT 0,
  description   TEXT NOT NULL,
  created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_character_history_character_id (character_id),
  KEY ix_character_history_account_id (account_id),
  KEY ix_character_history_created_at (created_at),
  KEY ix_character_history_event_type (event_type),
  KEY ix_character_history_char_created (character_id, created_at),
  CONSTRAINT fk_character_history_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT fk_character_history_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE,
  CONSTRAINT chk_character_history_severity CHECK (severity >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Historique d’actions (crimes, réputation, événements)';

-- ---------------------------------------------------------------------------
-- npc_memory : opinion PNJ → personnage (purge TTL séparée)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS npc_memory (
  id                 BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  npc_id             BIGINT UNSIGNED NOT NULL
                     COMMENT 'identifiant PNJ côté jeu (pas de table ref ici)',
  character_id       BIGINT UNSIGNED NOT NULL,
  opinion            INT NOT NULL DEFAULT 0,
  last_interaction   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                     COMMENT 'dernière interaction ; utilisé pour purge 6 mois',
  PRIMARY KEY (id),
  UNIQUE KEY uq_npc_memory_npc_character (npc_id, character_id),
  KEY ix_npc_memory_character_id (character_id),
  KEY ix_npc_memory_last_interaction (last_interaction),
  KEY ix_npc_memory_npc_id (npc_id),
  CONSTRAINT fk_npc_memory_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT chk_npc_memory_opinion CHECK (opinion BETWEEN -2147483648 AND 2147483647)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Mémoire PNJ vers personnages ; lignes anciennes purgées après 6 mois';

-- ---------------------------------------------------------------------------
-- player_legacy : mémoire narrative compte (non punitive)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS player_legacy (
  id                 BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  account_id         BIGINT UNSIGNED NOT NULL,
  total_crimes       INT NOT NULL DEFAULT 0,
  max_infamy_level   INT NOT NULL DEFAULT 0,
  last_known_alias   VARCHAR(64) NULL DEFAULT NULL,
  legacy_score       INT NOT NULL DEFAULT 0,
  updated_at         TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_player_legacy_account (account_id),
  KEY ix_player_legacy_updated_at (updated_at),
  CONSTRAINT fk_player_legacy_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE,
  CONSTRAINT chk_player_legacy_totals CHECK (total_crimes >= 0 AND max_infamy_level >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Héritage narratif au niveau compte';

-- ---------------------------------------------------------------------------
-- bounties : primes sur personnages
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS bounties (
  id                    BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  target_character_id   BIGINT UNSIGNED NOT NULL,
  amount                INT NOT NULL DEFAULT 0,
  level                 INT NOT NULL DEFAULT 1,
  region_id             BIGINT UNSIGNED NOT NULL DEFAULT 0,
  is_active             TINYINT(1) NOT NULL DEFAULT 1,
  created_at            TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_bounties_target_character_id (target_character_id),
  KEY ix_bounties_created_at (created_at),
  KEY ix_bounties_active_region (is_active, region_id),
  KEY ix_bounties_region_id (region_id),
  CONSTRAINT fk_bounties_target_character
    FOREIGN KEY (target_character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT chk_bounties_amount CHECK (amount >= 0),
  CONSTRAINT chk_bounties_level CHECK (level >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Primes posées sur un personnage';

-- ---------------------------------------------------------------------------
-- prison_records : peines par personnage / faction
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS prison_records (
  id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  character_id    BIGINT UNSIGNED NOT NULL,
  faction_id      BIGINT UNSIGNED NOT NULL DEFAULT 0,
  sentence_time   INT NOT NULL DEFAULT 0
                  COMMENT 'durée de peine (unité définie par le gameplay, ex. secondes)',
  is_completed    TINYINT(1) NOT NULL DEFAULT 0,
  created_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY ix_prison_records_character_id (character_id),
  KEY ix_prison_records_faction_id (faction_id),
  KEY ix_prison_records_created_at (created_at),
  KEY ix_prison_records_char_created (character_id, created_at),
  CONSTRAINT fk_prison_records_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE,
  CONSTRAINT chk_prison_sentence CHECK (sentence_time >= 0)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Historique de peines / prison';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;

-- ---------------------------------------------------------------------------
-- Purge automatique : données > 6 mois (UTC). Activer l’ordonnanceur : SET GLOBAL event_scheduler = ON;
-- ---------------------------------------------------------------------------
DROP EVENT IF EXISTS ev_lcdlln_purge_npc_memory_6m;
DROP EVENT IF EXISTS ev_lcdlln_purge_character_history_6m;

CREATE EVENT ev_lcdlln_purge_npc_memory_6m
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP
ON COMPLETION PRESERVE
ENABLE
COMMENT 'Purge npc_memory : last_interaction > 6 mois'
DO DELETE FROM npc_memory WHERE last_interaction < DATE_SUB(UTC_TIMESTAMP(), INTERVAL 6 MONTH);

CREATE EVENT ev_lcdlln_purge_character_history_6m
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP
ON COMPLETION PRESERVE
ENABLE
COMMENT 'Purge character_history : created_at > 6 mois (optionnel opérationnel)'
DO DELETE FROM character_history WHERE created_at < DATE_SUB(UTC_TIMESTAMP(), INTERVAL 6 MONTH);
