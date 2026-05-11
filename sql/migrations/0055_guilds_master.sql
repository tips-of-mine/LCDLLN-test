-- 0055 - Wave 5 Persistence Guilds (Phase 5.21b) : tables guilds_master,
-- guild_members_v2 et guild_bank pour persister la definition des guildes
-- V1 (2 guildes hardcodees + leurs membres + bank tab 0). Au boot, le
-- master prefere charger depuis la DB plutot que le seed hardcode dans
-- GuildHandler::SeedV1Guilds. Si la table est vide ou la DB indisponible,
-- le seed in-memory s'applique (fallback degrade).
--
-- guilds_master :
--   - guild_id : auto-increment PK. V1 ids 1..2 reserves au seed.
--   - leader_account_id : pointe sur accounts.account_id (mais pas de
--     FK ici, on garde la table autonome pour permettre des seeds
--     decorelles des accounts reels).
--   - created_at_unix_ms : timestamp creation pour l'audit.
--
-- guild_members_v2 :
--   - cle composite (guild_id, account_id) : un account ne peut etre
--     que dans une seule guilde a la fois (regle V1).
--   - rank_id : 0=Guild Master .. 9=Initiate, defaut 5=Member.
--
-- guild_bank :
--   - bank tab 0 only en V1 (tab_index hardcode a 0 dans le seed mais
--     la colonne est presente pour preparer V2).
--   - cle composite (guild_id, tab_index, slot_index) : un slot ne peut
--     contenir qu'un item type a la fois (stack au niveau du count).
--
-- Le seed V1 est insere via INSERT IGNORE pour rester idempotent.
-- Les 2 guildes "Les Gardiens" (id=1) et "L Ombre" (id=2, sans
-- apostrophe car ASCII safe MSVC) correspondent au seed hardcode de
-- GuildHandler::SeedV1Guilds. Les noms de membres sont stockes via
-- account_id (1=Aragorn, 2=Legolas, 3=Gimli, 4=Frodo, 5=Saruman,
-- 6=Wormtongue) -- la resolution accountId -> name reste cote handler
-- (V1 lookup en memoire jusqu'a integration AccountStore).

CREATE TABLE IF NOT EXISTS guilds_master (
    guild_id            INT UNSIGNED NOT NULL AUTO_INCREMENT,
    name                VARCHAR(64) NOT NULL,
    motd                VARCHAR(256) NOT NULL DEFAULT '',
    leader_account_id   BIGINT UNSIGNED NOT NULL,
    created_at_unix_ms  BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (guild_id),
    UNIQUE KEY uk_guild_name (name),
    KEY idx_leader (leader_account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS guild_members_v2 (
    guild_id           INT UNSIGNED NOT NULL,
    account_id         BIGINT UNSIGNED NOT NULL,
    rank_id            TINYINT UNSIGNED NOT NULL DEFAULT 5,
    joined_at_unix_ms  BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (guild_id, account_id),
    KEY idx_account (account_id),
    CONSTRAINT fk_guild_members_v2_guild FOREIGN KEY (guild_id)
        REFERENCES guilds_master(guild_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS guild_bank (
    guild_id           INT UNSIGNED NOT NULL,
    tab_index          TINYINT UNSIGNED NOT NULL DEFAULT 0,
    slot_index         INT UNSIGNED NOT NULL,
    item_template_id   INT UNSIGNED NOT NULL,
    item_name          VARCHAR(128) NOT NULL,
    count              INT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (guild_id, tab_index, slot_index),
    CONSTRAINT fk_guild_bank_guild FOREIGN KEY (guild_id)
        REFERENCES guilds_master(guild_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Seed V1 : 2 guildes + 6 membres + 7 items bank. INSERT IGNORE rend
-- la migration idempotente (re-application = no-op si dej seedee).
-- Les timestamps created_at_unix_ms / joined_at_unix_ms sont fixes a
-- 1767225600000 (2025-12-31 19:00:00 UTC) pour donner un repere
-- stable a travers les redeploys (corresponds approximativement a
-- la mise en service V1 alpha).

INSERT IGNORE INTO guilds_master (guild_id, name, motd, leader_account_id, created_at_unix_ms) VALUES
    (1, 'Les Gardiens', 'Soyez courageux',     1, 1767225600000),
    (2, 'L Ombre',      'Le pouvoir est tout', 5, 1767225600000);

-- account_id mapping V1 (resolu cote handler via lookup local) :
--   1=Aragorn (GM), 2=Legolas (Officer), 3=Gimli (Member), 4=Frodo (Initiate)
--   5=Saruman (GM), 6=Wormtongue (Member)
INSERT IGNORE INTO guild_members_v2 (guild_id, account_id, rank_id, joined_at_unix_ms) VALUES
    (1, 1, 0, 1767225600000),
    (1, 2, 1, 1767225600000),
    (1, 3, 5, 1767225600000),
    (1, 4, 9, 1767225600000),
    (2, 5, 0, 1767225600000),
    (2, 6, 5, 1767225600000);

-- Bank tab 0 : 5 items pour Les Gardiens, 2 items pour L Ombre.
-- itemTemplateId V1 : 1=Iron Ore, 2=Linen Cloth, 3=Mageweave,
-- 4=Health Potion, 5=Mana Potion, 6=Black Cloth, 7=Soul Shard.
-- (Aligne sur le seed in-memory hardcode de SeedV1Guilds.)
INSERT IGNORE INTO guild_bank (guild_id, tab_index, slot_index, item_template_id, item_name, count) VALUES
    (1, 0, 0, 1, 'Iron Ore',       100),
    (1, 0, 1, 2, 'Linen Cloth',    250),
    (1, 0, 2, 3, 'Mageweave',      80),
    (1, 0, 3, 4, 'Health Potion',  30),
    (1, 0, 4, 5, 'Mana Potion',    20),
    (2, 0, 0, 6, 'Black Cloth',    50),
    (2, 0, 1, 7, 'Soul Shard',     10);
