-- 0042 — Phase 1b CMANGOS Globals : 4 tables (conditions, condition_groups,
-- graveyards, locale_strings) + seeds minimaux pour les tests.

-- ─── conditions : prédicats atomiques data-driven ───
CREATE TABLE IF NOT EXISTS conditions (
  condition_id  INT UNSIGNED NOT NULL,
  type          TINYINT UNSIGNED NOT NULL,    -- enum ConditionType (0=LevelGE,1=LevelLE,2=HasItem,3=ZoneId,4=InGroup)
  value1        INT NOT NULL DEFAULT 0,
  value2        INT NOT NULL DEFAULT 0,
  value3        INT NOT NULL DEFAULT 0,
  description   VARCHAR(255),
  PRIMARY KEY (condition_id)
);

-- ─── condition_groups : composition logique AND/OR/NOT ───
CREATE TABLE IF NOT EXISTS condition_groups (
  group_id      INT UNSIGNED NOT NULL,
  logic         TINYINT UNSIGNED NOT NULL,    -- enum ConditionLogic (0=And,1=Or,2=Not)
  member_id     INT UNSIGNED NOT NULL,        -- condition_id ou group_id selon member_type
  member_type   TINYINT UNSIGNED NOT NULL,    -- 0=condition, 1=group
  PRIMARY KEY (group_id, member_id, member_type)
);

-- ─── graveyards : points de respawn ───
CREATE TABLE IF NOT EXISTS graveyards (
  id          INT UNSIGNED NOT NULL,
  map_id      INT UNSIGNED NOT NULL,
  position_x  FLOAT NOT NULL,
  position_y  FLOAT NOT NULL,
  position_z  FLOAT NOT NULL,
  faction     TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0=neutral, 1+ = faction id
  zone_id     INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (id),
  KEY ix_graveyards_map (map_id)
);

-- ─── locale_strings : i18n côté serveur ───
CREATE TABLE IF NOT EXISTS locale_strings (
  string_id   INT UNSIGNED NOT NULL,
  locale_id   TINYINT UNSIGNED NOT NULL,    -- 0=fr_FR, 1=en_US
  text        TEXT NOT NULL,
  PRIMARY KEY (string_id, locale_id)
);

-- ─── seeds (idempotents via INSERT IGNORE) ───

-- Conditions de test : C1=Lvl>=10, C2=Lvl<=20, C3=ZoneId=42
INSERT IGNORE INTO conditions (condition_id, type, value1, value2, value3, description) VALUES
  (1, 0, 10, 0, 0, 'Test LevelGE 10'),
  (2, 1, 20, 0, 0, 'Test LevelLE 20'),
  (3, 3, 42, 0, 0, 'Test ZoneId 42'),
  (4, 4, 0,  0, 0, 'Test InGroup');

-- Groups de test :
-- G100 = AND(C1, C2)  → niveau entre 10 et 20
-- G101 = OR(G100, C3) → niveau dans range OU zone 42
-- G102 = NOT(C4)       → pas en groupe
INSERT IGNORE INTO condition_groups (group_id, logic, member_id, member_type) VALUES
  (100, 0, 1, 0),  -- AND C1
  (100, 0, 2, 0),  -- AND C2
  (101, 1, 100, 1), -- OR G100
  (101, 1, 3, 0),  -- OR C3
  (102, 2, 4, 0);  -- NOT C4

-- Graveyards de test : 3 points sur map 0
INSERT IGNORE INTO graveyards (id, map_id, position_x, position_y, position_z, faction, zone_id) VALUES
  (1, 0,    0.0,   0.0,  0.0, 0, 0),
  (2, 0,  100.0,   0.0,  0.0, 1, 0),
  (3, 0,  200.0,   0.0,  0.0, 2, 0);

-- Locale strings de test : ID 1000 fr_FR + en_US, ID 1001 fr_FR seulement (test fallback)
INSERT IGNORE INTO locale_strings (string_id, locale_id, text) VALUES
  (1000, 0, 'Bienvenue {0}, niveau {1}!'),
  (1000, 1, 'Welcome {0}, level {1}!'),
  (1001, 0, 'Bonjour le monde');
