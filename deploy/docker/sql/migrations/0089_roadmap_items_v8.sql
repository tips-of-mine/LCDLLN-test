-- Migration 0089 — Roadmap items v8 : mise à jour de la carte du monde (lore v1.0.128, lot 1)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Ports au bord de la mer', 'Un monde plus vivant à explorer');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Ports au bord de la mer' AS title,
         'Les villes portuaires des Royaumes (Hishtarland, Brenhaval, Thrival, Ostrevan, Ryvaldh, Ekvhitorn…) sont désormais au bord de l''eau, avec leur ponton : on voit la mer en arrivant. Le Krak de Jhenner apparaît en ruine, la Tour Carrée de la Lune Noire et le Palais des Maîtres des Dragons prennent leur vrai nom sur la carte.' AS description,
         'completed' AS status, 'monde' AS category, 62 AS display_order
  UNION ALL SELECT 'Un monde plus vivant à explorer',
         'Refonte de la carte des Royaumes en plusieurs étapes : grands fleuves nommés et rivières avec leurs ponts de pierre et de bois, montagnes et grandes collines, routes qui suivent les vallées, et nouveaux lieux bâtis (tours, châteaux, phares, cascades). Objectif : donner envie de partir à l''aventure.',
         'in_progress', 'monde', 63
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
