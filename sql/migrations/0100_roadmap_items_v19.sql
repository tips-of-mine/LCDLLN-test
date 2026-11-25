-- Migration 0100 — Roadmap items v19 : pictogrammes définitifs des pouvoirs de classe
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Nouvelles icônes des pouvoirs de classe';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Nouvelles icônes des pouvoirs de classe' AS title,
         'Chaque pouvoir des 24 classes reçoit son icône peinte : carreaux et pavois de l''arbalétrier, givre du mage, feu infernal du démoniste, sang du sorcier, lumière dorée du paladin… Le cadre de l''icône prend la couleur du palier (bronze, argent, or, violet, orange, cyan) et des points indiquent le rang. Les nouvelles icônes apparaissent dans la barre de pouvoirs, le grimoire et l''arbre de compétences.' AS description,
         'completed' AS status, 'ui' AS category, 78 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
