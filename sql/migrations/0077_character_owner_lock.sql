-- Migration 0077 — Verrou de propriété des personnages (ETAPE1.2).
--
-- Règle : un seul propriétaire par personnage. À chaque entrée en jeu, le
-- MASTER attribue le personnage au shard qui va l'héberger
-- (CharacterEnterWorldHandler → CharacterOwnershipStore) :
--   owner_shard_id   = shard propriétaire (NULL = libre) ;
--   owner_generation = compteur incrémenté à chaque attribution ;
--   owner_acquired_at = date UTC de l'attribution.
-- Le SHARD reçoit la génération dans le push d'admission (opcode 199) et la
-- présente à chaque sauvegarde (ServerApp::SaveCharacterToDb, SELECT … FOR
-- UPDATE) : écriture refusée s'il n'est plus propriétaire. Quand le master
-- déclare un shard mort, il remet owner_shard_id à NULL pour ses personnages.
--
-- Idempotent : chaque colonne / index n'est ajouté que s'il manque. Défauts
-- compatibles avec l'existant (NULL / 0 = aucun verrou, comportement historique).

SET NAMES utf8mb4;

SET @m77_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'owner_shard_id'
);
SET @m77_s := IF(@m77_c = 0,
  'ALTER TABLE characters ADD COLUMN owner_shard_id INT UNSIGNED NULL DEFAULT NULL COMMENT ''shard propriétaire (NULL = libre)''',
  'SELECT 1');
PREPARE m77_p FROM @m77_s;
EXECUTE m77_p;
DEALLOCATE PREPARE m77_p;

SET @m77_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'owner_generation'
);
SET @m77_s := IF(@m77_c = 0,
  'ALTER TABLE characters ADD COLUMN owner_generation BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''génération du verrou, +1 par attribution''',
  'SELECT 1');
PREPARE m77_p FROM @m77_s;
EXECUTE m77_p;
DEALLOCATE PREPARE m77_p;

SET @m77_c := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND column_name = 'owner_acquired_at'
);
SET @m77_s := IF(@m77_c = 0,
  'ALTER TABLE characters ADD COLUMN owner_acquired_at DATETIME NULL DEFAULT NULL COMMENT ''date UTC de l''''attribution''',
  'SELECT 1');
PREPARE m77_p FROM @m77_s;
EXECUTE m77_p;
DEALLOCATE PREPARE m77_p;

-- Index : libération de tous les personnages d'un shard déclaré mort.
SET @m77_c := (
  SELECT COUNT(*) FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'characters' AND index_name = 'idx_characters_owner_shard'
);
SET @m77_s := IF(@m77_c = 0,
  'CREATE INDEX idx_characters_owner_shard ON characters (owner_shard_id)',
  'SELECT 1');
PREPARE m77_p FROM @m77_s;
EXECUTE m77_p;
DEALLOCATE PREPARE m77_p;
