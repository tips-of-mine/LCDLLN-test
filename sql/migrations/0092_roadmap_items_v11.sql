-- Migration 0092 — Roadmap items v11 : nouveaux arbres « dark fantasy » (chênes, pins, arbres morts, saules)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Forêts sombres et arbres noueux';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Forêts sombres et arbres noueux' AS title,
         'De nouveaux arbres dans l''esprit des Chroniques peuplent les Royaumes : chênes massifs aux couronnes larges dans les plaines, pins élancés en montagne, arbres morts tordus sur les terres désolées et saules aux rideaux de feuilles au bord des marais.' AS description,
         'completed' AS status, 'monde' AS category, 67 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
