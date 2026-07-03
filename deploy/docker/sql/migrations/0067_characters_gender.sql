-- Migration 0067 — Genre du personnage persisté en base.
--
-- Jusqu'ici le genre de l'avatar était un réglage client global (puis par
-- personnage côté client uniquement, cf. character_appearance.json). On le
-- persiste désormais côté serveur : le client l'envoie à la création
-- (CharacterCreateRequestPayload.gender) et le master le renvoie dans
-- CharacterListEntry.gender → l'avatar correct (mesh + peau) au relog, quelle
-- que soit la machine.
--
-- Idempotent : colonne ajoutée uniquement si manquante. Défaut 'male' pour les
-- personnages créés avant cette migration.

SET NAMES utf8mb4;

SET @m67_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'gender'
);
SET @m67_s := IF(@m67_c = 0,
  'ALTER TABLE characters ADD COLUMN gender VARCHAR(8) NOT NULL DEFAULT ''male''',
  'SELECT 1');
PREPARE m67_p FROM @m67_s;
EXECUTE m67_p;
DEALLOCATE PREPARE m67_p;
