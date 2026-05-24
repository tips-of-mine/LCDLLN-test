-- Migration 0068 — Teinte de peau du personnage persistée en base.
--
-- Suite du genre (0067) : la teinte de peau (skinColorIdx) était jusqu'ici un
-- réglage purement client (aperçu de création). On la persiste côté serveur :
-- le client l'envoie à la création (CharacterCreateRequestPayload.customization
-- .skinColorIdx) et le master la renvoie dans CharacterListEntry.skin_color_idx
-- → la bonne teinte au relog, quelle que soit la machine.
--
-- Valeurs : 0 = claire (défaut), 1 = foncée (extensible à une palette plus tard).
--
-- Idempotent : colonne ajoutée uniquement si manquante. Défaut 0 (claire) pour
-- les personnages créés avant cette migration.

SET NAMES utf8mb4;

SET @m68_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'skin_color_idx'
);
SET @m68_s := IF(@m68_c = 0,
  'ALTER TABLE characters ADD COLUMN skin_color_idx TINYINT UNSIGNED NOT NULL DEFAULT 0',
  'SELECT 1');
PREPARE m68_p FROM @m68_s;
EXECUTE m68_p;
DEALLOCATE PREPARE m68_p;
