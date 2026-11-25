-- Migration 0101 — Personnalisation complète du personnage persistée en base.
--
-- Suite de 0067 (genre) et 0068 (teinte de peau) : l'écran de création propose
-- désormais, pour chaque race jouable et chaque genre, visage, coiffure, couleur
-- des cheveux et des yeux, barbe, moustache, oreilles, cornes, queue, ailes,
-- défenses et proportions corporelles. Le client envoie un bloc binaire versionné
-- (CharacterAppearance, src/shared/network/CharacterAppearance.h, 20 octets en
-- version 1) dans CharacterCreateRequestPayload.appearance ; le master le stocke
-- ici et le renvoie dans CharacterListEntry.appearance ; le shard le lit à la
-- connexion et le réplique aux autres joueurs (identité du snapshot).
--
-- NULL = personnage créé avant cette migration : apparence par défaut (première
-- option de chaque emplacement), sans perte de données.
--
-- Idempotent : colonne ajoutée uniquement si manquante.
-- Réversible : ALTER TABLE characters DROP COLUMN appearance;

SET NAMES utf8mb4;

SET @m97_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'appearance'
);
SET @m97_s := IF(@m97_c = 0,
  'ALTER TABLE characters ADD COLUMN appearance VARBINARY(32) NULL DEFAULT NULL AFTER skin_color_idx',
  'SELECT 1');
PREPARE m97_p FROM @m97_s;
EXECUTE m97_p;
DEALLOCATE PREPARE m97_p;
