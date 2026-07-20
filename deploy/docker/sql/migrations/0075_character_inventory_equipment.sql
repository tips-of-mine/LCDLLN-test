-- Roadmap-10 (2026-07-20) — persistance MySQL des personnages : inventaire +
-- équipement porté. Le wallet réutilise player_wallet (migration 0012, jusqu'ici
-- définie mais inutilisée par le runtime). Écrites par le SHARD en write-through
-- à chaque save de personnage (SaveCharacterToDb) ; lues à l'enter-world
-- (LoadCharacterFromDb). Le fichier .ini du shard reste écrit en parallèle
-- (filet de secours ; shard sans config db = comportement fichier inchangé).
-- character_id référence characters.id (même base lcdlln_master que le master).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- character_inventory : sac du personnage (une ligne par pile d'objets).
-- slot_index = position dans le vector inventaire du shard (ordre stable).
-- Remplacement intégral à chaque save (DELETE + INSERT en transaction).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS character_inventory (
  character_id BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
  slot_index   INT UNSIGNED    NOT NULL COMMENT 'position dans le sac (0..n)',
  item_id      INT UNSIGNED    NOT NULL COMMENT 'items.json',
  quantity     INT UNSIGNED    NOT NULL DEFAULT 1,
  updated_at   TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (character_id, slot_index),
  CONSTRAINT fk_character_inventory_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Sac runtime ; source de vérité = shard (write-through au save)';

-- ---------------------------------------------------------------------------
-- character_equipment : équipement porté (une ligne par slot occupé).
-- slot_id = valeur de engine::items::EquipmentSlot (1..12, cf. ItemDefinition.h).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS character_equipment (
  character_id BIGINT UNSIGNED  NOT NULL COMMENT 'characters.id',
  slot_id      TINYINT UNSIGNED NOT NULL COMMENT 'engine::items::EquipmentSlot (1..12)',
  item_id      INT UNSIGNED     NOT NULL COMMENT 'items.json',
  updated_at   TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (character_id, slot_id),
  CONSTRAINT fk_character_equipment_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Équipement porté runtime ; source de vérité = shard (write-through au save)';

SET FOREIGN_KEY_CHECKS = 1;

COMMIT;
