-- Carte à découvrir (monde « Royaumes ») — brouillard de la carte du monde,
-- PAR PERSONNAGE. Une ligne par cellule de 10 km visitée : masque de 6400 bits
-- (cases de 125 m, 800 octets, cf. src/shared/world/MapDiscovery.h).
-- Écrite par le MASTER (opcode 210, fusion OU des cases révélées par le
-- client) ; lue à l'entrée en jeu (opcodes 208/209).
-- character_id référence characters.id ; cell_zone_id = CellZoneId (≥ 1 000 000).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS character_map_discovery (
  character_id BIGINT UNSIGNED NOT NULL COMMENT 'characters.id',
  cell_zone_id INT UNSIGNED    NOT NULL COMMENT 'CellZoneId de la cellule',
  bits         VARBINARY(800)  NOT NULL COMMENT 'masque brut 80x80 cases (bit i = tz*80+tx)',
  updated_at   TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (character_id, cell_zone_id),
  CONSTRAINT fk_character_map_discovery_character
    FOREIGN KEY (character_id) REFERENCES characters (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Cases de la carte du monde découvertes par personnage';

SET FOREIGN_KEY_CHECKS = 1;

COMMIT;
