-- FIX.16 — Carte à découvrir : cases de 62,5 m au lieu de 125 m.
-- Le masque d'une cellule passe de 80 x 80 cases (800 octets) à 160 x 160
-- cases (3 200 octets), cf. src/shared/world/MapDiscovery.h. Avec des cases de
-- 125 m, le rayon de vision de 300 m ne couvrait que 4 x 4 cases : la zone
-- découverte apparaissait carrée sur la carte du monde.
--
-- Les lignes existantes (800 octets) restent valides : le master et le shard
-- les agrandissent à la lecture (chaque ancienne case devient 2 x 2 nouvelles
-- cases, CellBitmap::Assign) puis les réécrivent en 3 200 octets à la
-- prochaine sauvegarde. Aucune conversion de données ici : seul le type de la
-- colonne est élargi (VARBINARY(800) aurait tronqué / refusé les nouveaux masques).
-- Idempotent : ré-appliquer la modification de colonne est sans effet.

SET NAMES utf8mb4;

START TRANSACTION;

ALTER TABLE character_map_discovery
  MODIFY COLUMN bits VARBINARY(3200) NOT NULL
  COMMENT 'masque brut 160x160 cases de 62,5 m (bit i = tz*160+tx) ; 800 octets = ancien 80x80, agrandi a la lecture';

COMMIT;
