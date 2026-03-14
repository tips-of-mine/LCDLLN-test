CREATE TABLE IF NOT EXISTS characters (
    character_key INTEGER PRIMARY KEY,
    zone_id INTEGER NOT NULL,
    position_x REAL NOT NULL,
    position_y REAL NOT NULL,
    position_z REAL NOT NULL,
    current_health INTEGER NOT NULL,
    max_health INTEGER NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS inventory_items (
    character_key INTEGER NOT NULL,
    item_id INTEGER NOT NULL,
    quantity INTEGER NOT NULL,
    PRIMARY KEY (character_key, item_id),
    FOREIGN KEY (character_key) REFERENCES characters(character_key)
);
