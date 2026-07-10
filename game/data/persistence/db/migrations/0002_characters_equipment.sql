-- Chantier 2 SP-A — équipement porté par personnage.
-- Note : le store de persistance runtime est aujourd'hui un fichier KV texte
-- (CharacterPersistenceStore) ; l'équipement y est écrit via les clés plates
-- equipment.<slot>.item_id. Ce fichier versionne le schéma cible (parité +
-- future bascule vers un back-end SQL) et documente la table d'équipement.
--
-- slot = valeur de engine::items::EquipmentSlot (1..10 ; cf. ItemDefinition.h).
-- L'ordre des slots est FIGÉ. item_id référence le catalogue game/data/items/items.json.
CREATE TABLE IF NOT EXISTS character_equipment (
    character_key INTEGER NOT NULL,
    slot INTEGER NOT NULL,
    item_id INTEGER NOT NULL,
    PRIMARY KEY (character_key, slot),
    FOREIGN KEY (character_key) REFERENCES characters(character_key)
);
