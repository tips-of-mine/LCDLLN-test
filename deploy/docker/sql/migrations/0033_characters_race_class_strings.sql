-- Migration 0033 — Stockage des identifiants race / classe en string
-- Phase 3.8 : aujourd'hui CharacterCreateRequestPayload porte raceId/classId
-- comme strings ("humains", "warrior", ...) mais le handler hardcode race_id=0
-- et class_id=0. On ajoute deux colonnes VARCHAR pour persister la valeur
-- réelle envoyée par le client. Les colonnes numériques race_id / class_id
-- restent en place (utiles pour des futures FK / index si besoin).
-- Idempotent : colonnes ajoutées uniquement si manquantes.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- race_str : id chaîne ("humains", "elfes", ...) du fichier races.json
SET @m33_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'race_str'
);
SET @m33_s1 := IF(@m33_c1 = 0,
  'ALTER TABLE characters ADD COLUMN race_str VARCHAR(32) NOT NULL DEFAULT '''' COMMENT ''Identifiant chaîne de la race (cf. game/data/races/races.json)''',
  'SELECT 1');
PREPARE m33_p1 FROM @m33_s1;
EXECUTE m33_p1;
DEALLOCATE PREPARE m33_p1;

-- class_str : id chaîne ("warrior", "mage", ...) du fichier classes.json
SET @m33_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'class_str'
);
SET @m33_s2 := IF(@m33_c2 = 0,
  'ALTER TABLE characters ADD COLUMN class_str VARCHAR(32) NOT NULL DEFAULT '''' COMMENT ''Identifiant chaîne de la classe (cf. game/data/races/classes.json)''',
  'SELECT 1');
PREPARE m33_p2 FROM @m33_s2;
EXECUTE m33_p2;
DEALLOCATE PREPARE m33_p2;

SET FOREIGN_KEY_CHECKS = 1;
