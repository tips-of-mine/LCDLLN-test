-- Migration 0035 — Seed de la table `servers` (créée vide par 0004)
--
-- Cause du bug : `CharacterCreateHandler::ResolveDefaultServerId` interroge la
-- table `servers` (legacy registry, schema 0004). Cette table reste *vide* car
-- aucun INSERT n'est jamais émis : tout le code applicatif (ServerRegistry,
-- ServerListHandler, etc.) écrit dans `game_servers` (schema 0017). Resultat :
-- toute creation de personnage echouait avec "no target server configured" car
-- `SELECT id FROM servers ...` retournait 0 lignes.
--
-- La FK `fk_characters_server_id` (0004) reference `servers.id`, donc on ne
-- peut pas simplement supprimer la table sans casser la contrainte d'integrite.
-- Plus court : seeder une ligne par defaut id=1 ('main') ; les futurs shards
-- pourront aussi y etre seed manuellement si besoin.
--
-- Idempotent : INSERT IGNORE sur la PRIMARY KEY (id).
-- Reversible : DELETE FROM servers WHERE id = 1 AND name = 'main'.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT IGNORE INTO servers (id, name, region, status) VALUES
  (1, 'main', '', 1);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
