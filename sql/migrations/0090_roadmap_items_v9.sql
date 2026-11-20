-- Migration 0090 — Roadmap items v9 : refonte de la carte du monde terminée (lore v1.0.132, lots 2 à 5)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé ;
-- changement de statut gardé par l'ancien statut (modification de l'admin préservée).
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Fleuves, montagnes et routes de voyage', 'Châteaux, tours, phares et cascades');
--              UPDATE roadmap_items SET status = 'in_progress' WHERE title = 'Un monde plus vivant à explorer' AND status = 'completed';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Fleuves, montagnes et routes de voyage' AS title,
         'Les huit grandes rivières des Royaumes coulent enfin dans leurs vallées, rejointes par près de deux cents ruisseaux, avec des ponts de pierre et des passerelles de bois. Les montagnes prennent l''échelle des Pyrénées : sommets enneigés vers 3 400 m, cols où passent les routes, grandes collines dans les plaines. Les routes serpentent au lieu de filer en ligne droite.' AS description,
         'completed' AS status, 'monde' AS category, 64 AS display_order
  UNION ALL SELECT 'Châteaux, tours, phares et cascades',
         'Les lieux emblématiques sont bâtis dans le monde : la Tour Carrée de la Lune Noire et ses trente étages, le château circulaire de Garundel et ses douves, le Palais des Maîtres des Dragons, les tours de guet, les phares et leur feu, les cascades en gradins et les ponts des gorges, intacts ou en ruine.',
         'completed', 'monde', 65
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

UPDATE roadmap_items SET status = 'completed'
WHERE title = 'Un monde plus vivant à explorer' AND status = 'in_progress';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
