-- Migration 0103 — Roadmap items v22 : Feyhin Lokcthat, la cité des gladiateurs
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Feyhin Lokcthat, la cité des gladiateurs';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Feyhin Lokcthat, la cité des gladiateurs' AS title,
         'Feyhin Lokcthat se dresse désormais sur un éperon rocheux dans une boucle de la rivière : on y arrive par un long pont de pierre à arches, la ville haute grimpe en gradins jusqu''à une cathédrale à haute flèche, et la ville basse s''étale au pied de la falaise, au bord de l''eau. Dans la ville basse, l''arène des gladiateurs ouvre ses gradins, sa palissade et ses braseros ; sous ses gradins, des geôles voûtées où l''on descend à la lueur des torches.' AS description,
         'completed' AS status, 'monde' AS category, 81 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
