-- Migration 0094 — Roadmap items v13 : atelier décors, de la BD au jeu (lore v1.0.137)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Les décors de la BD', 'Enseignes et cimetières', 'Saisons dans le monde');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Les décors de la BD' AS title,
         'Les arbres, les champs, les maisons et les objets des Chroniques prennent place dans le monde : chênes, hêtres, peupliers, saules et séquoias, très grands champs de blé, d''orge, de maïs, de tournesols, de colza et de vignes autour des villes et des hameaux, roseaux et nénuphars au bord de l''eau, maisons de planches, de pierre sèche, d''ardoise et de brique, fontaines, marchés, ponts de pierre et de cordes, camps et champs de bataille.' AS description,
         'completed' AS status, 'monde' AS category, 70 AS display_order
  UNION ALL SELECT 'Enseignes et cimetières',
         'Chaque bâtiment de service porte une enseigne à pictogramme ; passez le curseur dessus ou cliquez pour lire le nom du service dans votre langue. Les cimetières se couvrent de tombes anciennes : tas de cailloux moussus, croix de bois et vieilles stèles.',
         'completed', 'monde', 71
  UNION ALL SELECT 'Saisons dans le monde',
         'Un cycle des saisons est envisagé : feuillages d''automne, arbres nus en hiver. En attendant, le monde garde la même saison partout.',
         'planned', 'monde', 72
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
