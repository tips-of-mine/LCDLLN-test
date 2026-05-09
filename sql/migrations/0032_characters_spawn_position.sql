-- Migration 0032 — Position et orientation de spawn par personnage
-- Phase 3.6 : permet au shard / engine client de positionner la caméra à la dernière
-- position connue du personnage (ou un spawn par défaut au moment de la création)
-- au lieu de l'origine (0,0,0). Les unités sont des mètres pour les positions
-- (cohérent avec engine::math::Vec3) et des degrés pour l'orientation (lisibilité DB).
-- Idempotent : colonnes ajoutées uniquement si manquantes.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- spawn_x : position X dans le monde (mètres)
SET @m32_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'spawn_x'
);
SET @m32_s1 := IF(@m32_c1 = 0,
  'ALTER TABLE characters ADD COLUMN spawn_x FLOAT NOT NULL DEFAULT 0.0 COMMENT ''Position X au spawn / dernière connexion (mètres)''',
  'SELECT 1');
PREPARE m32_p1 FROM @m32_s1;
EXECUTE m32_p1;
DEALLOCATE PREPARE m32_p1;

-- spawn_y : altitude (mètres)
SET @m32_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'spawn_y'
);
SET @m32_s2 := IF(@m32_c2 = 0,
  'ALTER TABLE characters ADD COLUMN spawn_y FLOAT NOT NULL DEFAULT 100.0 COMMENT ''Altitude au spawn (mètres ; 100 = au-dessus du terrain par défaut)''',
  'SELECT 1');
PREPARE m32_p2 FROM @m32_s2;
EXECUTE m32_p2;
DEALLOCATE PREPARE m32_p2;

-- spawn_z : position Z dans le monde (mètres)
SET @m32_c3 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'spawn_z'
);
SET @m32_s3 := IF(@m32_c3 = 0,
  'ALTER TABLE characters ADD COLUMN spawn_z FLOAT NOT NULL DEFAULT 0.0 COMMENT ''Position Z au spawn (mètres)''',
  'SELECT 1');
PREPARE m32_p3 FROM @m32_s3;
EXECUTE m32_p3;
DEALLOCATE PREPARE m32_p3;

-- spawn_yaw_deg : orientation horizontale (degrés)
SET @m32_c4 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'spawn_yaw_deg'
);
SET @m32_s4 := IF(@m32_c4 = 0,
  'ALTER TABLE characters ADD COLUMN spawn_yaw_deg FLOAT NOT NULL DEFAULT 0.0 COMMENT ''Orientation horizontale (degrés ; 0 = +Z)''',
  'SELECT 1');
PREPARE m32_p4 FROM @m32_s4;
EXECUTE m32_p4;
DEALLOCATE PREPARE m32_p4;

-- spawn_pitch_deg : orientation verticale (degrés)
SET @m32_c5 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'spawn_pitch_deg'
);
SET @m32_s5 := IF(@m32_c5 = 0,
  'ALTER TABLE characters ADD COLUMN spawn_pitch_deg FLOAT NOT NULL DEFAULT -10.0 COMMENT ''Orientation verticale (degrés ; -90..+90 ; -10 = légèrement vers le sol)''',
  'SELECT 1');
PREPARE m32_p5 FROM @m32_s5;
EXECUTE m32_p5;
DEALLOCATE PREPARE m32_p5;

SET FOREIGN_KEY_CHECKS = 1;
