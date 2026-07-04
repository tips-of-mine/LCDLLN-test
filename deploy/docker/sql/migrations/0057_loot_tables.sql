-- 0057 - Wave 5 Persistence Loot (Phase 3.17b) : tables loot_tables +
-- loot_table_entries pour persister la definition des tables de loot
-- V1 (loup_base, lapin_base). Pattern similaire a game_events :
-- seed via INSERT IGNORE, lecture read-only au boot par le
-- LootHandler (qui utilise actuellement un tableau hardcode 5 items).
--
-- loot_tables :
--   - table_id : auto-increment PK. V1 ids 1..2 reserves au seed.
--   - name : identifiant logique unique (loup_base, lapin_base, ...).
--
-- loot_table_entries :
--   - drop_chance_pct : 0..100, probabilite que l'item drop (V1 simple).
--   - min_count / max_count : range de quantites possibles. min<=max.
--   - cle composite (entry_id) auto-increment pour pouvoir insere
--     plusieurs items du meme template_id dans une meme table.
--
-- Pas de UNIQUE sur (table_id, item_template_id) volontairement : il
-- est legitime d'avoir 2 entrees pour le meme item avec des drop_chance
-- distinctes (ex. drop pity vs drop bonus).

CREATE TABLE IF NOT EXISTS loot_tables (
    table_id     INT UNSIGNED NOT NULL AUTO_INCREMENT,
    name         VARCHAR(64) NOT NULL,
    description  VARCHAR(256) NOT NULL DEFAULT '',
    PRIMARY KEY (table_id),
    UNIQUE KEY uk_loot_table_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS loot_table_entries (
    entry_id           INT UNSIGNED NOT NULL AUTO_INCREMENT,
    table_id           INT UNSIGNED NOT NULL,
    item_template_id   INT UNSIGNED NOT NULL,
    item_name          VARCHAR(128) NOT NULL,
    drop_chance_pct    INT UNSIGNED NOT NULL DEFAULT 100,
    min_count          INT UNSIGNED NOT NULL DEFAULT 1,
    max_count          INT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (entry_id),
    KEY idx_table (table_id),
    CONSTRAINT fk_loot_entries_table FOREIGN KEY (table_id)
        REFERENCES loot_tables(table_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Seed V1 : 2 tables de base (loup_base + lapin_base) avec 3 entries
-- au total. INSERT IGNORE rend la migration idempotente.
INSERT IGNORE INTO loot_tables (table_id, name, description) VALUES
    (1, 'loup_base',   'Butin standard de loup'),
    (2, 'lapin_base',  'Butin standard de lapin');

-- entry_id explicite pour le seed (sinon AUTO_INCREMENT mais on prefere
-- des ids stables a travers les re-applications).
-- Note : item_template_id 1 = Peau de loup (pas Minerai de fer : on suppose
-- qu'a partir de la table loot_tables le master peut potentiellement
-- avoir des noms d'items disjoints du LootHandler hardcode 1..5 actuel).
INSERT IGNORE INTO loot_table_entries (entry_id, table_id, item_template_id, item_name, drop_chance_pct, min_count, max_count) VALUES
    (1, 1, 1,   'Peau de loup',    80, 1, 2),
    (2, 1, 100, 'Potion de soin',  10, 1, 1),
    (3, 2, 2,   'Patte de lapin',  95, 1, 1);
